#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Slic3r/App/Plater/SlaHeightBandMeshes.hpp"
#include "Slic3r/App/Scene/Clipper.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/SLAObjectCache.hpp"
#include "Slic3r/Domain/Bed.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "libslic3r/SLAResult.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Scene = Slic3r::App::Scene;

using namespace Slic3r;
using namespace Slic3r::App::Plater;
using Catch::Approx;
using Slic3r::Biz::SLAObjectCache;
using Slic3r::Biz::SLAObjectOptRef;
using Slic3r::Biz::Slicing::Sla::Object;
using Slic3r::Domain::SlicingId;

namespace {

struct Fixture
{
    Fixture()
    {
        Domain::BedCreationData bed_data;
        bed_data.type             = Domain::BedType::Rectangle;
        bed_data.contour          = { { 0.0, 0.0 }, { 100.0, 0.0 }, { 100.0, 100.0 }, { 0.0, 100.0 } };
        bed_data.max_print_height = 200.0f;
        project.bed_container().beds().push_back(std::make_unique<Domain::Bed>(Domain::Bed::create(bed_data)));
        bed_instance = std::make_unique<Domain::BedInstance>(*project.bed_container().bed(0));
    }

    // Adds a model object with one instance on the bed, and returns that instance.
    Domain::ModelInstance* add_object(std::string name, bool printable = true)
    {
        Domain::ModelObject* object = project.model().add_object();
        object->name               = std::move(name);
        Biz::Algorithms::ModelObject::add_volume(
            object,
            Biz::Algorithms::TriangleMesh::make_cube(10.0, 10.0, 20.0),
            Domain::ModelVolumeType::MODEL_PART
        );
        Domain::ModelInstance* instance = object->add_instance();
        instance->printable             = printable;
        bed_instance->model_instances.push_back(instance);
        return instance;
    }

    // Puts an object into the SLA object cache the way the slicer does, and returns the support tree
    // and the raft it now holds.
    std::pair<std::shared_ptr<const Domain::TriangleMesh>, std::shared_ptr<const Domain::TriangleMesh>>
    set_cached_support(const Domain::ModelObject* object, bool with_support, bool with_pad = true)
    {
        Object cached;
        cached.object_id       = object->id();
        cached.instance_trafos = { { object->id(), Domain::Transform3d::Identity() } };
        if (with_support) {
            cached.support_structure = std::make_shared<const Domain::TriangleMesh>(
                Biz::Algorithms::TriangleMesh::make_cube(1.0, 1.0, 1.0)
            );
        }
        if (with_pad) {
            cached.pad = std::make_shared<const Domain::TriangleMesh>(
                Biz::Algorithms::TriangleMesh::make_cube(20.0, 20.0, 1.0)
            );
        }
        sla_object_cache.on_sla_object_changed(slicing_id, std::move(cached));

        const SLAObjectCache::Key key{ slicing_id, object->id() };
        const SLAObjectOptRef stored = sla_object_cache.get_instance(key);
        REQUIRE(stored);
        return { stored->get().support_structure, stored->get().pad };
    }

    std::vector<Scene::ExtraMesh> collect(const Domain::ModelInstance* selected = nullptr) const
    {
        return collect_height_band_meshes(*bed_instance, slicing_id, sla_object_cache, selected);
    }

    Domain::Project                       project;
    std::unique_ptr<Domain::BedInstance> bed_instance;
    SLAObjectCache                        sla_object_cache;
    SlicingId                             slicing_id{ 1, 1 };
};

// Number of collected meshes whose payload is the given one.
size_t count_of(const std::vector<Scene::ExtraMesh>& meshes, const std::shared_ptr<const Domain::TriangleMesh>& mesh)
{
    size_t count = 0;
    for (const auto& extra : meshes) {
        if (extra.mesh == mesh)
            ++count;
    }
    return count;
}

// The single collected mesh carrying the given payload, or nullptr.
const Scene::ExtraMesh* find(const std::vector<Scene::ExtraMesh>& meshes, const std::shared_ptr<const Domain::TriangleMesh>& mesh)
{
    for (const auto& extra : meshes) {
        if (extra.mesh == mesh)
            return &extra;
    }
    return nullptr;
}

} // namespace

TEST_CASE("collect_height_band_meshes - every printable instance on the bed is collected", "[SlaHeightBandMeshes]")
{
    Fixture fixture;
    Domain::ModelInstance* first  = fixture.add_object("A");
    Domain::ModelInstance* second = fixture.add_object("B");

    const auto meshes = fixture.collect();

    REQUIRE(meshes.size() == 2);
    // The two model parts come back, each with the transform of its own instance.
    CHECK(count_of(meshes, first->get_object()->volumes.front()->mesh_ptr()) == 1);
    CHECK(count_of(meshes, second->get_object()->volumes.front()->mesh_ptr()) == 1);
}

TEST_CASE("collect_height_band_meshes - a non printable instance is skipped", "[SlaHeightBandMeshes]")
{
    Fixture fixture;
    fixture.add_object("A");
    Domain::ModelInstance* hidden = fixture.add_object("B", /*printable=*/ false);

    const auto meshes = fixture.collect();

    REQUIRE(meshes.size() == 1);
    CHECK(count_of(meshes, hidden->get_object()->volumes.front()->mesh_ptr()) == 0);
}

TEST_CASE("collect_height_band_meshes - the selected instance is left to the clipper", "[SlaHeightBandMeshes]")
{
    Fixture fixture;
    Domain::ModelInstance* selected = fixture.add_object("A");
    Domain::ModelInstance* other    = fixture.add_object("B");

    const auto meshes = fixture.collect(selected);

    REQUIRE(meshes.size() == 1);
    CHECK(count_of(meshes, other->get_object()->volumes.front()->mesh_ptr()) == 1);
    CHECK(count_of(meshes, selected->get_object()->volumes.front()->mesh_ptr()) == 0);
}

TEST_CASE("collect_height_band_meshes - the support tree and the raft are collected", "[SlaHeightBandMeshes]")
{
    Fixture fixture;
    Domain::ModelInstance* instance = fixture.add_object("A");
    const auto [support_mesh, pad_mesh] = fixture.set_cached_support(instance->get_object(), /*with_support=*/ true);

    const auto meshes = fixture.collect();

    // The model part, the support tree and the raft.
    REQUIRE(meshes.size() == 3);
    CHECK(count_of(meshes, instance->get_object()->volumes.front()->mesh_ptr()) == 1);

    const Scene::ExtraMesh* support = find(meshes, support_mesh);
    const Scene::ExtraMesh* pad     = find(meshes, pad_mesh);
    REQUIRE(support != nullptr);
    REQUIRE(pad != nullptr);
    // Both sit where the model of their instance is, the support lift of the print frame is dropped.
    CHECK(support->trafo.translation().z() == Approx(instance->get_matrix().translation().z()));
    CHECK(pad->trafo.translation().z() == Approx(instance->get_matrix().translation().z()));
}

TEST_CASE("collect_height_band_meshes - a support tree of another bed does not count", "[SlaHeightBandMeshes]")
{
    Fixture fixture;
    Domain::ModelInstance* instance = fixture.add_object("A");
    fixture.set_cached_support(instance->get_object(), /*with_support=*/ true);

    const auto meshes = collect_height_band_meshes(
        *fixture.bed_instance, SlicingId{ 1, 2 }, fixture.sla_object_cache, nullptr
    );

    // Only the model part, the sliced supports belong to the other bed.
    REQUIRE(meshes.size() == 1);
    CHECK(count_of(meshes, instance->get_object()->volumes.front()->mesh_ptr()) == 1);
}

TEST_CASE("collect_height_band_meshes - an object missing from the cache has no supports", "[SlaHeightBandMeshes]")
{
    Fixture fixture;
    fixture.add_object("A");

    CHECK(fixture.collect().size() == 1);
}

TEST_CASE("collect_height_band_meshes - an empty bed collects nothing", "[SlaHeightBandMeshes]")
{
    Fixture fixture;

    CHECK(fixture.collect().empty());
}
