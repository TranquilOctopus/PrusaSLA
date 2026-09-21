#include "Slic3r/Domain/SLA/SupportPoint.hpp"

namespace Slic3r::Domain::SLA {

bool SupportPoint::operator==(const SupportPoint& sp) const
{
    float rdiff = std::abs(head_front_radius - sp.head_front_radius);
    float pdiff = std::abs(pillar_diameter - sp.pillar_diameter);
    float bddiff = std::abs(base_diameter - sp.base_diameter);
    float bhdiff = std::abs(base_height - sp.base_height);
    return (pos == sp.pos) && rdiff < float(EPSILON) && pdiff < float(EPSILON) &&
           bddiff < float(EPSILON) && bhdiff < float(EPSILON) && type == sp.type;
}

bool SupportPoint::operator!=(const SupportPoint& sp) const { return !(sp == (*this)); }
} // namespace Slic3r::Domain::SLA
