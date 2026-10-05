#include "Slic3r/Domain/SLA/SupportPoint.hpp"

#include <cmath>

namespace Slic3r::Domain::SLA {

// The point is compared field by field, so a change of any dimension the support tree is built
// from counts as a change: the per-point tip and stem geometry (M2.16c) sits next to the sizes,
// and so do the per-point foot shape (M2.23) and the per-point "may rest on the model" switch,
// which changes where the pillar ends (M2.26), and the per-point bracing switch (M2.38), which
// changes whether the pillar is braced at all. The role of the point (M7.8.2) counts as well: it is
// what the tip class of its support is picked from, so a point that gets another role is another
// support once the tool applies the class.
static bool near(float a, float b) { return std::abs(a - b) < float(EPSILON); }

bool SupportPoint::operator==(const SupportPoint& sp) const
{
    return pos == sp.pos && type == sp.type && tip_shape == sp.tip_shape &&
           base_shape == sp.base_shape && on_model == sp.on_model && brace == sp.brace &&
           role == sp.role && stem_sides == sp.stem_sides &&
           near(head_front_radius, sp.head_front_radius) &&
           near(pillar_diameter, sp.pillar_diameter) && near(base_diameter, sp.base_diameter) &&
           near(base_height, sp.base_height) && near(tip_length, sp.tip_length) &&
           near(contact_depth, sp.contact_depth) && near(stem_taper, sp.stem_taper) &&
           near(knot_radius, sp.knot_radius);
}

bool SupportPoint::operator!=(const SupportPoint& sp) const { return !(sp == (*this)); }
} // namespace Slic3r::Domain::SLA
