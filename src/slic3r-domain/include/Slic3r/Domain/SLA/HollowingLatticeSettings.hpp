#pragma once

#include "Slic3r/Domain/ConfigDefsSLA.hpp"

#include <string>
#include <vector>

namespace Slic3r::Domain::SLA {

/// The hollowing infill settings the given infill pattern reads, in the order they should be
/// shown: hollowing_infill itself, then the two knobs that cut the lattice out of the cavity. A
/// pattern of None leaves the cavity empty, so it reads neither knob and the list is the pattern
/// alone.
/// @param infill The selected hollowing infill pattern.
/// @return Config keys of the settings that apply to this hollowing infill pattern.
const std::vector<std::string>& hollowing_infill_visible_settings(sla::HollowingInfillType infill);

/// Whether the named config key is one of the settings the given hollowing infill pattern reads.
/// A key of another panel is not a hollowing infill setting, so it is not one this answers for.
/// @param infill The selected hollowing infill pattern.
/// @param key Config key of an object setting.
/// @return true if the key applies to this hollowing infill pattern.
bool hollowing_infill_uses_setting(sla::HollowingInfillType infill, const std::string& key);

/// Whether the named config key is a hollowing infill setting for any pattern. Keys that are not
/// are never filtered by the pattern, the settings UI shows them whatever the pattern is.
/// @param key Config key of an object setting.
/// @return true if some hollowing infill pattern reads the key.
bool is_hollowing_infill_setting(const std::string& key);

} // namespace Slic3r::Domain::SLA
