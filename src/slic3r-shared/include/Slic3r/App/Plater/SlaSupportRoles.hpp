#pragma once

#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

#include <optional>
#include <string>

namespace Slic3r::App::Plater {

/// The tip class a support takes from the role of its point (M7.8.2, the support rulebook R4.1 and
/// R4.3 - R4.6 of doc/sla-fork/supports/rulebook.md, the size table of its section 3):
///
/// - Anchor      -> heavy (T0.4): the lowest point of the object and the anchors around it carry
/// the whole part early in the print (R4.1);
/// - Island      -> medium (T0.3): every island's lowest point is supported, and an island that
/// is not small gets the medium tip (R4.3);
/// - SmallIsland -> light (T0.2): a very small island gets the light tip (R4.3);
/// - Overhang    -> light (T0.2): overhangs and the rest are light by default (R4.6);
/// - Fragile     -> mini (T0.1): a tip, a point or another thin feature takes the minimum tip, so
/// that removing the support does not snap the feature off (R4.4, R4.5);
/// - AnchorLarge -> xheavy (T0.6): the anchors of a very large object, one that fills a mid-size
/// printer's plate, take the largest tip (M7.8.3, the size table of section 3 of the rulebook).
///
/// Unknown is what a point placed by hand carries, and what a project written before the roles
/// existed reads back as, so it takes the light tip as well: that is what such a support got before
/// there were roles, and R4.6 asks for light by default.
///
/// The name is one of "mini", "light", "medium", "heavy" and "xheavy", which is what
/// sla_support_preset() reads the sizes of, and what the preset buttons of the tool apply. The
/// classifier of the auto support presets (M2.37) is what asks for it.
const std::string& rulebook_tip_class(Domain::SLA::SupportPoint::Role role);

/// The tip class one generated point takes from its role and the two settings of the automatic
/// placement (M7.8.8), which is what lets the roles of M7.8.2 reach the sizes of the supports:
///
/// - Fragile takes its own class, the minimum tip, whatever the settings ask for: a thick support
/// under a thin feature snaps the feature off when it is removed (R4.4, R4.5). The Detail role of
/// R4.9 joins it when M7.8.5 is in this branch;
/// - Anchor and AnchorLarge take their own class (heavy, and xheavy for the anchors of a very large
/// object) while support_auto_heavy_base is on, and the detail preset when it is off (M2.37): with
/// the heavy base off there is nothing to tell the base of the model apart, so the anchors are
/// like every other generated point;
/// - Overhang and SmallIsland take the detail preset of support_auto_detail_preset, whatever it
/// names (R4.6, R4.3);
/// - Island keeps its own class, the medium tip, unless the detail preset asks for something
/// lighter, which is the whole range the key offers (R4.3). A heavier choice would leave it.
///
/// Nothing at all for Role::Unknown: a point of a project written before the roles existed, or one
/// from a path that does not classify, has no class to take here and keeps the band rule of M2.37
/// (the lowest island heavy, everything else the detail preset), which is what the caller falls
/// back to.
///
/// Pure, like rulebook_tip_class(): a role, two settings in, a preset name out. No model, no
/// config, no scene, and nothing sliced.
std::optional<std::string> sla_role_tip_class(
    Domain::SLA::SupportPoint::Role role,
    bool heavy_base,
    Domain::sla::SupportAutoDetailPreset detail
);

} // namespace Slic3r::App::Plater
