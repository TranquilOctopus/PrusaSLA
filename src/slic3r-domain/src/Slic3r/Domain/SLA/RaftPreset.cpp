#include "Slic3r/Domain/SLA/RaftPreset.hpp"

namespace Slic3r::Domain::SLA {

// Named constants for Skate preset tuning (starting point, to be validated against Lychee/Chitubox)
constexpr double SKATE_BRIM_FACTOR = 0.5;  // Skate brim is half the user expansion
constexpr double SKATE_SLOPE_DEG = 70.0;   // Skate uses steeper walls (70 deg vs 90 deg straight)

RaftPadValues raft_preset_to_pad_values(
    sla::RaftType type,
    double wall_height_mm,
    double wall_thickness_mm,
    double expansion_mm,
    double slope_deg,
    double object_gap_mm
) {
    RaftPadValues vals;
    vals.pad_wall_height_mm = wall_height_mm;
    vals.pad_wall_thickness_mm = wall_thickness_mm;
    vals.pad_brim_size_mm = expansion_mm;
    vals.pad_wall_slope_deg = slope_deg;
    vals.pad_object_gap_mm = object_gap_mm;

    switch (type) {
    case sla::RaftType::None:
        vals.pad_enable = false;
        vals.pad_around_object = false;
        break;

    case sla::RaftType::Full:
        vals.pad_enable = true;
        vals.pad_around_object = false;
        break;

    case sla::RaftType::AroundObject:
        vals.pad_enable = true;
        vals.pad_around_object = true;
        break;

    case sla::RaftType::Skate:
        vals.pad_enable = true;
        vals.pad_around_object = true;
        vals.pad_brim_size_mm = expansion_mm * SKATE_BRIM_FACTOR;
        vals.pad_wall_slope_deg = SKATE_SLOPE_DEG;
        break;
    }

    return vals;
}

} // namespace Slic3r::Domain::SLA