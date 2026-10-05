#pragma once

#include "Slic3r/Domain/SLA/SupportPoint.hpp"

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
/// - Detail      -> mini (T0.1): a detailed region, where fine, dense or highly curved surface is,
/// takes the lightest tip whatever the role of the point would have given it, except the Anchor of
/// the lowest island, which R4.9 leaves alone (M7.8.5, R4.9).
///
/// Unknown is what a point placed by hand carries, and what a project written before the roles
/// existed reads back as, so it takes the light tip as well: that is what such a support got before
/// there were roles, and R4.6 asks for light by default.
///
/// The name is one of "mini", "light", "medium", "heavy", which is what sla_support_preset() reads
/// the sizes of, and what the preset buttons of the tool apply. The classifier of the auto support
/// presets (M2.37) is what asks for it.
const std::string& rulebook_tip_class(Domain::SLA::SupportPoint::Role role);

} // namespace Slic3r::App::Plater
