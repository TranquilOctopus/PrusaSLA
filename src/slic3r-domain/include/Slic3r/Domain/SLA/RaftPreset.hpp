#pragma once

#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include <cmath>
#include <string>
#include <vector>

namespace Slic3r::Domain::SLA {

struct RaftPadValues
{
    bool pad_enable              = true;
    bool pad_around_object       = false;
    double pad_wall_height_mm    = 0.0;
    double pad_wall_thickness_mm = 2.0;
    double pad_brim_size_mm      = 1.6;
    double pad_wall_slope_deg    = 90.0;
    double pad_object_gap_mm     = 1.0;
};

/// Map a raft type and shared knobs to the pad configuration values.
/// @param type The raft type preset.
/// @param wall_height_mm User-specified wall height (cavity depth).
/// @param wall_thickness_mm User-specified wall thickness.
/// @param expansion_mm User-specified brim expansion around geometry.
/// @param slope_deg User-specified wall slope in degrees (45-90).
/// @param object_gap_mm Gap between object bottom and pad in zero-elevation mode.
/// @return RaftPadValues to be applied to the pad generator.
RaftPadValues raft_preset_to_pad_values(
    sla::RaftType type,
    double wall_height_mm,
    double wall_thickness_mm,
    double expansion_mm,
    double slope_deg,
    double object_gap_mm
);

/// The raft settings the given raft type actually reads, in the order they should be shown:
/// raft_type itself first, then the knobs that are not already decided by the raft type.
/// raft_type is always in the list. A knob the raft type ignores (Skate replaces the expansion
/// and the wall slope, None prints no raft at all) is not, so the settings UI can hide it.
/// @param type The selected raft type.
/// @return Config keys of the settings that apply to this raft type.
const std::vector<std::string>& raft_type_visible_settings(sla::RaftType type);

/// Whether the named config key is one of the raft settings the given raft type reads.
/// @param type The selected raft type.
/// @param key Config key of a print setting.
/// @return true if the key applies to this raft type, false if it is not a raft setting at all
/// or the raft type decides it on its own.
bool raft_type_uses_setting(sla::RaftType type, const std::string& key);

/// Whether the named config key is a raft setting for any raft type. Keys that are not are
/// never filtered by the raft type, the settings UI shows them whatever the raft type is.
/// @param key Config key of a print setting.
/// @return true if some raft type reads the key.
bool is_raft_setting(const std::string& key);

} // namespace Slic3r::Domain::SLA
