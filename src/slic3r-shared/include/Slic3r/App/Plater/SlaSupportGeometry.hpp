#pragma once

#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

#include <optional>
#include <unordered_set>

namespace Slic3r::App::Plater {

/// The per-point support geometry the support tool edits: the tip diameter, the tip shape, the tip
/// length, the knot ball between tip and stem, the stem cross-section and the stem taper. Every
/// value is a default that reproduces the geometry the support tree has always built (the
/// configured pinhead diameter, TipShape::Default, a tip length derived from the pinhead width, no
/// knot, a round stem of one diameter), which is what the Supports & raft settings ship with, so a
/// point that carries none of these is an ordinary support.
struct SlaSupportGeometry
{
    double tip_diameter_mm{0.4}; // [mm] the diameter where the tip touches the model
    Domain::SLA::SupportPoint::TipShape tip_shape{Domain::SLA::SupportPoint::TipShape::Default};
    double tip_length_mm{0.};    // [mm] 0 = the pinhead width, as the tree has always built it
    double knot_diameter_mm{0.}; // [mm] 0 = no knot
    int    stem_sides{0};        // 0 = round, 4 = square, 6 = hexagon, ...
    double stem_taper{0.};       // fraction of the stem diameter lost base -> tip, 0 = no taper

    friend bool operator==(const SlaSupportGeometry& lhs, const SlaSupportGeometry& rhs)
    {
        return lhs.tip_diameter_mm == rhs.tip_diameter_mm && lhs.tip_shape == rhs.tip_shape &&
               lhs.tip_length_mm == rhs.tip_length_mm && lhs.knot_diameter_mm == rhs.knot_diameter_mm &&
               lhs.stem_sides == rhs.stem_sides && lhs.stem_taper == rhs.stem_taper;
    }

    friend bool operator!=(const SlaSupportGeometry& lhs, const SlaSupportGeometry& rhs)
    {
        return !(lhs == rhs);
    }
};

/// The one of the values an edit is about. The support tool writes one value at a time, so setting
/// the tip shape of a point does not overwrite the tip diameter, the tip length, the knot, the
/// cross-section or the taper it already carries.
enum class SupportGeometryField { TipDiameter, TipShape, TipLength, KnotDiameter, StemSides, StemTaper };

/// The geometry @p point carries, in the units the tool shows: the tip and the knot are diameters
/// there, while the point stores their radii.
SlaSupportGeometry support_geometry_of(const Domain::SLA::SupportPoint& point);

/// Writes the one value of @p field from @p geometry onto @p point.
void apply_support_geometry(
    Domain::SLA::SupportPoint& point,
    const SlaSupportGeometry& geometry,
    SupportGeometryField field
);

/// Writes the geometry of @p geometry onto @p point, but not its tip diameter: the generator fills
/// the head radius itself, from the tree type it was asked for, and a point placed by hand takes
/// the tip diameter of the tool (SlaSupportPointsEditing::add_point).
void apply_support_geometry(Domain::SLA::SupportPoint& point, const SlaSupportGeometry& geometry);

/// The geometry a set of points is shown with, or nullopt when nothing is selected or the selected
/// points disagree on any of the values. The tool then leaves the field blank rather than showing a
/// number that only some of the selected points have.
std::optional<SlaSupportGeometry> selection_support_geometry(
    const Domain::SLA::SupportPoints& points,
    const std::unordered_set<size_t>& selected_point_indices
);

/// The three tip shapes, as the config stores them and as the point carries them.
Domain::SLA::SupportPoint::TipShape support_tip_shape_of(Domain::sla::SupportTipShape shape);
Domain::sla::SupportTipShape config_support_tip_shape_of(Domain::SLA::SupportPoint::TipShape shape);

} // namespace Slic3r::App::Plater
