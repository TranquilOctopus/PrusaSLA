#ifndef SLA_SUPPORTROLES_HPP
#define SLA_SUPPORTROLES_HPP

#include "libslic3r/SLA/SupportPointGenerator.hpp" // Layers, ThrowOnCancel

#include "Slic3r/Domain/SLA/SupportPoint.hpp"

#include <cstddef>

namespace Slic3r {
class AABBMesh;
} // namespace Slic3r

namespace Slic3r::sla {

/// The numbers the roles are decided by (M7.8.2). They are named values and not config options:
/// the support rulebook fixes them (R4.1, R4.3 - R4.5 of doc/sla-fork/supports/rulebook.md) and R1.2
/// says the same rules for every resin, printer and layer height.
struct SupportRoleThresholds
{
    // R4.3: an island whose area on the layer it starts on is below this is a small island, and its
    // support is light instead of medium.
    double small_island_area_mm2 = 1.0;
    // R4.4: a feature thinner or narrower than this is fragile, and its support takes the minimum
    // tip, so that removing it does not snap the feature off. A ray cast into the model measures
    // the thickness, the footprint of the layer part measures how narrow the feature is.
    double thin_feature_mm = 1.2;
    // R4.1: the lowest point of the object carries the heavy anchor early in the print, and every
    // island point within this many layers of it is one of those anchors.
    double anchor_layers = 2.0;
    // R4.5: small surface detail, a rivet or a stud, stands out of the plane of the surface around
    // it by at least this much ...
    double detail_bulge_mm = 0.15;
    // ... and a point that sits on one moves this far at the most to get off it.
    double detail_move_radius_mm = 1.0;
    // How far the surface may be from the plane it was fitted to around a point before the point
    // is on a curved surface rather than on a flat one with a bump on it. A curve changes by this
    // much over the neighbourhood, so it does not read as a bump of the size above.
    double detail_flatness_mm = 0.1;
    // How many layers above a point the width of the part is looked at to tell the tip of a spike
    // from the flat top of a thin pillar.
    size_t spike_layers = 2;
};

/// Puts the role of every generated point on it, which is what the tip class of its support is
/// picked from in the tool (M7.8.2, the rulebook R4.1 and R4.3 - R4.6):
///
/// - Anchor:      the island points of the lowest island of the object, the ones within
/// `anchor_layers` of its lowest point (R4.1);
/// - Island:      the other island points, whose island is big enough (R4.3);
/// - SmallIsland: the other island points whose island is not (R4.3);
/// - Fragile:     the points on a thin feature (R4.4), and the points on small surface detail
/// that have nowhere plain to move to (R4.5);
/// - Overhang:    everything else (R4.6).
///
/// R4.5 comes first, because a point that sits on a rivet moves to the plain surface next to it
/// and is then classified there; the role it ends up with is the one of the spot it holds.
///
/// @param points The generated points, in the frame of @p mesh. Their roles are written on them,
/// and a point that R4.5 moves is moved on the surface, still in that frame.
/// @param mesh The model the points are on.
/// @param layers The layers the generator sampled, which say what is under a point on its layer:
/// which island or which overhang part it belongs to, how big that part is and how wide it is.
/// @param layer_height The layer height the object is sliced with, in mm.
void classify_support_point_roles(
    Domain::SLA::SupportPoints& points,
    const AABBMesh& mesh,
    const Layers& layers,
    double layer_height,
    const SupportRoleThresholds& thresholds = {},
    ThrowOnCancel throw_on_cancel           = &detail::generator_no_throw_on_cancel
);

} // namespace Slic3r::sla

#endif // SLA_SUPPORTROLES_HPP
