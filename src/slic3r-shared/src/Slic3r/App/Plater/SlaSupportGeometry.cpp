#include "Slic3r/App/Plater/SlaSupportGeometry.hpp"

#include <algorithm>
#include <cstdint>

namespace Slic3r::App::Plater {

using Domain::SLA::SupportPoint;
using Domain::SLA::SupportPoints;

SlaSupportGeometry support_geometry_of(const SupportPoint& point)
{
    SlaSupportGeometry geometry;
    // The tool works in diameters, the point stores the radii of the tip and of the knot.
    geometry.tip_diameter_mm  = 2. * static_cast<double>(point.head_front_radius);
    geometry.tip_shape        = point.tip_shape;
    geometry.tip_length_mm    = point.tip_length;
    geometry.knot_diameter_mm = 2. * static_cast<double>(point.knot_radius);
    geometry.stem_sides       = point.stem_sides;
    geometry.stem_taper       = point.stem_taper;
    geometry.base_shape       = point.base_shape;
    return geometry;
}

void apply_support_geometry(
    SupportPoint& point,
    const SlaSupportGeometry& geometry,
    SupportGeometryField field
)
{
    switch (field) {
    case SupportGeometryField::TipDiameter:
        point.head_front_radius = static_cast<float>(geometry.tip_diameter_mm / 2.);
        break;
    case SupportGeometryField::TipShape:
        point.tip_shape = geometry.tip_shape;
        break;
    case SupportGeometryField::TipLength:
        point.tip_length = static_cast<float>(geometry.tip_length_mm);
        break;
    case SupportGeometryField::KnotDiameter:
        // The tool works in diameters, the point stores the radius of the knot.
        point.knot_radius = static_cast<float>(geometry.knot_diameter_mm / 2.);
        break;
    case SupportGeometryField::StemSides:
        point.stem_sides = static_cast<uint8_t>(std::clamp(geometry.stem_sides, 0, 255));
        break;
    case SupportGeometryField::StemTaper:
        point.stem_taper = static_cast<float>(geometry.stem_taper);
        break;
    case SupportGeometryField::BaseShape:
        point.base_shape = geometry.base_shape;
        break;
    }
}

void apply_support_geometry(SupportPoint& point, const SlaSupportGeometry& geometry)
{
    // The tip diameter is not among them: the generator fills the head radius from the tree type
    // it was asked for, and a point placed by hand takes the tip diameter of the tool, which is the
    // one this geometry carries (see SlaSupportPointsEditing::add_point).
    for (const SupportGeometryField field :
         {SupportGeometryField::TipShape,
          SupportGeometryField::TipLength,
          SupportGeometryField::KnotDiameter,
          SupportGeometryField::StemSides,
          SupportGeometryField::StemTaper,
          SupportGeometryField::BaseShape}) {
        apply_support_geometry(point, geometry, field);
    }
}

std::optional<SlaSupportGeometry> selection_support_geometry(
    const SupportPoints& points,
    const std::unordered_set<size_t>& selected_point_indices)
{
    std::optional<SlaSupportGeometry> common;
    for (size_t idx : selected_point_indices) {
        if (idx >= points.size()) {
            continue;
        }
        const SlaSupportGeometry geometry = support_geometry_of(points[idx]);
        if (!common.has_value()) {
            common = geometry;
        } else if (*common != geometry) {
            // The points of the selection disagree, so there is no value to show.
            return std::nullopt;
        }
    }
    return common;
}

SupportPoint::TipShape support_tip_shape_of(Domain::sla::SupportTipShape shape)
{
    switch (shape) {
    case Domain::sla::SupportTipShape::Cone:
        return SupportPoint::TipShape::Cone;
    case Domain::sla::SupportTipShape::Ball:
        return SupportPoint::TipShape::Ball;
    case Domain::sla::SupportTipShape::Default:
    default:
        return SupportPoint::TipShape::Default;
    }
}

Domain::sla::SupportTipShape config_support_tip_shape_of(SupportPoint::TipShape shape)
{
    switch (shape) {
    case SupportPoint::TipShape::Cone:
        return Domain::sla::SupportTipShape::Cone;
    case SupportPoint::TipShape::Ball:
        return Domain::sla::SupportTipShape::Ball;
    case SupportPoint::TipShape::Default:
    default:
        return Domain::sla::SupportTipShape::Default;
    }
}

SupportPoint::BaseShape support_base_shape_of(Domain::sla::SupportBaseShape shape)
{
    // The key has no Default: it is the shape every point gets unless it asks for one, so every
    // value of it names a real foot (M2.23).
    switch (shape) {
    case Domain::sla::SupportBaseShape::Cylinder:
        return SupportPoint::BaseShape::Cylinder;
    case Domain::sla::SupportBaseShape::Flat:
        return SupportPoint::BaseShape::Flat;
    case Domain::sla::SupportBaseShape::Cone:
    default:
        return SupportPoint::BaseShape::Cone;
    }
}

} // namespace Slic3r::App::Plater
