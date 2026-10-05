#include "Slic3r/App/Plater/SlaSupportRoles.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace Slic3r::App::Plater {

using Domain::SLA::SupportPoint;

const std::string& rulebook_tip_class(SupportPoint::Role role)
{
    // The names the preset buttons of the tool use (sla_support_preset_name), in the order of
    // SupportPoint::Role. Unknown and Overhang share the light tip on purpose: see the header.
    static const std::array<std::string, 8> classes{
        "light", // Unknown
        "heavy", // Anchor
        "medium", // Island
        "light", // SmallIsland
        "light", // Overhang
        "mini", // Fragile
        "xheavy", // AnchorLarge, the anchors of a very large object (M7.8.3)
        "mini" // Detail (M7.8.5, R4.9)
    };

    // A role the enum does not hold cannot index the table: the last class is the answer rather than
    // a read past the end of it.
    return classes[std::min<std::size_t>(std::size_t(role), classes.size() - 1)];
}

} // namespace Slic3r::App::Plater
