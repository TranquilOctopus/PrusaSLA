#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Plater/SlaUnsupportedObjects.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/SLAObjectCache.hpp"
#include "Slic3r/Domain/Bed.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "libslic3r/SLAResult.hpp"

#include <memory>
#include <string>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::App::Plater;
using Slic3r::Biz::SLAObjectCache;
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

    // Adds a model object with one instance on the bed.
    Domain::ModelObject* add_object(std::string name, bool printable = true)
    {
        Domain::ModelObject*  object    = project.model().add_object();
        object->name                     = std::move(name);
        Domain::ModelInstance* instance  = object->add_instance();
        instance->printable              = printable;
        bed_instance->model_instances.push_back(instance);
        return object;
    }

    // Puts an object into the SLA object cache the way the slicer does. No entry at all, or an
    // entry without a support structure, is what makes an object count as unsupported.
    void set_support_structure(const Domain::ModelObject* object, bool with_support)
    {
        Object cached;
        cached.object_id       = object->id();
        cached.instance_trafos = { { object->id(), Domain::Transform3d::Identity() } };
        if (with_support) {
            cached.support_structure =
                std::make_shared<const Domain::TriangleMesh>(Biz::Algorithms::TriangleMesh::make_cube(1.0, 1.0, 1.0));
        }
        sla_object_cache.on_sla_object_changed(slicing_id, std::move(cached));
    }

    std::vector<std::string> collect() const
    {
        std::vector<std::string> names;
        for (const Domain::ModelObject* object :
             collect_unsupported_objects(sla_object_cache, slicing_id, *bed_instance, project)) {
            names.push_back(object->name);
        }
        return names;
    }

    Domain::Project                       project;
    std::unique_ptr<Domain::BedInstance> bed_instance;
    SLAObjectCache                       sla_object_cache;
    SlicingId                            slicing_id{ 1, 1 };
};

} // namespace

TEST_CASE("collect_unsupported_objects - object without a support structure is reported", "[SlaUnsupportedObjects]")
{
    Fixture fixture;
    Domain::ModelObject* object = fixture.add_object("A");
    fixture.set_support_structure(object, /*with_support=*/ false);

    const std::vector<std::string> names = fixture.collect();
    REQUIRE(names.size() == 1);
    CHECK(names.front() == "A");
}

TEST_CASE("collect_unsupported_objects - object missing from the cache is reported", "[SlaUnsupportedObjects]")
{
    Fixture fixture;
    fixture.add_object("A");

    const std::vector<std::string> names = fixture.collect();
    REQUIRE(names.size() == 1);
    CHECK(names.front() == "A");
}

TEST_CASE("collect_unsupported_objects - object with a support structure is not reported", "[SlaUnsupportedObjects]")
{
    Fixture fixture;
    Domain::ModelObject* object = fixture.add_object("A");
    fixture.set_support_structure(object, /*with_support=*/ true);

    CHECK(fixture.collect().empty());
}

TEST_CASE("collect_unsupported_objects - a support structure of another bed does not count", "[SlaUnsupportedObjects]")
{
    Fixture fixture;
    Domain::ModelObject* object = fixture.add_object("A");
    fixture.set_support_structure(object, /*with_support=*/ true);

    const std::vector<const Domain::ModelObject*> unsupported{
        collect_unsupported_objects(fixture.sla_object_cache, SlicingId{ 1, 2 }, *fixture.bed_instance, fixture.project)
    };
    REQUIRE(unsupported.size() == 1);
    CHECK(unsupported.front() == object);
}

TEST_CASE("collect_unsupported_objects - each object is reported once, non-printable ones are skipped", "[SlaUnsupportedObjects]")
{
    Fixture fixture;
    Domain::ModelObject* first  = fixture.add_object("A");
    Domain::ModelObject* second = fixture.add_object("B");
    Domain::ModelObject* third  = fixture.add_object("C", /*printable=*/ false);

    // A second instance of the same object must not duplicate it in the result.
    fixture.bed_instance->model_instances.push_back(first->add_instance());

    fixture.set_support_structure(first, /*with_support=*/ true);
    fixture.set_support_structure(second, /*with_support=*/ false);
    // C has no supports either, but nothing of it gets printed, so it is not worth a warning.
    fixture.set_support_structure(third, /*with_support=*/ false);

    const std::vector<std::string> names = fixture.collect();
    REQUIRE(names.size() == 1);
    CHECK(names.front() == "B");
}

TEST_CASE("collect_unsupported_objects - an empty bed has no unsupported objects", "[SlaUnsupportedObjects]")
{
    Fixture fixture;

    CHECK(fixture.collect().empty());
}
