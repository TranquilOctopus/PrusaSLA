#pragma once

#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

#include <optional>
#include <unordered_set>

namespace Slic3r::App::Plater {

/// The per-point support geometry the support tool edits: the tip shape, the knot ball between tip
/// and stem, the stem cross-section and the stem taper. Every value is a default that reproduces
/// the geometry the support tree has always built (TipShape::Default, no knot, a round stem of one
/// diameter), which is what the Supports & raft settings ship with, so a point that carries none
/// of these is an ordinary support.
struct SlaSupportGeometry
{
    Domain::SLA::SupportPoint::TipShape tip_shape{Domain::SLA::SupportPoint::TipShape::Default};
    double knot_diameter_mm{0.}; // [mm] 0 = no knot
    int    stem_sides{0};       // 0 = round, 4 = square, 6 = hexagon, ...
    double stem_taper{0.};      // fraction of the stem diameter lost base -> tip, 0 = no taper

    friend bool operator==(const SlaSupportGeometry& lhs, const SlaSupportGeometry& rhs)
    {
        return lhs.tip_shape == rhs.tip_shape && lhs.knot_diameter_mm == rhs.knot_diameter_mm &&
               lhs.stem_sides == rhs.stem_sides && lhs.stem_taper == rhs.stem_taper;
    }

    friend bool operator!=(const SlaSupportGeometry& lhs, const SlaSupportGeometry& rhs)
    {
        return !(lhs == rhs);
    }
};

/// The one of the four values an edit is about. The support tool writes one value at a time, so
/// setting the tip shape of a point does not overwrite the knot, the cross-section or the taper it
/// already carries.
enum class SupportGeometryField { TipShape, KnotDiameter, StemSides, StemTaper };

/// The geometry @p point carries, in the units the tool shows: the knot is a diameter there, while
/// the point stores its radius.
SlaSupportGeometry support_geometry_of(const Domain::SLA::SupportPoint& point);

/// Writes the one value of @p field from @p geometry onto @p point.
void apply_support_geometry(
    Domain::SLA::SupportPoint& point,
    const SlaSupportGeometry& geometry,
    SupportGeometryField field
);

/// Writes all four values of @p geometry onto @p point, which is what a new point takes.
void apply_support_geometry(Domain::SLA::SupportPoint& point, const SlaSupportGeometry& geometry);

/// The geometry a set of points is shown with, or nullopt when nothing is selected or the selected
/// points disagree on any of the four values. The tool then leaves the field blank rather than
/// showing a number that only some of the selected points have.
std::optional<SlaSupportGeometry> selection_support_geometry(
    const Domain::SLA::SupportPoints& points,
    const std::unordered_set<size_t>& selected_point_indices
);

/// The three tip shapes, as the config stores them and as the point carries them.
Domain::SLA::SupportPoint::TipShape support_tip_shape_of(Domain::sla::SupportTipShape shape);
Domain::sla::SupportTipShape config_support_tip_shape_of(Domain::SLA::SupportPoint::TipShape shape);

} // namespace Slic3r::App::Plater
