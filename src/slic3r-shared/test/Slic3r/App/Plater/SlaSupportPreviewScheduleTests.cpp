#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Plater/SlaSupportPreviewSchedule.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/Transformation.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "libslic3r/SLASupportTool.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

using Slic3r::App::Plater::ISlaSupportPreviewTimer;
using Slic3r::App::Plater::SlaSupportPreviewSchedule;
using Slic3r::Domain::ObjectID;
using Slic3r::sla::SupportToolModelMesh;

namespace {

/// Stands in for the platform timer queue: it collects the debounce and fires it when the test
/// says so, so the window can be stepped over without waiting for it.
class ManualTimer final : public ISlaSupportPreviewTimer
{
public:
    TimerId set(std::chrono::milliseconds delay, std::function<void()> callback) override
    {
        const TimerId id = ++m_next_id;
        m_callbacks.push_back({id, delay, std::move(callback)});
        return id;
    }

    void cancel(TimerId timer) override
    {
        std::erase_if(m_callbacks, [timer](const Entry& entry) { return entry.id == timer; });
    }

    /// Fires the oldest pending callback, the way the main thread would.
    void fire_next()
    {
        if (m_callbacks.empty()) {
            return;
        }
        const Entry entry = std::move(m_callbacks.front());
        m_callbacks.erase(m_callbacks.begin());
        entry.callback();
    }

    /// Takes the oldest pending callback without running it, the way a debounce that is already on
    /// its way to the main thread looks from here.
    std::function<void()> take_next()
    {
        if (m_callbacks.empty()) {
            return {};
        }
        const Entry entry = std::move(m_callbacks.front());
        m_callbacks.erase(m_callbacks.begin());
        return entry.callback;
    }

    std::size_t pending() const { return m_callbacks.size(); }

    std::chrono::milliseconds next_delay() const
    {
        return m_callbacks.empty() ? std::chrono::milliseconds{0} : m_callbacks.front().delay;
    }

private:
    struct Entry
    {
        TimerId                   id;
        std::chrono::milliseconds delay;
        std::function<void()>     callback;
    };

    TimerId            m_next_id{0};
    std::vector<Entry> m_callbacks;
};

/// Stands in for the support tool call on the worker thread: it counts the builds that were
/// started and remembers the stop it was handed, so the test can tell a cancelled build from one
/// that was allowed to finish.
struct FakeBuilder
{
    std::vector<SlaSupportPreviewSchedule::Request> started;
    bool                                            stop_requested{false};

    void build(const SlaSupportPreviewSchedule::Request& request)
    {
        started.push_back(request);
    }

    /// The engine asks the stop from time to time; this is how the schedule reaches it.
    void stop() { stop_requested = true; }
};

/// How many vertices the main thread had to copy for a build: nothing while the snapshot shares the
/// meshes of the model, the whole of a part once it holds a copy of one. Counts the buffers, not
/// the time, so a figure of any size is the same test.
std::size_t copied_vertices(const SupportToolModelMesh& model_mesh, const Slic3r::Domain::ModelObject& object)
{
    std::size_t copied = 0;
    for (const SupportToolModelMesh::Part& part : model_mesh.parts) {
        const bool shared = std::any_of(object.volumes.begin(), object.volumes.end(),
            [&part](const Slic3r::Domain::ModelVolume* vol) {
                return vol->is_model_part() && vol->mesh_ptr() == part.mesh;
            });
        if (!shared) {
            copied += part.mesh->its.vertices.size();
        }
    }
    return copied;
}

/// Stands in for what SlaSupportPreviewService does on the main thread when a build starts, and for
/// the engine call on the worker thread: the service snapshots the geometry of the object (M2.21c)
/// and hands it to the build, and the builder records how much of the mesh that cost.
struct SnapshotBuilder
{
    struct Started
    {
        SlaSupportPreviewSchedule::Request request;
        const Slic3r::Domain::ModelObject* object;
        SupportToolModelMesh               model_mesh;
    };

    std::vector<Started> started;
    std::size_t          copied{0};

    void start(const SlaSupportPreviewSchedule::Request& request, const Slic3r::Domain::ModelObject& object)
    {
        started.push_back({request, &object, Slic3r::sla::support_tool_model_mesh(object)});
        copied += copied_vertices(started.back().model_mesh, object);
    }
};

/// A model with one object holding a box.
Slic3r::Domain::ModelObject* add_box(Slic3r::Domain::Model& model, double x, double y, double z)
{
    Slic3r::Domain::ModelObject* object = model.add_object();
    Slic3r::Biz::Algorithms::ModelObject::add_volume(
        object, Slic3r::Biz::Algorithms::TriangleMesh::make_cube(x, y, z)
    );
    object->add_instance();
    return object;
}

/// A model with one object holding a plate of @p side quads a side, so the test can have a mesh
/// much bigger than the box without writing one out.
Slic3r::Domain::ModelObject* add_grid(Slic3r::Domain::Model& model, std::size_t side)
{
    std::vector<Slic3r::Domain::Vec3f> vertices;
    std::vector<Slic3r::Domain::Index3> faces;
    vertices.reserve((side + 1) * (side + 1));
    for (std::size_t y = 0; y <= side; ++y) {
        for (std::size_t x = 0; x <= side; ++x) {
            vertices.emplace_back(float(x), float(y), 0.f);
        }
    }
    for (std::size_t y = 0; y < side; ++y) {
        for (std::size_t x = 0; x < side; ++x) {
            const int i = int(y * (side + 1) + x);
            faces.push_back({ i, i + 1, i + int(side + 1) });
            faces.push_back({ i, i + int(side + 1), i + int(side + 1) + 1 });
        }
    }

    Slic3r::Domain::ModelObject* object = model.add_object();
    Slic3r::Biz::Algorithms::ModelObject::add_volume(
        object, Slic3r::Biz::Algorithms::TriangleMesh::construct(std::move(vertices), std::move(faces))
    );
    object->add_instance();
    return object;
}

struct Fixture
{
    ManualTimer                                       timer;
    std::vector<SlaSupportPreviewSchedule::Request> built;
    SlaSupportPreviewSchedule                        schedule{timer, [this](const SlaSupportPreviewSchedule::Request& request) {
        built.push_back(request);
    }};
};

} // namespace

TEST_CASE("SlaSupportPreviewSchedule - a burst of edits ends in one build", "[SlaSupportPreviewSchedule]")
{
    Fixture fx;

    // A dragged point or a moved slider asks again on every step, with a key of its own each time.
    fx.schedule.request({ObjectID{7}, 1});
    fx.schedule.request({ObjectID{7}, 2});
    fx.schedule.request({ObjectID{7}, 3});
    fx.schedule.request({ObjectID{7}, 4});

    // Nothing was built while the burst went on, and the object waits with one window only.
    CHECK(fx.built.empty());
    CHECK(fx.timer.pending() == 1);
    CHECK(fx.timer.next_delay() == SlaSupportPreviewSchedule::debounce);
    CHECK(fx.schedule.waiting_count() == 1);

    fx.timer.fire_next();

    REQUIRE(fx.built.size() == 1);
    CHECK(fx.built.front() == SlaSupportPreviewSchedule::Request(ObjectID{7}, 4));
    CHECK(fx.schedule.waiting_count() == 0);
    CHECK(fx.timer.pending() == 0);
}

TEST_CASE("SlaSupportPreviewSchedule - every object debounces on its own", "[SlaSupportPreviewSchedule]")
{
    Fixture fx;

    fx.schedule.request({ObjectID{7}, 1});
    fx.schedule.request({ObjectID{8}, 1});
    fx.schedule.request({ObjectID{8}, 2});

    CHECK(fx.timer.pending() == 2);

    fx.timer.fire_next();
    REQUIRE(fx.built.size() == 1);
    CHECK(fx.built.front() == SlaSupportPreviewSchedule::Request(ObjectID{7}, 1));

    fx.timer.fire_next();
    REQUIRE(fx.built.size() == 2);
    CHECK(fx.built.back() == SlaSupportPreviewSchedule::Request(ObjectID{8}, 2));
}

TEST_CASE("SlaSupportPreviewSchedule - a newer edit cancels the build in flight", "[SlaSupportPreviewSchedule]")
{
    Fixture fx;
    FakeBuilder builder;

    fx.schedule.request({ObjectID{7}, 1});
    fx.timer.fire_next();
    const SlaSupportPreviewSchedule::Request first = fx.built.front();

    fx.schedule.begin_build(first, [&builder]() { builder.stop(); });
    builder.build(first);
    CHECK(fx.schedule.building());

    // The slider moved on while the tree was building: the build stops instead of finishing into
    // the bin.
    fx.schedule.request({ObjectID{7}, 2});
    CHECK(builder.stop_requested);
    CHECK(builder.started.size() == 1);

    // The build that was stopped hands in nothing that may be shown.
    CHECK_FALSE(fx.schedule.is_current(first));
    fx.schedule.finish_build();
    CHECK_FALSE(fx.schedule.building());

    // And the new key builds after its own window, once.
    fx.timer.fire_next();
    REQUIRE(fx.built.size() == 2);
    CHECK(fx.built.back() == SlaSupportPreviewSchedule::Request(ObjectID{7}, 2));
}

TEST_CASE("SlaSupportPreviewSchedule - a result of an older key is stale", "[SlaSupportPreviewSchedule]")
{
    Fixture fx;

    fx.schedule.request({ObjectID{7}, 1});
    fx.timer.fire_next();
    const SlaSupportPreviewSchedule::Request first = fx.built.front();

    fx.schedule.begin_build(first, []() {});
    CHECK(fx.schedule.is_current(first));

    // A newer key arrived while the build ran, so its result describes a state that is gone.
    fx.schedule.request({ObjectID{7}, 2});
    CHECK_FALSE(fx.schedule.is_current(first));

    // And a build that already came back is stale for good.
    fx.schedule.finish_build();
    CHECK_FALSE(fx.schedule.is_current(first));
    CHECK(fx.schedule.is_current(SlaSupportPreviewSchedule::Request(ObjectID{7}, 2)));
}

TEST_CASE("SlaSupportPreviewSchedule - a build of another object keeps running", "[SlaSupportPreviewSchedule]")
{
    Fixture fx;
    FakeBuilder builder;

    fx.schedule.request({ObjectID{7}, 1});
    fx.schedule.request({ObjectID{8}, 1});
    fx.timer.fire_next();

    const SlaSupportPreviewSchedule::Request first = fx.built.front();
    fx.schedule.begin_build(first, [&builder]() { builder.stop(); });

    // The other object changed, the running build of this one is still the newest of its object.
    fx.schedule.request({ObjectID{8}, 2});
    CHECK_FALSE(builder.stop_requested);
    CHECK(fx.schedule.is_current(first));
}

TEST_CASE("SlaSupportPreviewSchedule - an object that lost its preview is forgotten", "[SlaSupportPreviewSchedule]")
{
    Fixture fx;
    FakeBuilder builder;

    fx.schedule.request({ObjectID{7}, 1});
    fx.schedule.forget(ObjectID{7});
    CHECK(fx.timer.pending() == 0);
    CHECK(fx.schedule.waiting_count() == 0);

    // A build of it that was running is stopped, and its result can no longer be current. The
    // worker slot itself stays busy until its result comes back (finish_build), so that a second
    // build is never started next to it.
    fx.schedule.request({ObjectID{7}, 2});
    fx.timer.fire_next();
    const SlaSupportPreviewSchedule::Request request = fx.built.front();
    fx.schedule.begin_build(request, [&builder]() { builder.stop(); });
    fx.schedule.forget(ObjectID{7});

    CHECK(builder.stop_requested);
    CHECK_FALSE(fx.schedule.is_current(request));
    CHECK(fx.schedule.building());
}

TEST_CASE("SlaSupportPreviewSchedule - another project drops every build", "[SlaSupportPreviewSchedule]")
{
    Fixture fx;
    FakeBuilder builder;

    fx.schedule.request({ObjectID{7}, 1});
    fx.schedule.request({ObjectID{8}, 1});
    fx.timer.fire_next();
    const SlaSupportPreviewSchedule::Request request = fx.built.front();
    fx.schedule.begin_build(request, [&builder]() { builder.stop(); });

    fx.schedule.clear();

    CHECK(builder.stop_requested);
    CHECK(fx.timer.pending() == 0);
    CHECK(fx.schedule.waiting_count() == 0);
    CHECK_FALSE(fx.schedule.building());
    CHECK_FALSE(fx.schedule.is_current(request));

    // The counter keeps counting over the change, so the object that comes back is a new build and
    // not the one that was dropped.
    fx.schedule.request({ObjectID{7}, 9});
    CHECK(fx.schedule.is_current(SlaSupportPreviewSchedule::Request(ObjectID{7}, 9)));
    CHECK_FALSE(fx.schedule.is_current(SlaSupportPreviewSchedule::Request(ObjectID{7}, 1)));
}

TEST_CASE("SlaSupportPreviewSchedule - a debounce of a gone service builds nothing", "[SlaSupportPreviewSchedule]")
{
    ManualTimer                                       timer;
    std::vector<SlaSupportPreviewSchedule::Request> built;
    std::function<void()>                             on_the_way;
    {
        SlaSupportPreviewSchedule schedule{timer, [&built](const SlaSupportPreviewSchedule::Request& request) {
            built.push_back(request);
        }};
        schedule.request({ObjectID{7}, 1});
        // The window is already over and the callback is on its way to the main thread when the
        // service goes away.
        on_the_way = timer.take_next();
    }

    REQUIRE(on_the_way);
    on_the_way();
    CHECK(built.empty());
}

TEST_CASE("SlaSupportPreviewSchedule - a build start shares the model's meshes", "[SlaSupportPreviewSchedule]")
{
    // A small model and one whose mesh is of the size the maintainer's figures have: what the main
    // thread hands to a build must not grow with either of them (M2.21c).
    Slic3r::Domain::Model       model;
    Slic3r::Domain::ModelObject* small = add_box(model, 20., 20., 40.);
    Slic3r::Domain::ModelObject* large = add_grid(model, 300);

    const std::size_t small_vertices = small->volumes.front()->mesh_ptr()->its.vertices.size();
    const std::size_t large_vertices = large->volumes.front()->mesh_ptr()->its.vertices.size();
    REQUIRE(large_vertices > 1000 * small_vertices);

    ManualTimer      timer;
    SnapshotBuilder  builder;
    SlaSupportPreviewSchedule schedule{timer, [&builder, small, large](const SlaSupportPreviewSchedule::Request& request) {
        builder.start(request, *small);
        builder.start(request, *large);
    }};

    // A burst of edits of both objects, each with a key of its own.
    schedule.request({ObjectID{7}, 1});
    schedule.request({ObjectID{7}, 2});
    schedule.request({ObjectID{7}, 3});
    schedule.request({ObjectID{7}, 4});
    schedule.request({ObjectID{8}, 1});
    schedule.request({ObjectID{8}, 2});

    timer.fire_next();
    timer.fire_next();

    // One build per object, and the main thread copied no vertex of either mesh to start them: the
    // snapshot shares the buffers the model holds, the worker copies them on its own thread.
    REQUIRE(builder.started.size() == 2);
    CHECK(builder.started[0].request == SlaSupportPreviewSchedule::Request(ObjectID{7}, 4));
    CHECK(builder.started[1].request == SlaSupportPreviewSchedule::Request(ObjectID{8}, 2));
    CHECK(builder.copied == 0);

    for (const SnapshotBuilder::Started& started : builder.started) {
        REQUIRE(started.model_mesh.parts.size() == 1);
        CHECK(started.model_mesh.parts.front().mesh.get() == started.object->volumes.front()->mesh_ptr().get());
    }
    // The small object and the big one were both built, so the zero above is not a mesh that never
    // reached a build.
    CHECK(builder.started[0].object == small);
    CHECK(builder.started[1].object == large);
}

TEST_CASE("SlaSupportPreviewSchedule - a build start keeps the geometry the model had", "[SlaSupportPreviewSchedule]")
{
    Slic3r::Domain::Model       model;
    Slic3r::Domain::ModelObject* object = add_box(model, 20., 20., 40.);

    ManualTimer      timer;
    SnapshotBuilder  builder;
    SlaSupportPreviewSchedule schedule{timer, [&builder, object](const SlaSupportPreviewSchedule::Request& request) {
        builder.start(request, *object);
    }};

    schedule.request({ObjectID{7}, 1});
    timer.fire_next();
    REQUIRE(builder.started.size() == 1);
    REQUIRE(builder.started.front().model_mesh.parts.size() == 1);

    const SupportToolModelMesh& model_mesh = builder.started.front().model_mesh;

    // The model goes on changing after the build started: the worker keeps reading the mesh the
    // snapshot was taken from, which no edit of the main thread can reach.
    const Slic3r::Domain::BoundingBox3d taken = Slic3r::Domain::bounding_box(model_mesh.parts.front().mesh->its);
    object->volumes.front()->set_mesh(Slic3r::Biz::Algorithms::TriangleMesh::make_cube(5., 5., 5.));
    object->volumes.front()->set_offset(Slic3r::Domain::Vec3d(10., 0., 0.));

    CHECK(builder.copied == 0);
    // Still the box of 20 x 20 x 40 mm the build was asked for, and not the 5 mm cube the model
    // holds now, and not its new placement either.
    CHECK(taken.max.x() == 20.);
    CHECK(taken.max.z() == 40.);
    CHECK(model_mesh.parts.front().matrix.isApprox(Slic3r::Domain::Transform3d::Identity()));
    CHECK(object->volumes.front()->mesh_ptr().get() != model_mesh.parts.front().mesh.get());
    CHECK(Slic3r::Domain::bounding_box(object->volumes.front()->mesh_ptr()->its).max.x() == 5.);
}
