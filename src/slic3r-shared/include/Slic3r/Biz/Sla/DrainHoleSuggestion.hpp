#pragma once

#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/SLA/DrainHole.hpp"
#include "Slic3r/Domain/Transformation.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "libslic3r/SLAResult.hpp"

#include <optional>
#include <vector>

namespace Slic3r {
class AABBMesh;
} // namespace Slic3r

namespace Slic3r::Biz::Sla {

/// The size a drain hole gets when the hollow tool starts a new one (SlaHollowGizmo). A suggestion
/// uses them, so an accepted suggestion is the hole a click in the tool would have added.
inline constexpr double drain_hole_default_radius_mm = 5.;
inline constexpr double drain_hole_default_height_mm = 10.;

struct DrainHoleSuggestionOptions
{
    double radius_mm = drain_hole_default_radius_mm;
    double height_mm = drain_hole_default_height_mm;
};

/// One model a suggestion can be placed on: the mesh in the coordinates the drain holes of that
/// model object are stored in, and the transformation of one of its instances into the frame of
/// the sliced layers (the build plate). @p mesh has to stay alive for the duration of the call,
/// the raycaster of the suggestion only borrows it.
struct DrainHoleCandidate
{
    Domain::ObjectID object_id{};
    const Domain::TriangleMesh* mesh = nullptr;
    Domain::Transform3d           mesh_to_world = Domain::Transform3d::Identity();
};

/// One drain hole suggested for a cup or a trapped resin pocket of a slice.
struct DrainHoleSuggestion
{
    Domain::ObjectID object_id{};
    /// On the surface of the model, in the coordinates of the mesh of the candidate above.
    Domain::Vec3d position_mm = Domain::Vec3d::Zero();
    /// Into the material: the hole is cut as a cylinder that starts at position_mm and runs along
    /// the normal for height_mm (sla::to_mesh), so a normal pointing out of the model would cut
    /// nothing. This is the opposite of the facet normal the raycast returns on an inner wall.
    Domain::Vec3d normal = Domain::Vec3d::UnitZ();
    double radius_mm = drain_hole_default_radius_mm;
    double height_mm = drain_hole_default_height_mm;

    /// The hole to store in ModelObject::sla_drain_holes.
    Domain::SLA::DrainHole to_drain_hole() const;
};

/// Can this kind of issue be answered with a drain hole? Only the two cavities can: an island
/// needs a support, not a hole.
bool accepts_drain_hole_suggestion(Slicing::Sla::SlaIssue::Kind kind);

/**
 * @brief Suggest one drain hole for a cup or a trapped resin pocket of a slice.
 *
 * @param mesh The model of one instance, in the coordinates the drain holes are stored in.
 * @param mesh_to_world The transformation of that instance into the frame of the sliced layers.
 * @param kind Cup or TrappedResin, the kinds accepts_drain_hole_suggestion() knows.
 * @param slice_position_mm Where the issue was found: the footprint centre of the cavity in mm
 *        and, in Z, the bottom of the lowest layer of its layer range. That is the only place the
 *        slicer reports for a cavity, the layer range is not carried with the issue, so the hole
 *        is placed on the surface this single point maps to and the range is not needed.
 * @return The suggested hole in the coordinates of @p mesh, or nothing when the kind is not a
 *         cavity or the surface of the model is not along the axis of the cavity.
 *
 * Where the hole goes, and why:
 *
 * TRAPPED RESIN is resin in a cavity with no way out, and it sits at the bottom of that cavity,
 * so the hole goes through the floor under it, at the lowest point of the cavity's bottom
 * boundary. Printed the usual way up, the floor is the face the resin can run out of towards the
 * plate, and the resin never has to travel sideways through the print to get there. The search
 * ray therefore starts a layer above the bottom of the cavity and goes down, and the first
 * surface it finds is that floor. The hole is drilled downwards, into it.
 *
 * A CUP is a pocket closed on top and open to the vat, and what it does is pin a vacuum against
 * the film on every peel, so the hole goes through the roof, at the highest point of the closed
 * top, where the air of the pocket can get out. The search ray starts at the bottom of the cup
 * and goes up, and the first surface it finds is that roof. The hole is drilled upwards, through
 * it.
 *
 * Both searches run along the axis of the cavity, so the surface they find is always inside the
 * footprint of the cavity and the hole cannot end up on the outside of the model. The layers of a
 * slice are merged over the whole bed, so an issue does not name the model it is in and the
 * surface found may belong to another one; the nearest along the axis wins, see
 * suggest_nearest_drain_hole().
 */
std::optional<DrainHoleSuggestion> suggest_drain_hole(
    const Slic3r::AABBMesh& mesh,
    const Domain::Transform3d& mesh_to_world,
    Slicing::Sla::SlaIssue::Kind kind,
    const Domain::Vec3d& slice_position_mm,
    const DrainHoleSuggestionOptions& opts = {});

/// The suggestion of the candidate whose surface lies nearest to the cavity, so that a cavity
/// inside a model beats another model printed above or below it. Nothing when no candidate has
/// a surface along the axis of the cavity.
std::optional<DrainHoleSuggestion> suggest_nearest_drain_hole(
    const std::vector<DrainHoleCandidate>& candidates,
    Slicing::Sla::SlaIssue::Kind kind,
    const Domain::Vec3d& slice_position_mm,
    const DrainHoleSuggestionOptions& opts = {});

} // namespace Slic3r::Biz::Sla
