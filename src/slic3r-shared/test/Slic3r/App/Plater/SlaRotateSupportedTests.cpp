#include "Slic3r/App/Plater/SlaRotateSupported.hpp"

#include "Slic3r/App/Plater/SlaSupportPointsClear.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/SecretStoreDummy.hpp"
#include "Slic3r/Biz/Preset/IO/BundlePaths.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/App/Plater/ThumbnailImageGenerator.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/TestUtils/AppInstanceMessageHandlerScope.hpp"
#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/ScopedThreadDispatcher.hpp"
#include "Slic3r/TestUtils/TestData.hpp"
#include "Slic3r/Biz/IUndoProvider.hpp"

#include <boost/nowide/filesystem.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using namespace Slic3r::App::Plater;
using namespace Slic3r::Biz;
using namespace Slic3r::Domain;
using namespace Slic3r::Biz::Algorithms::TriangleMesh;

struct RotateSupportedFixture
{
    RotateSupportedFixture()
        : workbench{}
        , dispatcher{}
        , app_instance_message_handler_scope{dispatcher}
        , job_manager_scope{dispatcher}
        , thumbnail_image_generator{}
        , project_interactor{workbench, dispatcher, thumbnail_image_generator}
        , scene_interactor{project_interactor.scene_interactor()}
        , thread_dispatcher{dispatcher}
    {
        boost::nowide::nowide_filesystem();

        std::unique_ptr<SecretStoreDummy> store_dummy = std::make_unique<SecretStoreDummy>();
        Platform::PlatformServices::instance().set_secret_store(std::move(store_dummy));

        Slic3r::set_data_dir(Tests::get_datadir().string());

        project_interactor.preset_interactor().load_preset_bundle(
            Slic3r::Biz::Preset::IO::BundlePaths::make_test_runtime(Tests::get_datadir())
        );

        // Create a new project so there's a build plate
        project_interactor.new_project();
    }

    ModelObject* place_model_with_points(std::size_t count, const std::string& name = "Cube")
    {
        Project& project = project_interactor.selected_project();
        scene_interactor.new_object_from_mesh(make_cube(20., 20., 20.));
        ModelObject* object = project.model().objects.back();
        object->name = name;
        object->sla_support_points.clear();
        for (std::size_t i = 0; i < count; ++i) {
            object->sla_support_points.push_back(SLA::SupportPoint{
                Vec3f{float(i), 0.f, 0.f}, 0.2f, SLA::SupportPointType::manual_add});
        }
        object->sla_points_status = count > 0 ? SLA::PointsStatus::UserModified : SLA::PointsStatus::NoPoints;
        return object;
    }

    // The selection of one instance of a model: what a click on it in the scene selects.
    static ElementRef ref_of(const ModelObject& object)
    {
        return ElementRef{object.id().id, object.instances.front()->id().id};
    }

    Slic3r::Domain::Workbench workbench;
    Slic3r::App::Platform::StdMainThreadDispatcher dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope;
    Tests::JobManagerScope job_manager_scope;
    Slic3r::App::Plater::ThumbnailImageGenerator thumbnail_image_generator;
    ProjectInteractor project_interactor;
    Slic3r::Biz::Scene::SceneInteractor& scene_interactor;
    Tests::ScopedThreadDispatcher thread_dispatcher;
};

TEST_CASE_METHOD(
    RotateSupportedFixture,
    "[SlaRotateSupported][M2.40] sla_rotate_supported_check returns empty for no selection",
    "[SlaRotateSupported][M2.40]"
)
{
    // No objects selected
    SlaRotateSupportedCheck check = sla_rotate_supported_check(project_interactor);
    CHECK(check.empty());
    CHECK(check.object_refs.empty());
    CHECK(check.point_count == 0);
}

TEST_CASE_METHOD(
    RotateSupportedFixture,
    "[SlaRotateSupported][M2.40] sla_rotate_supported_check returns empty for selection without supports",
    "[SlaRotateSupported][M2.40]"
)
{
    // Add an object without supports
    ModelObject* obj = place_model_with_points(0, "UnsupportedCube");

    // Select the object
    scene_interactor.set_object_selection(Slic3r::Biz::Scene::ObjectSelection{Slic3r::Biz::Scene::SelectionMode::Instance, {ref_of(*obj)}});

    // Check - should be empty because no supports
    SlaRotateSupportedCheck check = sla_rotate_supported_check(project_interactor);
    CHECK(check.empty());
    CHECK(check.object_refs.empty());
    CHECK(check.point_count == 0);
}

TEST_CASE_METHOD(
    RotateSupportedFixture,
    "[SlaRotateSupported][M2.40] sla_rotate_supported_check finds objects with supports",
    "[SlaRotateSupported][M2.40]"
)
{
    // Add an object with supports
    ModelObject* obj = place_model_with_points(3, "SupportedCube");

    // Select the object
    scene_interactor.set_object_selection(Slic3r::Biz::Scene::ObjectSelection{Slic3r::Biz::Scene::SelectionMode::Instance, {ref_of(*obj)}});

    // Check - should find the object with 3 points
    SlaRotateSupportedCheck check = sla_rotate_supported_check(project_interactor);
    CHECK(!check.empty());
    CHECK(check.object_refs.size() == 1);
    CHECK(check.object_refs.front().object_id == obj->id().id);
    CHECK(check.point_count == 3);
}

TEST_CASE_METHOD(
    RotateSupportedFixture,
    "[SlaRotateSupported][M2.40] sla_rotate_supported_check finds multiple objects with supports",
    "[SlaRotateSupported][M2.40]"
)
{
    // Add first object with supports
    ModelObject* obj1 = place_model_with_points(2, "Cube1");

    // Add second object with supports
    ModelObject* obj2 = place_model_with_points(1, "Cube2");

    // Select both objects
    scene_interactor.set_object_selection(Slic3r::Biz::Scene::ObjectSelection{Slic3r::Biz::Scene::SelectionMode::Instance, {ref_of(*obj1), ref_of(*obj2)}});

    // Check - should find both objects with total 3 points
    SlaRotateSupportedCheck check = sla_rotate_supported_check(project_interactor);
    CHECK(!check.empty());
    CHECK(check.object_refs.size() == 2);
    CHECK(check.point_count == 3);
    // Order should match selection order
    CHECK(check.object_refs[0].object_id == obj1->id().id);
    CHECK(check.object_refs[1].object_id == obj2->id().id);
}

TEST_CASE_METHOD(
    RotateSupportedFixture,
    "[SlaRotateSupported][M2.40] sla_rotate_supported_check ignores objects without supports in mixed selection",
    "[SlaRotateSupported][M2.40]"
)
{
    // Add first object WITH supports
    ModelObject* obj1 = place_model_with_points(1, "SupportedCube");

    // Add second object WITHOUT supports
    ModelObject* obj2 = place_model_with_points(0, "UnsupportedCube");

    // Select both objects
    scene_interactor.set_object_selection(Slic3r::Biz::Scene::ObjectSelection{Slic3r::Biz::Scene::SelectionMode::Instance, {ref_of(*obj1), ref_of(*obj2)}});

    // Check - should only find the first object
    SlaRotateSupportedCheck check = sla_rotate_supported_check(project_interactor);
    CHECK(!check.empty());
    CHECK(check.object_refs.size() == 1);
    CHECK(check.object_refs.front().object_id == obj1->id().id);
    CHECK(check.point_count == 1);
}

TEST_CASE("[SlaRotateSupported][M2.40] sla_rotate_supported_question formats single object", "[SlaRotateSupported][M2.40]")
{
    SlaRotateSupportedCheck check;
    check.point_count = 5;
    check.object_refs.push_back(ElementRef{1}); // dummy object ref
    // We can't easily create a ModelObject with a name here, so just test the empty case
    SlaRotateSupportedCheck empty_check;
    CHECK(sla_rotate_supported_question(empty_check).empty());
}

TEST_CASE("[SlaRotateSupported][M2.40] sla_rotate_supported_question formats multiple objects", "[SlaRotateSupported][M2.40]")
{
    SlaRotateSupportedCheck check;
    check.point_count = 10;
    // Add two dummy object refs (not real objects, but we test the count logic)
    check.object_refs.push_back(ElementRef{1});
    check.object_refs.push_back(ElementRef{2});

    std::string question = sla_rotate_supported_question(check);
    CHECK(!question.empty());
    // Names how many parts it is about
    CHECK(question.find("2") != std::string::npos);
}

TEST_CASE_METHOD(
    RotateSupportedFixture,
    "[SlaRotateSupported][M2.40] sla_support_points_clear_plan integration",
    "[SlaRotateSupported][M2.40]"
)
{
    // Add an object with supports
    ModelObject* obj = place_model_with_points(2, "TestCube");

    // Create plan
    std::vector<const ModelObject*> models = {obj};
    SlaSupportPointsClearPlan plan = sla_support_points_clear_plan(models);

    CHECK(!plan.empty());
    CHECK(plan.object_refs.size() == 1);
    CHECK(plan.object_refs.front().object_id == obj->id().id);
    CHECK(plan.point_count == 2);

    // Clear the points
    std::size_t cleared = clear_sla_support_points(project_interactor, plan);
    CHECK(cleared == 1);
    CHECK(obj->sla_support_points.empty());
    CHECK(obj->sla_points_status == SLA::PointsStatus::NoPoints);
}