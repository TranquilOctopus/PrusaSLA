#include "Slic3r/Domain/SLA/SupportPoint.hpp"

#include <cmath>

namespace Slic3r::Domain::SLA {

// The point is compared field by field, so a change of any dimension the support tree is built
// from counts as a change: the per-point tip and stem geometry (M2.16c) sits next to the sizes.
static bool near(float a, float b) { return std::abs(a - b) < float(EPSILON); }

bool SupportPoint::operator==(const SupportPoint& sp) const
{
    return pos == sp.pos && type == sp.type && tip_shape == sp.tip_shape &&
           base_shape == sp.base_shape && stem_sides == sp.stem_sides &&
           near(head_front_radius, sp.head_front_radius) &&
           near(pillar_diameter, sp.pillar_diameter) && near(base_diameter, sp.base_diameter) &&
           near(base_height, sp.base_height) && near(tip_length, sp.tip_length) &&
           near(contact_depth, sp.contact_depth) && near(stem_taper, sp.stem_taper) &&
           near(knot_radius, sp.knot_radius);
}

bool SupportPoint::operator!=(const SupportPoint& sp) const { return !(sp == (*this)); }
} // namespace Slic3r::Domain::SLA
