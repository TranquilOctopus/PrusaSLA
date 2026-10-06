#include "Slic3r/App/Plater/SlaSupportRoles.hpp"

#include "Slic3r/App/Plater/SlaSupportAutoPresets.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace Slic3r::App::Plater {

using Domain::sla::SupportAutoDetailPreset;
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

std::optional<std::string>
sla_role_tip_class(SupportPoint::Role role, bool heavy_base, SupportAutoDetailPreset detail)
{
    // No role, no class: a point that was not classified keeps the band rule of M2.37, which is the
    // height of the point and not its role. The one rule every role follows is a fragile feature,
    // which takes the minimum tip whatever the settings ask for (R4.4, R4.5).
    if (role == SupportPoint::Role::Unknown)
        return std::nullopt;
    if (role == SupportPoint::Role::Fragile || role == SupportPoint::Role::Detail)
        return rulebook_tip_class(role);

    // The heavy base is the setting the two anchor roles are about (M2.37): on, the lowest point and
    // the anchors around it carry their own class, off, they take the detail preset like every
    // other generated point.
    if (role == SupportPoint::Role::Anchor || role == SupportPoint::Role::AnchorLarge)
        return heavy_base ? rulebook_tip_class(role) : sla_auto_detail_preset_name(detail);

    // An island keeps the medium tip of R4.3 unless the detail preset is lighter, which is every
    // choice the key offers: a heavier one would leave the medium tip where it is.
    if (role == SupportPoint::Role::Island && detail == SupportAutoDetailPreset::Medium)
        return rulebook_tip_class(role);

    // A small island and an overhang are light by default (R4.3, R4.6), so they take the detail
    // preset whatever it names. A role the enum does not hold ends up here as well, which is the
    // detail class rather than a read past the end of the table of rulebook_tip_class().
    return sla_auto_detail_preset_name(detail);
}

} // namespace Slic3r::App::Plater
