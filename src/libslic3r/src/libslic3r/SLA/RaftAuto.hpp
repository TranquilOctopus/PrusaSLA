#pragma once

#include "Slic3r/Domain/ConfigDefsSLA.hpp" // Domain::sla::RaftType
#include "Slic3r/Domain/TriangleMesh.hpp"   // indexed_triangle_set (admesh/stl.h, global scope)
#include "libslic3r/SLA/Pad.hpp"            // ThrowOnCancel

#include <cstddef>
#include <vector>

namespace Slic3r::sla {

// The knobs of the Auto raft rule (rulebook R6, M7.8.4).
struct RaftAutoOptions
{
    /// How much of the underside is read, in mm above the lowest point of the part. A suction cup is
    /// a pocket of the first layers (a hollow sole, a cup standing on its rim, a ring), so only the
    /// bottom of the part is scanned and this bounds it: a pocket deeper than this is not the cup
    /// this rule is about. 1000 mm by default (effectively the full part height, capped by the
    /// part's bounding box), so tall cups and deep soles are detected.
    double scan_height_mm = 1000.;

    /// A pocket whose opening is smaller than this cannot seal against the vat film, so it is not a
    /// cup (it is a drain hole, which is what hollow prints have on purpose). Same value and the
    /// same meaning as the post-slice detection it runs on, see CavityDetectionOptions.
    double min_cup_opening_mm2 = 1.;
};

// What Auto decided about one object.
struct RaftAutoDecision
{
    /// The raft type to build for this object: None for no raft, or the type that removes the
    /// suction the underside would otherwise form. Never Auto, which is a decision and not a shape.
    Domain::sla::RaftType raft_type = Domain::sla::RaftType::None;
    /// Whether the underside would form a suction cup without a raft (R6.2).
    bool suction = false;
    /// The lowest layer the cup is open on, counted from the first layer read. Only meaningful
    /// when suction is true.
    size_t first_cup_layer = 0;
    /// The opening onto the vat of that cup, in mm². Only meaningful when suction is true.
    double opening_area_mm2 = 0.;
};

/**
 * @brief Resolve the raft type Auto for one object (rulebook R6, M7.8.4).
 *
 * R6.1: no raft by default, a raft wastes resin. R6.2: a raft where the underside would form a
 * suction cup, so the pocket of the first layers that stays enclosed until a layer above seals it
 * is looked for. The cup is the one the post-slice detection reports (SLA/CavityDetection.hpp), run
 * on the layers of the underside here, so the rule and the peel force estimate agree on what a cup
 * is.
 *
 * @param mesh_in_print_pose The object as it prints: its transform applied and nothing else, the
 *        same mesh the print is sliced from. It is not lifted, @p object_elevation_mm is the lift.
 * @param layer_height_mm The layer height of the print, in mm. The layers read are the ones of the
 *        first millimetres of the part, all of this thickness.
 * @param object_elevation_mm How high above the build plate the lowest point of the part prints
 *        when Auto builds no raft, which is what the underside is checked in (R6.2 talks about the
 *        pose and the elevation the part prints at). Zero for a part that stands on the plate.
 * @param opts The knobs of the rule.
 * @param throw_on_cancel Checked between layers. Empty means never.
 * @return The raft type to build, and whether the underside would form a suction cup.
 *
 * Which raft removes the suction depends on where the part sits. A part that stands on the plate
 * gets the raft around the object: a full plate raft needs a tree to hang on and there is none
 * (generate_pad() gives up on it, which sla_pad_robustness_tests pins), while the around object one
 * is cut from the part's own footprint and puts resin between the pocket and the vat film. A part
 * held above the plate by its supports gets the full plate raft: it is the slab the pillars stand
 * on, and its top skin is what the pocket opens onto instead of the film.
 */
RaftAutoDecision auto_raft_decision(const indexed_triangle_set& mesh_in_print_pose,
                                    double                     layer_height_mm,
                                    double                     object_elevation_mm,
                                    const RaftAutoOptions&     opts           = {},
                                    const ThrowOnCancel&       throw_on_cancel = {});

} // namespace Slic3r::sla