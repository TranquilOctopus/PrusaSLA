#pragma once

#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include <cmath>
#include <string>
#include <vector>

namespace Slic3r::Domain::SLA {

// Raft infill defaults, named so the tuning has one place (the same style as the Skate constants
// below). Starting point to validate against Lychee and Chitubox: 2 mm cells keep the resin out of
// the pattern, 0.4 mm of material between two cells is about one layer on a fine resin, and half a
// millimetre of skin under the top face carries the object.
constexpr double RAFT_INFILL_SPACING_MM = 2.0;
constexpr double RAFT_INFILL_WALL_MM    = 0.4;
constexpr double RAFT_INFILL_SKIN_MM    = 0.5;

/// What the inside of the raft is filled with. The raft keeps a solid skin under its top face and
/// a solid rim as thick as the wall, so the object never rests on a hole and the raft stays
/// printable. A default built raft has no infill, which is the solid slab.
struct RaftInfill
{
    sla::RaftInfillType type      = sla::RaftInfillType::None;
    double              spacing_mm = RAFT_INFILL_SPACING_MM;
    double              wall_mm    = RAFT_INFILL_WALL_MM;
    double              skin_mm    = RAFT_INFILL_SKIN_MM;
};

struct RaftPadValues
{
    bool pad_enable              = true;
    bool pad_around_object       = false;
    double pad_wall_height_mm    = 0.0;
    double pad_wall_thickness_mm = 2.0;
    double pad_brim_size_mm      = 1.6;
    double pad_wall_slope_deg    = 90.0;
    double pad_object_gap_mm     = 1.0;
    double raft_edge_taper_mm    = 0.0;
    RaftInfill raft_infill       = {};
};

/// Map a raft type and shared knobs to the pad configuration values.
/// @param type The raft type preset.
/// @param wall_height_mm User-specified wall height (cavity depth).
/// @param wall_thickness_mm User-specified wall thickness.
/// @param expansion_mm User-specified brim expansion around geometry.
/// @param slope_deg User-specified wall slope in degrees (45-90).
/// @param object_gap_mm Gap between object bottom and pad in zero-elevation mode.
/// @param edge_taper_mm How far the top edge of the raft is bevelled in, 0 for a sharp edge.
/// @param infill What the inside of the raft is filled with. No raft type replaces it, the user
/// picks the pattern and the raft type only decides whether a raft is printed at all.
/// @return RaftPadValues to be applied to the pad generator.
RaftPadValues raft_preset_to_pad_values(
    sla::RaftType type,
    double wall_height_mm,
    double wall_thickness_mm,
    double expansion_mm,
    double slope_deg,
    double object_gap_mm,
    double edge_taper_mm,
    RaftInfill infill
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

/// The raft infill settings the given raft infill pattern reads, in the order they should be
/// shown: the size of one cell, the material between two cells and the skin under the top face.
/// A pattern of None leaves the raft solid, so it reads none of them and the list is empty.
/// raft_infill itself is not in the list: the pattern is picked where it is shown, so it is always
/// in the raft type list above.
/// @param infill The selected raft infill pattern.
/// @return Config keys of the settings that apply to this raft infill pattern.
const std::vector<std::string>& raft_infill_visible_settings(sla::RaftInfillType infill);

/// Whether the named config key is one of the settings the given raft infill pattern reads.
/// @param infill The selected raft infill pattern.
/// @param key Config key of a print setting.
/// @return true if the key applies to this raft infill pattern.
bool raft_infill_uses_setting(sla::RaftInfillType infill, const std::string& key);

/// The raft settings the given raft type and the given raft infill pattern together read, in the
/// order they should be shown. The raft type decides first (None prints no raft at all, so nothing
/// but raft_type is left), the pattern decides the three knobs that cut the pattern out of it.
/// @param type The selected raft type.
/// @param infill The selected raft infill pattern.
/// @return Config keys of the settings that apply to this raft type and infill pattern.
const std::vector<std::string>&
raft_visible_settings(sla::RaftType type, sla::RaftInfillType infill);

/// Whether the named config key is one of the raft settings the given raft type and raft infill
/// pattern read. A knob the raft type ignores is not, and neither is one of the three knobs that
/// shape a pattern that is not cut.
/// @param type The selected raft type.
/// @param infill The selected raft infill pattern.
/// @param key Config key of a print setting.
/// @return true if the key applies to this raft type and infill pattern.
bool raft_uses_setting(sla::RaftType type, sla::RaftInfillType infill, const std::string& key);

/// Whether the named config key is a raft setting for any raft type. Keys that are not are
/// never filtered by the raft type, the settings UI shows them whatever the raft type is.
/// @param key Config key of a print setting.
/// @return true if some raft type reads the key.
bool is_raft_setting(const std::string& key);

} // namespace Slic3r::Domain::SLA
