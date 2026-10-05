#ifndef SLA_SUPPORTANCHORS_HPP
#define SLA_SUPPORTANCHORS_HPP

#include "libslic3r/SLA/SupportPointGenerator.hpp" // ThrowOnCancel, detail::generator_no_throw_on_cancel

#include "Slic3r/Domain/SLA/SupportPoint.hpp"

#include <cstddef>

namespace Slic3r {
class AABBMesh;
} // namespace Slic3r

namespace Slic3r::sla {

/// The numbers the heavy anchors of the support rulebook R4.2 are placed with (M7.8.3,
/// doc/sla-fork/supports/rulebook.md). They are named values and not config options: the rulebook
/// fixes them, and R1.2 says the same rules for every resin, printer and layer height.
struct SupportAnchorThresholds
{
    // R4.2 "a few T0.4 anchors ... about two on a small miniature, more with size and weight". A
    // footprint that fits in this square takes this many anchors, and every area of footprint above
    // it takes one more per footprint_per_extra_anchor_mm2, never more than max_anchors.
    double small_footprint_mm             = 30.0;
    size_t small_footprint_anchors        = 2;
    double footprint_per_extra_anchor_mm2 = 400.0;
    size_t max_anchors                    = 8;

    // The size table of the rulebook: the anchors of a very large object take the largest tip
    // (T0.6) instead of the heavy one, and "very large" is a footprint that fills a mid-size
    // printer's plate. Both of its sides have to be above this.
    double large_footprint_mm = 150.0;

    // R4.2 "on flat, low-detail areas facing the plate". A spot is a candidate when the normal of
    // the surface under it still points at the plate that far down ...
    double down_facing_normal_z = -0.5;

    // ... and the surface around it is flat, which is what the rays this pass casts ask: every ray
    // that leaves the model from under a spot around this one has to come out in the plane the spot
    // is on, and this is how far off that plane it may come out. It is the tolerance M7.8.2 uses to
    // tell a flat surface with a bump on it from a curve, and over the 3 mm neighbourhood below it is
    // a surface that bends by less than two degrees. The rays ask the surface and not the facets, so a
    // stud a millimetre across is found whatever the mesh is cut into, and a finely tessellated flat
    // surface is not mistaken for detail.
    double detail_flatness_mm = 0.1;

    // R4.4 and R4.9: a heavy anchor never stands on a thin, fragile feature, the same measure
    // M7.8.2 uses for R4.4. The lowest-point anchor of R4.1 is the one exception the rulebook makes,
    // and that one is not placed by this pass.
    double thin_feature_mm = 1.2;

    // How wide around a spot the surface is probed is a constant of the pass and not an option: the
    // rulebook fixes the neighbourhood a spot has to be flat over at about three millimetres.

    // The grid the candidate spots are looked for on, in mm. A spot is the centre of one of its
    // cells, so two spots are never closer than this, which is also the distance an added anchor
    // keeps from every other point of the model, and the distance the spot is measured from a point of
    // the generator that is to become the anchor rather than a new point next to it.
    //
    // The grid of a very large object is wider than this, and never narrower: a metre-wide plate has
    // a hundred thousand cells of three millimetres on it, and the spots of a footprint are spread
    // over it as widely as the cap of spots below allows.
    double spot_spacing_mm   = 3.0;
    double promote_radius_mm = 4.0;
    // How many candidate spots the pass looks for at most, and so how wide the grid of a footprint
    // is allowed to become. Anchors of R4.2 are a handful, and the spots are only the places they
    // could stand, so looking for more of them than this costs time and tells nothing.
    size_t max_candidate_spots = 4096;
    // How far the flat, low-detail surface around a spot is measured over, in mm: it is measured by
    // the spots that are near the spot, so a spot beside a bump, a step or the rim of the model has
    // less of it around, because the places beside it are not spots at all.
    double spot_area_radius_mm = 6.0;
    // How far around an anchor that has just been placed the spots are pushed back, in mm. Twice the
    // spacing, so that the next anchor is not tried on the same spot but beside it.
    double spread_radius_mm = 6.0;

    // R4.1 and R4.2: an anchor holds the print early, so it is looked for in the lower part of the
    // model. This much of its height at the very least, and never less than this many millimetres,
    // so that a flat plate and a small miniature are both in their own lower part.
    double lower_band_ratio  = 0.25;
    double min_lower_band_mm = 6.0;
};

/// What one run of the heavy anchors did to a model, which is what the support tool and the tests
/// read to see what R4.2 asked for and what it got.
struct AnchorPlacement
{
    // How many heavy anchors the model has from this rule, of which ...
    size_t placed = 0;
    // ... this many are points the generator had already made, and turned into anchors, and this
    // many are points this pass added.
    size_t promoted = 0;
    size_t added    = 0;
    // The spots the flat, low-detail, plate-facing surface of the model offered, and how many of
    // them R4.2 asks for.
    size_t candidates = 0;
    size_t wanted     = 0;
    // The footprint of the model in the x-y plane, in mm2, which is what the count is decided by,
    // and whether it is above the size at which the anchors take the largest tip (T0.6).
    double footprint_mm2 = 0.;
    bool large           = false;

    /// Whether the model has at least one heavy anchor from this rule.
    bool any() const
    {
        return placed > 0;
    }
};

/// Puts the heavy anchors of the rulebook R4.2 on a model: a few points of the Anchor role on the
/// flat, low-detail areas of the surface that faces the build plate, more of them the bigger the
/// footprint of the model is, and the largest tip (T0.6, the AnchorLarge role) on a very large one.
///
/// The spots the anchors are placed on are the flat, low-detail, plate-facing surface of the model,
/// ranked by the flat area around the spot, by how low the spot is on the model and by how far it
/// is from the anchors the model already has. A spot that a point of the generator already stands
/// on turns that point into the anchor rather than adding a new one next to it, and a spot with
/// nothing on it gets a new point, at least `spot_spacing_mm` away from every other point of the
/// model.
///
/// It runs after classify_support_point_roles (M7.8.2), which is what tells a point from a previous
/// rule apart from one this pass adds, and before the points leave the frame the model is in.
///
/// @param points The points of the model in the frame @p mesh is in, with the roles of M7.8.2 on
/// them. Points this pass adds are appended to it.
/// @param mesh The model, in the frame its points are in.
/// @param head_front_radius The head radius of a point this pass adds, which is the one the
/// generator gave its own points.
/// @param thresholds The numbers of R4.2, see above.
AnchorPlacement add_heavy_anchors(
    Domain::SLA::SupportPoints& points,
    const AABBMesh& mesh,
    const SupportAnchorThresholds& thresholds = {},
    float head_front_radius                   = 0.1f,
    ThrowOnCancel throw_on_cancel             = &detail::generator_no_throw_on_cancel
);

} // namespace Slic3r::sla

#endif // SLA_SUPPORTANCHORS_HPP
