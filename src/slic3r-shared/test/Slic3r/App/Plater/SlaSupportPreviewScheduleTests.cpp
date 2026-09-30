#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Plater/SlaSupportPreviewSchedule.hpp"
#include "Slic3r/Domain/ObjectID.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

using Slic3r::App::Plater::ISlaSupportPreviewTimer;
using Slic3r::App::Plater::SlaSupportPreviewSchedule;
using Slic3r::Domain::ObjectID;

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
