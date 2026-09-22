#pragma once

#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include <cmath>

namespace Slic3r::Domain::SLA {

struct RaftPadValues {
    bool pad_enable = true;
    bool pad_around_object = false;
    double pad_wall_height_mm = 0.0;
    double pad_wall_thickness_mm = 2.0;
    double pad_brim_size_mm = 1.6;
    double pad_wall_slope_deg = 90.0;
    double pad_object_gap_mm = 1.0;
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

} // namespace Slic3r::Domain::SLA