// M2.38: the flow of the support points tool, the way a user drives it: select a support and change
// its tip, stem and bracing, remove one support, add one support.
//
// What the tool does with a click and with a value of the "Selected supports" group is one question
// each, asked in SlaSupportPointEdits.hpp and SlaSupportPointsSettings.cpp, and every edit reaches
// the ModelObject through one write (commit_sla_support_point_edits). The tests below play those
// parts against a real ProjectInteractor and a real SceneInteractor: a model on the build plate with
// three support points on it, raised by the lift the tool holds for that object (M2.33), and the
// undo provider of the app, so "one undo step" and "the drawn tree is rebuilt" are asked of the
// project and not of a copy of the points.
//
// What is NOT driven here, and cannot be: the camera, the raycast against the drawn model and the
// marker glyphs. Those need a Scene and a Render::Device, i.e. an OpenGL context, which is why the
// gizmo tests of M2.33 to M2.35 play the picking (SlaSupportPointPickTests), the lift
// (SlaSupportPointsGizmoTests) and the raycast on their own. What a click is asked about here is
// what a user clicks: a support (by the index the pick of M2.35 found), the drawn model at a
// position, or neither.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Slic3r/App/Plater/SlaSupportPointEdits.hpp"
#include "Slic3r/App/Plater/SlaSupportPointPick.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsEditing.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsLift.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsSettings.hpp"
#include "Slic3r/App/Plater/SlaSupportPreviewService.hpp"
#include "Slic3r/App/Plater/SlaSupportToolShortcuts.hpp"
#include "Slic3r/App/Plater/ThumbnailImageGenerator.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/App/Undo/ModelSerialize.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/IUndoProvider.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/Scene/Selection.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/Workbench.hpp"
#include "Slic3r/TestUtils/AppInstanceMessageHandlerScope.hpp"
#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/ScopedThreadDispatcher.hpp"
#include "Slic3r/TestUtils/TestData.hpp"

using Slic3r::App::Plater::commit_sla_support_point_edits;
using Slic3r::App::Plater::make_sla_support_preview_key;
using Slic3r::App::Plater::selection_support_view;
using Slic3r::App::Plater::sla_new_support_setting_changed;
using Slic3r::App::Plater::sla_selected_support_setting_changed;
using Slic3r::App::Plater::sla_support_click_action;
using Slic3r::App::Plater::sla_support_point_click_target;
using Slic3r::App::Plater::sla_support_point_field_value;
using Slic3r::App::Plater::sla_support_points_drawing_trafo;
using Slic3r::App::Plater::sla_support_points_lift;
using Slic3r::App::Plater::SlaSupportClick;
using Slic3r::App::Plater::SlaSupportClickAction;
using Slic3r::App::Plater::SlaSupportClickButton;
using Slic3r::App::Plater::SlaSupportClickModifier;
using Slic3r::App::Plater::SlaSupportClickResult;
using Slic3r::App::Plater::SlaSupportPointField;
using Slic3r::App::Plater::SlaSupportPointMarker;
using Slic3r::App::Plater::SlaSupportPointsEditing;
using Slic3r::App::Plater::SlaSupportPointsLiftAction;
using Slic3r::App::Plater::SlaSupportPointTarget;
using Slic3r::App::Plater::SlaSupportPreviewKey;
using Slic3r::App::Plater::support_tool_action_for;
using Slic3r::App::Plater::SupportBrace;
using Slic3r::App::Plater::SupportOnModel;
using Slic3r::App::Plater::SupportToolAction;
using Slic3r::App::Plater::SupportToolKeyEvent;
using Slic3r::App::Undo::load_serialized_model;
using Slic3r::App::Undo::serialize_model;
using Slic3r::App::Undo::SerializedData;
using Slic3r::Biz::UndoSnapshotType;

// The code of this file is outside namespace Slic3r, so the namespaces it uses are named here (the
// M2.32 clear tests work around the same trap).
namespace Biz                   = Slic3r::Biz;
namespace Domain                = Slic3r::Domain;
namespace Platform              = Slic3r::App::Platform;
namespace TriMesh               = Slic3r::Biz::Algorithms::TriangleMesh;
namespace UndoSnapshotSelection = Slic3r::Biz::UndoSnapshotSelection;

namespace {

using ModelObject      = Slic3r::Domain::ModelObject;
using PointsStatus     = Slic3r::Domain::SLA::PointsStatus;
using SupportPoint     = Slic3r::Domain::SLA::SupportPoint;
using SupportPoints    = Slic3r::Domain::SLA::SupportPoints;
using SupportPointType = Slic3r::Domain::SLA::SupportPointType;
using Vec3d            = Slic3r::Domain::Vec3d;
using Vec3f            = Slic3r::Domain::Vec3f;
using Vec2d            = Slic3r::Domain::Vec2d;

/// The support elevation of the object of the fixture: the tool raises its object by this while it is
/// open, and the M2.21 support preview lifts it by exactly the same value once it has points.
constexpr double support_elevation = 5.;

/// Three points on the bottom of the 20 mm cube of the fixture, far enough apart that a click on the
/// marker of one is not on the marker of another.
SupportPoints three_points()
{
    SupportPoints points;
    points.push_back(SupportPoint{Vec3f{4.f, 4.f, 0.f}, 0.2f, SupportPointType::manual_add});
    points.push_back(SupportPoint{Vec3f{16.f, 4.f, 0.f}, 0.3f, SupportPointType::manual_add});
    points.push_back(SupportPoint{Vec3f{10.f, 16.f, 0.f}, 0.4f, SupportPointType::manual_add});
    return points;
}

/// One click of the tool, asked the way on_mouse asks it: which button, which modifier, what the pick
/// of M2.35 found under the cursor and, when the ray hit the model, where on it.
SlaSupportClickResult click_on(
    SlaSupportClickButton button,
    SlaSupportClickModifier modifier,
    const SlaSupportPointsEditing& editing,
    const std::optional<SlaSupportPointTarget>& target,
    const std::optional<Vec3d>& surface_pos = std::nullopt
)
{
    SlaSupportClick click;
    click.button   = button;
    click.modifier = modifier;
    return sla_support_click_action(
        click,
        target,
        editing.points,
        editing.lock_island_supports,
        surface_pos
    );
}

/// The undo stack of this app keeps whole models, so what one undo brings back is what
/// serialize_model / load_serialized_model carry. The provider records the snapshot of every action
/// the way the real one takes it, and can load an earlier one back into the project.
struct ModelUndoProvider : Biz::IUndoProvider
{
    explicit ModelUndoProvider(Biz::ProjectInteractor& project_interactor) :
        m_project_interactor(project_interactor)
    {}

    void take_snapshot(UndoSnapshotType type) override
    {
        types.push_back(type);
        SerializedData previous;
        snapshots.push_back(
            serialize_model(m_project_interactor.selected_project().model(), previous)
        );
    }

    void select_snapshot(UndoSnapshotSelection::Variant /*snapshot_variant*/) override {}

    bool is_undo_possible() const override
    {
        return snapshots.size() > 1;
    }

    bool is_redo_possible() const override
    {
        return false;
    }

    /// One undo: the first snapshot of the run, loaded the way the undo of the app loads it.
    void undo()
    {
        REQUIRE(snapshots.size() > 1);
        Domain::Model model = load_serialized_model(snapshots.front());
        model.update_links_bottom_up_recursive();
        m_project_interactor.scene_interactor().set_state(
            m_project_interactor.selected_project_id(),
            std::move(model),
            Biz::Scene::ObjectSelection{}
        );
    }

    std::vector<UndoSnapshotType> types;
    std::vector<SerializedData> snapshots;

private:
    Biz::ProjectInteractor& m_project_interactor;
};

/// A project with one model on the first build plate, three support points on it, and the lift the
/// tool holds for that object while it is open. This is the state the tool finds when a user goes
/// into supporting an object, and the snapshot of it is the state one undo goes back to.
struct SupportPointsFlowFixture
{
    SupportPointsFlowFixture()
    {
        Slic3r::set_data_dir(Tests::get_datadir().string());

        project_interactor.preset_interactor().load_preset_bundle(
            Slic3r::Biz::Preset::IO::BundlePaths::make_test_runtime(Tests::get_datadir())
        );
        project_interactor.new_project();

        undo_provider_storage = std::make_unique<ModelUndoProvider>(project_interactor);
        undo_provider         = undo_provider_storage.get();
        project_interactor.set_undo_provider(std::move(undo_provider_storage));

        scene_interactor.new_object_from_mesh(
            Slic3r::Domain::TriangleMesh{TriMesh::make_cube(20., 20., 20.)}
        );
        ModelObject* model_object = project_interactor.selected_project().model().objects.back();
        model_object->sla_support_points = three_points();
        model_object->sla_points_status  = PointsStatus::AutoGenerated;
        object_id                        = model_object->id().id;

        // The lift: the tool raises its object while it is open, so the raycast and the point glyphs
        // work with the model as it is drawn (M2.33). The M2.21 support preview lifts it by the same
        // value once it has points, so the model is not lifted twice.
        const auto decision = sla_support_points_lift(
            /* tool_open */ true,
            /* scene_lift */ 0.,
            support_elevation,
            /* tool_owns_lift */ false
        );
        REQUIRE(decision.action == SlaSupportPointsLiftAction::Take);
        lift = decision.lift;

        // The instance matrix of the model with that lift: the transform the tree, the glyphs and
        // every pick of the tool go through (M2.33, M2.34, M2.35).
        const Domain::ModelInstance* instance =
            project_interactor.selected_project().find_instance_by_id(object_id);
        REQUIRE(instance != nullptr);
        drawing    = sla_support_points_drawing_trafo(instance->get_matrix(), lift);
        object_ref = Domain::ElementRef{object_id};

        // The snapshot of the state the tool found, which is where one undo goes back to.
        undo_provider->take_snapshot(UndoSnapshotType::InitializeProject);
    }

    ModelObject* model()
    {
        return project_interactor.selected_project().find_object_by_id(object_id);
    }

    const Domain::ModelInstance* instance() const
    {
        return project_interactor.selected_project().find_instance_by_id(object_id);
    }

    /// An edit session over the points the model carries, the way begin_editing opens one.
    SlaSupportPointsEditing session()
    {
        SlaSupportPointsEditing editing;
        editing.points                           = model()->sla_support_points;
        editing.support_geometry.tip_diameter_mm = 0.6;
        editing.pillar_diameter_mm               = 1.2;
        return editing;
    }

    /// The preview key of the object as the M2.21 service computes it, so a test can ask whether an
    /// edit is a new tree or the same one.
    SlaSupportPreviewKey preview_key()
    {
        REQUIRE(instance() != nullptr);
        return make_sla_support_preview_key(
            model()->sla_support_points,
            instance()->get_matrix(),
            0,
            0,
            true
        );
    }

    /// The marker of every point, at the spot the tool draws it. The screen positions are not
    /// projected through a camera here (that needs a Scene and a Render::Device, which is why the
    /// gizmo tests of M2.33 to M2.35 do it on their own): the world position of each point is used
    /// as its screen position, which is distinct per point and keeps the pick of M2.35 the only
    /// thing that decides which support a click is on.
    std::vector<SlaSupportPointMarker> markers_of(const SupportPoints& points) const
    {
        std::vector<SlaSupportPointMarker> markers;
        for (const SupportPoint& point : points) {
            const Vec3d world = drawing * point.pos.cast<double>();
            markers.push_back(SlaSupportPointMarker{Vec2d{world.x(), world.y()}, 2., 300.});
        }
        return markers;
    }

    /// A click on the marker of @p index of @p points, which is what a user does to select a support.
    std::optional<SlaSupportPointTarget>
    pick_marker(const SupportPoints& points, size_t index) const
    {
        const std::vector<SlaSupportPointMarker> markers = markers_of(points);
        REQUIRE(index < markers.size());
        return sla_support_point_click_target(markers, {}, markers[index].screen_pos);
    }

    /// One snapshot of the tool, of the kind every edit of the session takes (M2.6b).
    void snapshot_edit()
    {
        undo_provider->take_snapshot(UndoSnapshotType::SlaSupportPointsEdit);
    }

    /// The one write of the points of the session, which is what every edit of the tool ends with.
    void commit(const SlaSupportPointsEditing& editing)
    {
        commit_sla_support_point_edits(project_interactor, object_ref, editing.points);
    }

    Slic3r::Domain::Workbench workbench;
    Slic3r::App::Platform::StdMainThreadDispatcher dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope{dispatcher};
    Tests::JobManagerScope job_manager_scope{dispatcher};
    Slic3r::App::Plater::ThumbnailImageGenerator thumbnail_image_generator;
    Biz::ProjectInteractor project_interactor{workbench, dispatcher, thumbnail_image_generator};
    Biz::Scene::SceneInteractor& scene_interactor{project_interactor.scene_interactor()};
    Tests::ScopedThreadDispatcher thread_dispatcher{dispatcher};
    std::unique_ptr<ModelUndoProvider> undo_provider_storage;
    ModelUndoProvider* undo_provider{nullptr};
    size_t object_id{0};
    double lift{0.};
    Domain::Transform3d drawing{Domain::Transform3d::Identity()};
    Domain::ElementRef object_ref;
};

} // namespace

// M2.38 (a): clicking the marker of a support selects exactly that support, the "Selected supports"
// group shows what it carries, and changing the tip diameter, the stem diameter or the bracing there
// changes that one support on the model, in one undo step, and is a new tree for the preview.
TEST_CASE_METHOD(
    SupportPointsFlowFixture,
    "selecting a support and changing its values changes that one support",
    "[SlaSupportPointEdits][M2.38]"
)
{
    SlaSupportPointsEditing editing = session();
    REQUIRE(editing.points.size() == 3u);

    SECTION("a click on the marker of a support selects exactly that support")
    {
        const std::optional<SlaSupportPointTarget> target = pick_marker(editing.points, 1);
        REQUIRE(target.has_value());
        REQUIRE(target->index == 1u);

        const SlaSupportClickResult result =
            click_on(SlaSupportClickButton::Left, SlaSupportClickModifier::None, editing, target);
        REQUIRE(result.action == SlaSupportClickAction::SelectPoint);
        REQUIRE(result.point_index.has_value());
        CHECK(*result.point_index == 1u);
        // A drag starts from the marker of a point, so this one may be moved by it.
        CHECK(result.drag_allowed);

        // The tool selects it and the "Selected supports" group shows that one support.
        editing.select_point(*result.point_index);
        const auto view = selection_support_view(editing);
        CHECK(view.count == 1u);
        REQUIRE(view.sizes.has_value());
        CHECK(view.sizes->tip_diameter_mm == Catch::Approx(0.6));
        CHECK(view.brace == SupportBrace::Inherit);
        // The first and the third support are not in the group, so the group holds exactly one.
        CHECK(view.sizes->stem_diameter_mm == Catch::Approx(0.));
    }

    SECTION("changing the tip diameter changes that support only")
    {
        editing.select_point(1);
        const SlaSupportPreviewKey before = preview_key();

        snapshot_edit();
        sla_selected_support_setting_changed(editing, SlaSupportPointField::TipDiameter, 1.4);
        commit(editing);

        const ModelObject* model_object = model();
        REQUIRE(model_object->sla_support_points.size() == 3u);
        CHECK(model_object->sla_support_points[1].head_front_radius == Catch::Approx(0.7));
        CHECK(model_object->sla_support_points[0].head_front_radius == Catch::Approx(0.2));
        CHECK(model_object->sla_support_points[2].head_front_radius == Catch::Approx(0.4));
        // One snapshot for the one change, of the kind an edit of a point takes.
        REQUIRE(undo_provider->types.size() == 2u);
        CHECK(undo_provider->types.back() == UndoSnapshotType::SlaSupportPointsEdit);
        // A new tip is a new tree, so the preview service rebuilds the drawn one.
        CHECK_FALSE(before == preview_key());
    }

    SECTION("changing the stem diameter changes that support only, and one undo brings it back")
    {
        editing.select_point(1);
        const SlaSupportPreviewKey before = preview_key();

        snapshot_edit();
        sla_selected_support_setting_changed(editing, SlaSupportPointField::StemDiameter, 2.4);
        commit(editing);

        const ModelObject* model_object = model();
        REQUIRE(model_object->sla_support_points.size() == 3u);
        CHECK(model_object->sla_support_points[1].pillar_diameter == Catch::Approx(2.4));
        CHECK(model_object->sla_support_points[0].pillar_diameter == Catch::Approx(0.));
        CHECK(model_object->sla_support_points[2].pillar_diameter == Catch::Approx(0.));
        CHECK_FALSE(before == preview_key());

        // One undo brings the support back as it was.
        undo_provider->undo();
        CHECK(model()->sla_support_points[1].pillar_diameter == Catch::Approx(0.));
        CHECK(model()->sla_support_points[1].head_front_radius == Catch::Approx(0.3));
    }

    SECTION("changing the bracing changes that support only, and one undo brings it back")
    {
        editing.select_point(1);
        const SlaSupportPreviewKey before = preview_key();

        snapshot_edit();
        sla_selected_support_setting_changed(
            editing,
            SlaSupportPointField::Bracing,
            sla_support_point_field_value(SupportBrace::Off)
        );
        commit(editing);

        const ModelObject* model_object = model();
        REQUIRE(model_object->sla_support_points.size() == 3u);
        CHECK(model_object->sla_support_points[1].brace == SupportBrace::Off);
        CHECK(model_object->sla_support_points[0].brace == SupportBrace::Inherit);
        CHECK(model_object->sla_support_points[2].brace == SupportBrace::Inherit);
        REQUIRE(undo_provider->types.back() == UndoSnapshotType::SlaSupportPointsEdit);
        // The braces are part of the tree, so taking one away is a new tree.
        CHECK_FALSE(before == preview_key());

        undo_provider->undo();
        CHECK(model()->sla_support_points[1].brace == SupportBrace::Inherit);
    }

    SECTION("a change of the New supports group touches no support that is there")
    {
        editing.select_point(1);

        sla_new_support_setting_changed(
            editing,
            SlaSupportPointField::Bracing,
            sla_support_point_field_value(SupportBrace::On)
        );
        sla_new_support_setting_changed(editing, SlaSupportPointField::StemDiameter, 4.2);
        commit(editing);

        const ModelObject* model_object = model();
        REQUIRE(model_object->sla_support_points.size() == 3u);
        for (const SupportPoint& point : model_object->sla_support_points) {
            CHECK(point.brace == SupportBrace::Inherit);
            CHECK(point.pillar_diameter == Catch::Approx(0.));
        }
        // What the group writes is what the next clicked point takes.
        CHECK(editing.new_support_brace == SupportBrace::On);
        CHECK(editing.pillar_diameter_mm == Catch::Approx(4.2));
    }

    SECTION("three changes of the group are three undo steps")
    {
        editing.select_point(1);

        snapshot_edit();
        sla_selected_support_setting_changed(editing, SlaSupportPointField::TipDiameter, 1.0);
        commit(editing);

        snapshot_edit();
        sla_selected_support_setting_changed(editing, SlaSupportPointField::StemDiameter, 3.0);
        commit(editing);

        snapshot_edit();
        sla_selected_support_setting_changed(
            editing,
            SlaSupportPointField::Bracing,
            sla_support_point_field_value(SupportBrace::On)
        );
        commit(editing);

        REQUIRE(undo_provider->snapshots.size() == 4u);
        const ModelObject* model_object = model();
        REQUIRE(model_object->sla_support_points.size() == 3u);
        CHECK(model_object->sla_support_points[1].head_front_radius == Catch::Approx(0.5));
        CHECK(model_object->sla_support_points[1].pillar_diameter == Catch::Approx(3.0));
        CHECK(model_object->sla_support_points[1].brace == SupportBrace::On);
        CHECK(model_object->sla_support_points[0].brace == SupportBrace::Inherit);
    }

    SECTION("two supports that disagree leave the group without a value to show")
    {
        editing.select_point(0);
        editing.select_point(2, /* add_to_selection */ true);
        sla_selected_support_setting_changed(editing, SlaSupportPointField::StemDiameter, 1.5);
        sla_selected_support_setting_changed(
            editing,
            SlaSupportPointField::Bracing,
            sla_support_point_field_value(SupportBrace::On)
        );

        // The third support is put on bracing Off on its own.
        editing.clear_selection();
        editing.select_point(2);
        sla_selected_support_setting_changed(
            editing,
            SlaSupportPointField::Bracing,
            sla_support_point_field_value(SupportBrace::Off)
        );

        editing.clear_selection();
        editing.select_point(0);
        editing.select_point(2, /* add_to_selection */ true);

        const auto view = selection_support_view(editing);
        CHECK(view.count == 2u);
        // The two selected supports disagree on the bracing, so the field says "Mixed" instead of
        // showing the value only one of them has.
        CHECK_FALSE(view.brace.has_value());
        // They also carry different tip diameters, so the size fields say so as well.
        CHECK_FALSE(view.sizes.has_value());
    }
}

// M2.38 (b): removing one support. A user removes one with the Delete key, with the "Delete" button
// of the "Selected supports" group, or with Ctrl+click or a right click on the marker. All of them go
// through the one action of the tool, so the same support goes and one undo brings it back.
TEST_CASE_METHOD(
    SupportPointsFlowFixture,
    "removing one support takes that support away and one undo brings it back",
    "[SlaSupportPointEdits][M2.38]"
)
{
    SlaSupportPointsEditing editing = session();
    REQUIRE(editing.points.size() == 3u);

    const SupportPoints before = model()->sla_support_points;

    // The click on the marker of the second support, which is what every gesture below is asked
    // about: a click that lands on that marker and nowhere else.
    const std::optional<SlaSupportPointTarget> target = pick_marker(editing.points, 1);
    REQUIRE(target.has_value());
    REQUIRE(target->index == 1u);

    SECTION("the Delete key with the support selected")
    {
        SupportToolKeyEvent key;
        key.code          = Platform::KeyCode::Delete;
        key.tool_active   = true;
        key.has_selection = true;
        editing.select_point(target->index);
        REQUIRE(support_tool_action_for(key) == SupportToolAction::DeleteSelectedPoints);

        snapshot_edit();
        editing.delete_selected_points();
        commit(editing);

        CHECK(editing.points.size() == 2u);
        CHECK(editing.selected_point_indices.empty());
    }

    SECTION("the Delete button of the Selected supports group")
    {
        // The button is on while the group has a selection to remove, which is the count the group
        // is titled and shown with, and off with none.
        editing.select_point(target->index);
        CHECK(selection_support_view(editing).count == 1u);

        snapshot_edit();
        editing.delete_selected_points();
        commit(editing);

        CHECK(selection_support_view(editing).count == 0u);
    }

    SECTION("Ctrl and the right button on the marker")
    {
        const SlaSupportClickResult ctrl =
            click_on(SlaSupportClickButton::Left, SlaSupportClickModifier::Ctrl, editing, target);
        const SlaSupportClickResult right =
            click_on(SlaSupportClickButton::Right, SlaSupportClickModifier::None, editing, target);
        for (const SlaSupportClickResult* result : {&ctrl, &right}) {
            REQUIRE(result->action == SlaSupportClickAction::DeletePoint);
            REQUIRE(result->point_index.has_value());
            CHECK(*result->point_index == 1u);
        }

        snapshot_edit();
        editing.remove_point(target->index);
        commit(editing);

        CHECK(editing.points.size() == 2u);
    }

    // Everything above ends in the same state of the model, so what is checked here is what all three
    // of them leave behind: the one support is gone and the two others are where they were.
    const ModelObject* model_object = model();
    REQUIRE(model_object->sla_support_points.size() == 2u);
    CHECK(model_object->sla_support_points[0] == before[0]);
    CHECK(model_object->sla_support_points[1] == before[2]);
    CHECK(model_object->sla_points_status == PointsStatus::UserModified);
    // One snapshot for the one support that went, on top of the one of the state the tool found.
    REQUIRE(undo_provider->snapshots.size() == 2u);

    // And one undo brings the support back, in place.
    undo_provider->undo();
    const ModelObject* undone = model();
    REQUIRE(undone->sla_support_points.size() == 3u);
    for (size_t i = 0; i < before.size(); ++i) {
        CHECK(undone->sla_support_points[i].pos == before[i].pos);
        CHECK(undone->sla_support_points[i].head_front_radius == before[i].head_front_radius);
    }
}

// M2.38 (c): a click on the drawn model where there is no support adds one there, with the values of
// the "New supports" group, and it is on the model right away: no Apply is needed.
TEST_CASE_METHOD(
    SupportPointsFlowFixture,
    "clicking the model where there is no support adds one there",
    "[SlaSupportPointEdits][M2.38]"
)
{
    SlaSupportPointsEditing editing   = session();
    const SlaSupportPreviewKey before = preview_key();

    // The values of the "New supports" group, as begin_editing fills it and a changed field writes it.
    sla_new_support_setting_changed(editing, SlaSupportPointField::TipDiameter, 0.8);
    sla_new_support_setting_changed(editing, SlaSupportPointField::StemDiameter, 1.4);
    sla_new_support_setting_changed(
        editing,
        SlaSupportPointField::SupportOnModel,
        sla_support_point_field_value(SupportOnModel::Allow)
    );
    sla_new_support_setting_changed(
        editing,
        SlaSupportPointField::Bracing,
        sla_support_point_field_value(SupportBrace::On)
    );

    // A click on the top face of the model as it is drawn, where no support is. The lift moved the
    // model, so the face the user clicked is 5 mm above the plate, and the point that lands there is
    // the surface point in the mesh frame of the object (M2.33).
    const Vec3d world_surface = drawing * Vec3d{10., 10., 20.};
    CHECK(world_surface.z() == Catch::Approx(20. + lift));
    const Vec3d mesh_surface{10., 10., 20.};

    const SlaSupportClickResult result = click_on(
        SlaSupportClickButton::Left,
        SlaSupportClickModifier::None,
        editing,
        std::nullopt,
        mesh_surface
    );
    REQUIRE(result.action == SlaSupportClickAction::AddPoint);
    REQUIRE(result.surface_pos.has_value());
    CHECK(*result.surface_pos == Vec3d{10., 10., 20.});

    snapshot_edit();
    editing.add_point(*result.surface_pos);
    commit(editing);

    // The support is on the ModelObject the moment the click was, with no Apply pressed.
    const ModelObject* model_object = model();
    REQUIRE(model_object->sla_support_points.size() == 4u);
    const SupportPoint& added = model_object->sla_support_points.back();
    CHECK(added.pos == Vec3f{10.f, 10.f, 20.f});
    CHECK(added.head_front_radius == Catch::Approx(0.4));
    CHECK(added.pillar_diameter == Catch::Approx(1.4));
    CHECK(added.on_model == SupportOnModel::Allow);
    CHECK(added.brace == SupportBrace::On);
    CHECK(added.type == SupportPointType::manual_add);
    // And it is a new tree for the preview service.
    CHECK_FALSE(before == preview_key());

    // One undo takes it away again, leaving the three supports the model had.
    undo_provider->undo();
    CHECK(model()->sla_support_points.size() == 3u);
}

// The click of the tool is one question, and the three steps a person takes with the supports of a
// model come out of it. These are the rules of that question, with no scene and no camera in them.
TEST_CASE("what a click of the support points tool asks for", "[SlaSupportPointEdits][M2.38]")
{
    SlaSupportPointsEditing editing;
    editing.points.push_back(SupportPoint{Vec3f{4.f, 4.f, 0.f}, 0.2f, SupportPointType::island});
    editing.points.push_back(
        SupportPoint{Vec3f{16.f, 4.f, 0.f}, 0.3f, SupportPointType::manual_add}
    );
    REQUIRE(editing.points.size() == 2u);

    const std::optional<SlaSupportPointTarget> on_island{SlaSupportPointTarget{0, true}};
    const std::optional<SlaSupportPointTarget> on_plain{SlaSupportPointTarget{1, true}};
    const std::optional<SlaSupportPointTarget> on_the_tree{SlaSupportPointTarget{1, false}};
    const std::optional<Vec3d> on_the_model{Vec3d{8., 8., 20.}};

    SECTION("an event that is no button press is not for the tool")
    {
        CHECK(
            click_on(SlaSupportClickButton::None, SlaSupportClickModifier::None, editing, on_plain)
                .action
            == SlaSupportClickAction::None
        );
    }

    SECTION("a click on the drawn tree of a support selects it and starts no drag")
    {
        const SlaSupportClickResult result = click_on(
            SlaSupportClickButton::Left,
            SlaSupportClickModifier::None,
            editing,
            on_the_tree
        );
        CHECK(result.action == SlaSupportClickAction::SelectPoint);
        REQUIRE(result.point_index.has_value());
        CHECK(*result.point_index == 1u);
        // Or a click on a pillar would move a support by accident (M2.35).
        CHECK_FALSE(result.drag_allowed);
    }

    SECTION("Ctrl and the right button remove the support and nothing else")
    {
        const SlaSupportClickResult right = click_on(
            SlaSupportClickButton::Right,
            SlaSupportClickModifier::None,
            editing,
            on_plain
        );
        CHECK(right.action == SlaSupportClickAction::DeletePoint);
        REQUIRE(right.point_index.has_value());
        CHECK(*right.point_index == 1u);

        const SlaSupportClickResult ctrl =
            click_on(SlaSupportClickButton::Left, SlaSupportClickModifier::Ctrl, editing, on_plain);
        CHECK(ctrl.action == SlaSupportClickAction::DeletePoint);
        REQUIRE(ctrl.point_index.has_value());
        CHECK(*ctrl.point_index == 1u);
    }

    SECTION("on empty space Ctrl and the right button are the app's own click")
    {
        CHECK(
            click_on(
                SlaSupportClickButton::Right,
                SlaSupportClickModifier::None,
                editing,
                std::nullopt
            )
                .action
            == SlaSupportClickAction::None
        );
        CHECK(
            click_on(
                SlaSupportClickButton::Left,
                SlaSupportClickModifier::Ctrl,
                editing,
                std::nullopt
            )
                .action
            == SlaSupportClickAction::None
        );
    }

    SECTION("Shift adds a support to the selection or takes it out of it")
    {
        const SlaSupportClickResult result = click_on(
            SlaSupportClickButton::Left,
            SlaSupportClickModifier::Shift,
            editing,
            on_plain
        );
        CHECK(result.action == SlaSupportClickAction::TogglePoint);
        REQUIRE(result.point_index.has_value());
        CHECK(*result.point_index == 1u);
    }

    SECTION("Shift where there is no support starts a rectangle selection")
    {
        CHECK(
            click_on(
                SlaSupportClickButton::Left,
                SlaSupportClickModifier::Shift,
                editing,
                std::nullopt,
                on_the_model
            )
                .action
            == SlaSupportClickAction::RectangleSelect
        );
    }

    SECTION("a click where there is neither a support nor the model drops the selection")
    {
        CHECK(
            click_on(
                SlaSupportClickButton::Left,
                SlaSupportClickModifier::None,
                editing,
                std::nullopt
            )
                .action
            == SlaSupportClickAction::ClearSelection
        );
    }

    SECTION("a locked island support is left alone, and the click is still the tool's")
    {
        editing.lock_island_supports = true;
        for (const SlaSupportClickModifier modifier :
             {SlaSupportClickModifier::None,
              SlaSupportClickModifier::Ctrl,
              SlaSupportClickModifier::Shift})
        {
            INFO("Modifier: " << static_cast<int>(modifier));
            const SlaSupportClickResult result =
                click_on(SlaSupportClickButton::Left, modifier, editing, on_island, on_the_model);
            CHECK(result.action == SlaSupportClickAction::Ignored);
        }

        // A support that is not an island one is picked while the lock is on.
        CHECK(
            click_on(SlaSupportClickButton::Left, SlaSupportClickModifier::None, editing, on_plain)
                .action
            == SlaSupportClickAction::SelectPoint
        );
        // And the model is still there to add a support on.
        CHECK(
            click_on(
                SlaSupportClickButton::Left,
                SlaSupportClickModifier::None,
                editing,
                std::nullopt,
                on_the_model
            )
                .action
            == SlaSupportClickAction::AddPoint
        );
    }
}
