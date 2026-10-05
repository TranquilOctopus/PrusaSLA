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
    // R4.9: how far from a point the surface is looked at to say whether it is in a detailed
    // region, i.e. whether the surface there is fine, dense or highly curved. It is about the size
    // of the features this rule is about: a stud, a rivet, a chain mail of a miniature, the
    // wrinkles of a sculpted face.
    double detail_radius_mm = 1.5;
    // R4.9: how far the surface around a point may leave the plane the point's own normal is
    // tangent to before the point counts as highly curved: 0.2 mm over 1.5 mm is a curvature radius
    // of about 5.6 mm, which is a finger or a bead rather than a plate.
    double detail_sag_mm = 0.2;
    // R4.9: how many highs and lows of its own the surface around a point may go over between the
    // point and that radius before the point counts as fine or dense: two is a surface that comes
    // and goes and comes back inside 1.5 mm, i.e. a texture of well under a millimetre across.
    double detail_turns = 2.0;
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
/// - Overhang:    everything else (R4.6);
/// - Detail:      the points in a detailed region, whatever any of the three above would have made
/// them (R4.9).
///
/// R4.5 comes first, because a point that sits on a rivet moves to the plain surface next to it
/// and is then classified there; the role it ends up with is the one of the spot it holds. R4.9
/// comes last, because it is the one rule that overrides another: a fragile point stays Fragile,
/// which takes the same minimum tip, and the Anchor of the lowest island is never Detail, since
/// that anchor carries the whole part early in the print (R4.1).
///
/// How a detailed region is found (R4.9)
///
/// The surface around a point is sampled on rays, the way R4.5 samples it: the ray starts inside
/// the material beside the spot and comes out at the first surface below it, which is the surface
/// of the model around the point. Every sample says how far from the plane through the point,
/// perpendicular to its own normal, the surface is at that spot (`h`). Two numbers of those samples
/// say whether the region is detailed:
///
/// - `sag`: how far the surface is from that plane on the ring at `detail_radius_mm`, on average
/// over the spots that were valid. A plate and a slope have none of it whatever their mesh is made
/// of, because the plane follows the surface; a curve of radius R has r2 / 2R of it at the radius
/// r, so this is the "highly curved" half of the rule and a bead or a finger is over it while a
/// plain barrel is not;
/// - `turns`: how many highs and lows of its own the surface goes over on the way out from the
/// point to the same radius, averaged over the directions of the walk. A plain surface and a
/// smooth curve only fall away from the plane, so they go over none; relief goes over one per
/// feature, which is the "fine or dense" half of the rule and is what reads a grid of studs, a
/// chain mail and the wrinkles of a sculpted face.
///
/// Both are measured on the surface and not on the mesh, so a model that is finely tessellated but
/// smooth - a scan of a flat face - is not detail, and a model with a handful of big triangles on a
/// sphere is. A region the rays cannot see into - the rim of a silhouette, the open side of a hollow
/// shell, a point over a gap - gives too few samples to say anything and is not detail: the rules
/// that did reach there have already said what the point is.
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
