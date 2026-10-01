// M2.6b: undo and redo for the edits batch m15 added. Every one of them has to come back exactly as
// it was, in one undo step. The undo stack of this app keeps whole models, so what undo and redo
// restore is what serialize_model / load_serialized_model carry (the stack of M2.6), and the edits
// are the ones the support tool makes: SlaSupportPointsEditing writes the per-point geometry on the
// points that are selected, and the model of one object carries the per-object overrides.
//
// The tests below play the parts the gizmo plays: a snapshot before the edit, the edit, then the
// snapshot after it. Undo is loading the first snapshot back, redo is loading the second one.
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

#include "Slic3r/App/Plater/SlaSupportGeometry.hpp"
#include "Slic3r/App/Plater/SlaSupportOnModel.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsEditing.hpp"
#include "Slic3r/App/Undo/ModelSerialize.hpp"
#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/SLA/DrainHole.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/Types.hpp"

using Slic3r::App::Plater::SlaSupportGeometry;
using Slic3r::App::Plater::SlaSupportPointsEditing;
using Slic3r::App::Plater::SupportGeometryField;
using Slic3r::App::Plater::SupportOnModel;
using Slic3r::App::Undo::load_serialized_model;
using Slic3r::App::Undo::serialize_model;
using Slic3r::App::Undo::SerializedData;
using Slic3r::Domain::Model;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::SLA::DrainHole;
using Slic3r::Domain::SLA::PointsStatus;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPoints;
using Slic3r::Domain::SLA::SupportPointType;

namespace {

/// One support point carrying every value the per-point controls of M2.16c, M2.23b, M2.24 and
/// M2.26 write, so a round trip that loses one of them shows up here.
SupportPoint make_point()
{
    SupportPoint point;
    point.pos               = Vec3f{1.f, 2.f, 3.f};
    point.head_front_radius = 0.3f;
    point.type              = SupportPointType::manual_add;
    point.pillar_diameter   = 1.6f;
    point.base_diameter     = 4.2f;
    point.base_height       = 0.8f;
    point.base_shape        = SupportPoint::BaseShape::Flat;
    point.tip_shape         = SupportPoint::TipShape::Ball;
    point.tip_length        = 1.4f;
    point.contact_depth     = 0.2f;
    point.stem_sides        = 6;
    point.stem_taper        = 0.35f;
    point.knot_radius       = 0.45f;
    point.on_model          = SupportPoint::OnModel::Forbid;
    return point;
}

/// A model with one object holding the given points, as the support tool finds it when it opens.
Model make_model(const SupportPoints& points)
{
    Model model;
    ModelObject* object        = model.add_object();
    object->name               = "TestObject";
    object->sla_support_points = points;
    object->sla_points_status  = PointsStatus::UserModified;
    return model;
}

const SupportPoint& point_of(const Model& model)
{
    REQUIRE(model.objects.size() == 1u);
    const ModelObject* object = model.objects.front();
    REQUIRE(object->sla_support_points.size() == 1u);
    return object->sla_support_points.front();
}

/// Writes the points of the editing session on the model, the way the tool's
/// commit_edited_points_live does after every edit.
void commit(Model& model, const SlaSupportPointsEditing& editing)
{
    ModelObject* object        = model.objects.front();
    object->sla_support_points = editing.points;
    object->sla_points_status  = PointsStatus::UserModified;
}

/// Every field of a point, so two points are compared as a whole instead of field by field.
void check_same_point(const SupportPoint& lhs, const SupportPoint& rhs)
{
    CHECK(lhs.pos == rhs.pos);
    CHECK(lhs.head_front_radius == rhs.head_front_radius);
    CHECK(lhs.type == rhs.type);
    CHECK(lhs.pillar_diameter == rhs.pillar_diameter);
    CHECK(lhs.base_diameter == rhs.base_diameter);
    CHECK(lhs.base_height == rhs.base_height);
    CHECK(lhs.base_shape == rhs.base_shape);
    CHECK(lhs.tip_shape == rhs.tip_shape);
    CHECK(lhs.tip_length == rhs.tip_length);
    CHECK(lhs.contact_depth == rhs.contact_depth);
    CHECK(lhs.stem_sides == rhs.stem_sides);
    CHECK(lhs.stem_taper == rhs.stem_taper);
    CHECK(lhs.knot_radius == rhs.knot_radius);
    CHECK(lhs.on_model == rhs.on_model);
}

/// The undo stack as the tool uses it: a snapshot of the state before a change, then a snapshot of
/// the state after it. Index 0 is what undo brings back, index 1 what redo brings back.
class UndoStack
{
public:
    void push(const Model& model)
    {
        SerializedData empty_previous;
        m_snapshots.push_back(serialize_model(model, empty_previous));
    }

    std::size_t size() const
    {
        return m_snapshots.size();
    }

    Model load(std::size_t index) const
    {
        return load_serialized_model(m_snapshots.at(index));
    }

private:
    std::vector<SerializedData> m_snapshots;
};

/// An editing session over the points of the object, with the first point selected, the way the tool
/// has it when the user picks a field.
SlaSupportPointsEditing editing_session(const Model& model)
{
    SlaSupportPointsEditing editing;
    editing.points                 = model.objects.front()->sla_support_points;
    editing.selected_point_indices = {0};
    return editing;
}

} // namespace

TEST_CASE("Every per-point field of a support point survives an undo", "[SlaUndo][SLA][undo]")
{
    const Model original = make_model({make_point()});

    SerializedData empty_previous;
    const Model recovered = load_serialized_model(serialize_model(original, empty_previous));

    check_same_point(point_of(recovered), make_point());
}

TEST_CASE("Undo and redo cover the per-point fields the support tool edits", "[SlaUndo][SLA][undo]")
{
    // The values the fields are left at by an edit, so undo brings back a point that carries the
    // old ones and redo brings back the new ones.
    SlaSupportGeometry edited_geometry;
    edited_geometry.tip_diameter_mm  = 0.6;
    edited_geometry.tip_shape        = SupportPoint::TipShape::Cone;
    edited_geometry.tip_length_mm    = 1.1;
    edited_geometry.knot_diameter_mm = 0.5;
    edited_geometry.stem_sides       = 5;
    edited_geometry.stem_taper       = 0.2;
    edited_geometry.base_shape       = SupportPoint::BaseShape::Cylinder;

    struct FieldEdit
    {
        SupportGeometryField field;
        const char* name;
    };

    const FieldEdit field_edits[]{
        {SupportGeometryField::TipDiameter, "tip diameter"},
        {SupportGeometryField::TipShape, "tip shape"},
        {SupportGeometryField::TipLength, "tip length"},
        {SupportGeometryField::KnotDiameter, "knot diameter"},
        {SupportGeometryField::StemSides, "stem sides"},
        {SupportGeometryField::StemTaper, "stem taper"},
        {SupportGeometryField::BaseShape, "foot shape"},
    };

    for (const FieldEdit& edit : field_edits) {
        CAPTURE(edit.name);

        Model model = make_model({make_point()});
        UndoStack stack;
        stack.push(model);

        SlaSupportPointsEditing editing = editing_session(model);
        editing.support_geometry        = edited_geometry;
        editing.apply_support_geometry_to_selected(edit.field);
        commit(model, editing);

        // The one snapshot of the whole drag, whatever the number of ticks of it was.
        stack.push(model);
        REQUIRE(stack.size() == 2u);

        const Model undone = stack.load(0);
        check_same_point(point_of(undone), make_point());

        const Model redone = stack.load(1);
        check_same_point(point_of(redone), point_of(model));
    }
}

TEST_CASE("Undo and redo cover the sizes a support preset writes", "[SlaUndo][SLA][undo]")
{
    // A preset button is one click and it writes the tip diameter and the three sizes at once
    // (SlaSupportPointsGizmo::apply_support_preset).
    Model model = make_model({make_point()});
    UndoStack stack;
    stack.push(model);

    SlaSupportPointsEditing editing          = editing_session(model);
    editing.support_geometry.tip_diameter_mm = 1.2;
    editing.apply_support_geometry_to_selected(SupportGeometryField::TipDiameter);
    editing.pillar_diameter_mm         = 2.4;
    editing.pillar_diameter_use_global = false;
    editing.apply_pillar_diameter_to_selected();
    editing.base_diameter_mm         = 6.6;
    editing.base_diameter_use_global = false;
    editing.apply_base_diameter_to_selected();
    editing.base_height_mm         = 1.4;
    editing.base_height_use_global = false;
    editing.apply_base_height_to_selected();
    commit(model, editing);
    stack.push(model);

    const SupportPoint& preset = model.objects.front()->sla_support_points.front();
    CHECK(preset.head_front_radius == 0.6f);
    CHECK(preset.pillar_diameter == 2.4f);
    CHECK(preset.base_diameter == 6.6f);
    CHECK(preset.base_height == 1.4f);

    check_same_point(point_of(stack.load(0)), make_point());
    check_same_point(point_of(stack.load(1)), preset);
}

TEST_CASE("Undo and redo cover the per-point support on model switch", "[SlaUndo][SLA][undo]")
{
    Model model = make_model({make_point()});
    UndoStack stack;
    stack.push(model);

    SlaSupportPointsEditing editing = editing_session(model);
    editing.apply_support_on_model_to_selected(SupportOnModel::Allow);
    commit(model, editing);
    stack.push(model);

    CHECK(point_of(stack.load(1)).on_model == SupportOnModel::Allow);
    CHECK(point_of(stack.load(0)).on_model == SupportOnModel::Forbid);
}

TEST_CASE("Undo and redo cover a point added on the canvas", "[SlaUndo][SLA][undo]")
{
    Model model = make_model({});
    UndoStack stack;
    stack.push(model);

    SlaSupportPointsEditing editing;
    editing.add_point(Vec3d{5., 6., 7.});
    commit(model, editing);
    stack.push(model);

    const Model& added = stack.load(1);
    REQUIRE(added.objects.front()->sla_support_points.size() == 1u);
    CHECK(added.objects.front()->sla_support_points.front().pos == Vec3f(5.f, 6.f, 7.f));
    // The point takes the geometry the tool was set to, and so does what redo brings back.
    check_same_point(
        added.objects.front()->sla_support_points.front(),
        model.objects.front()->sla_support_points.front()
    );

    // Undo brings back the object as it was, with no point on it.
    CHECK(stack.load(0).objects.front()->sla_support_points.empty());
}

TEST_CASE("Undo and redo cover the drain hole the sidebar issues list adds", "[SlaUndo][SLA][undo]")
{
    Model model = make_model({});
    UndoStack stack;
    stack.push(model);

    DrainHole hole;
    hole.pos    = Vec3f{4.f, 5.f, 6.f};
    hole.normal = Vec3f{0.f, 0.f, 1.f};
    hole.radius = 2.5f;
    hole.height = 4.f;
    model.objects.front()->sla_drain_holes.push_back(hole);
    stack.push(model);

    const Model& with_hole = stack.load(1);
    REQUIRE(with_hole.objects.front()->sla_drain_holes.size() == 1u);
    CHECK(with_hole.objects.front()->sla_drain_holes.front().pos == hole.pos);
    CHECK(with_hole.objects.front()->sla_drain_holes.front().radius == hole.radius);

    CHECK(stack.load(0).objects.front()->sla_drain_holes.empty());
}

TEST_CASE("Undo and redo cover the per-object raft and support overrides", "[SlaUndo][SLA][undo]")
{
    // The object override panel writes these on the model of one object: a raft thickness, a raft
    // edge taper, the stem cross-section and the support point density.
    Model model = make_model({make_point()});
    UndoStack stack;
    stack.push(model);

    Slic3r::Domain::SLAObjectSettings& settings = model.objects.front()->object_settings_sla;
    settings.overrides.set("raft_floor_thickness", 1.5);
    settings.overrides.set("raft_edge_taper", 2.5);
    settings.overrides.set("support_stem_sides", 8);
    settings.overrides.set("support_points_density_relative", 120);
    stack.push(model);

    const Model after = stack.load(1);
    const Slic3r::Domain::SLAObjectSettings& edited = after.objects.front()->object_settings_sla;
    CHECK(edited.overrides.get("raft_floor_thickness")->get<double>() == 1.5);
    CHECK(edited.overrides.get("raft_edge_taper")->get<double>() == 2.5);
    CHECK(edited.overrides.get("support_stem_sides")->get<int>() == 8);
    CHECK(edited.overrides.get("support_points_density_relative")->get<int>() == 120);

    // Undo brings back an object with none of them overridden.
    const Model before = stack.load(0);
    CHECK(before.objects.front()->object_settings_sla.overrides.empty());
}

TEST_CASE("Undo and redo cover the points auto support replaced", "[SlaUndo][SLA][undo]")
{
    // "Auto support selected" and "Auto support all" replace the points of every model of their run
    // under the one snapshot the run takes before the first of them is touched.
    Model model                              = make_model({make_point()});
    model.objects.front()->sla_points_status = PointsStatus::AutoGenerated;
    UndoStack stack;
    stack.push(model);

    SupportPoints generated{make_point(), make_point()};
    generated[1].pos                          = Vec3f{9.f, 9.f, 9.f};
    model.objects.front()->sla_support_points = generated;
    model.objects.front()->sla_points_status  = PointsStatus::AutoGenerated;
    stack.push(model);

    const Model& after = stack.load(1);
    REQUIRE(after.objects.front()->sla_support_points.size() == 2u);
    CHECK(after.objects.front()->sla_points_status == PointsStatus::AutoGenerated);

    const Model& undone = stack.load(0);
    REQUIRE(undone.objects.front()->sla_support_points.size() == 1u);
    check_same_point(point_of(undone), make_point());
}
