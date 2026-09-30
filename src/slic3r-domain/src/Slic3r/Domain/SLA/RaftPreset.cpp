#include "Slic3r/Domain/SLA/RaftPreset.hpp"

#include <algorithm>
#include <map>
#include <utility>

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
    double object_gap_mm,
    double edge_taper_mm,
    RaftInfill infill
) {
    RaftPadValues vals;
    vals.pad_wall_height_mm = wall_height_mm;
    vals.pad_wall_thickness_mm = wall_thickness_mm;
    vals.pad_brim_size_mm = expansion_mm;
    vals.pad_wall_slope_deg = slope_deg;
    vals.pad_object_gap_mm = object_gap_mm;
    vals.raft_edge_taper_mm = edge_taper_mm;
    // No raft type brings its own infill: how much resin a raft may save is a tuning question
    // against Lychee and Chitubox, so the user picks the pattern.
    vals.raft_infill = infill;

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

namespace {

// The knobs every printed raft reads: the cavity height, the wall thickness, how far the raft
// reaches around the geometry, how steep its walls are, how far its top edge is bevelled in, what
// its inside is filled with and how close separate pieces are allowed to be before they become
// one raft. The three knobs that shape the infill pattern are in here too: they are raft settings,
// but the raft type alone does not decide them, raft_infill_visible_settings does.
const std::vector<std::string>& raft_shape_settings()
{
    static const std::vector<std::string> settings{
        "pad_wall_height",
        "pad_wall_thickness",
        "pad_brim_size",
        "pad_wall_slope",
        "raft_edge_taper",
        "raft_infill",
        "raft_infill_spacing",
        "raft_infill_wall",
        "raft_infill_skin",
        "pad_max_merge_distance",
    };
    return settings;
}

// Only a raft around the object lifts the object off the build plate, so only there are the
// object gap, the "everywhere" override and the object connectors read (builtin_pad_cfg reads
// them only when the raft embeds the object).
const std::vector<std::string>& object_embed_settings()
{
    static const std::vector<std::string> settings{
        "pad_object_gap",
        "pad_around_object_everywhere",
        "pad_object_connector_stride",
        "pad_object_connector_width",
        "pad_object_connector_penetration",
    };
    return settings;
}

std::vector<std::string> visible_settings_for(sla::RaftType type)
{
    std::vector<std::string> ret{"raft_type"};

    // Raft None prints no raft, so none of the raft knobs change anything: not the edge taper, not
    // the infill, not the three knobs that shape the infill pattern.
    if (type == sla::RaftType::None)
        return ret;

    // Skate brings its own expansion and wall slope, so the user's values for those two would
    // be ignored.
    for (const std::string& key : raft_shape_settings()) {
        if (type == sla::RaftType::Skate && (key == "pad_brim_size" || key == "pad_wall_slope")) {
            continue;
        }
        ret.push_back(key);
    }

    if (type == sla::RaftType::AroundObject || type == sla::RaftType::Skate)
        ret.insert(ret.end(), object_embed_settings().begin(), object_embed_settings().end());

    return ret;
}

// The knobs that cut the raft infill pattern out of the inside of the raft. The raft type lists
// them because they are raft settings, but only a pattern that is actually cut reads them.
const std::vector<std::string>& raft_infill_shape_settings()
{
    static const std::vector<std::string> settings{
        "raft_infill_spacing",
        "raft_infill_wall",
        "raft_infill_skin",
    };
    return settings;
}

std::vector<std::string> visible_settings_for(sla::RaftType type, sla::RaftInfillType infill)
{
    std::vector<std::string> ret = visible_settings_for(type);

    if (infill != sla::RaftInfillType::None)
        return ret;

    // A solid raft fills its inside with resin, so the cell size, the wall between two cells and
    // the skin under the top face change nothing.
    std::erase_if(ret, [](const std::string& key) {
        return std::ranges::find(raft_infill_shape_settings(), key)
               != raft_infill_shape_settings().end();
    });

    return ret;
}

} // namespace

bool is_raft_setting(const std::string& key)
{
    // Around object reads every raft setting, so its list is the union over the raft types.
    static const std::vector<std::string> all_raft_settings{
        visible_settings_for(sla::RaftType::AroundObject)
    };

    return std::ranges::find(all_raft_settings, key) != all_raft_settings.end();
}

const std::vector<std::string>& raft_type_visible_settings(sla::RaftType type)
{
    static const std::vector<std::string> none{visible_settings_for(sla::RaftType::None)};
    static const std::vector<std::string> full{visible_settings_for(sla::RaftType::Full)};
    static const std::vector<std::string> around_object{
        visible_settings_for(sla::RaftType::AroundObject)
    };
    static const std::vector<std::string> skate{visible_settings_for(sla::RaftType::Skate)};

    switch (type) {
    case sla::RaftType::None:
        return none;
    case sla::RaftType::Full:
        return full;
    case sla::RaftType::AroundObject:
        return around_object;
    case sla::RaftType::Skate:
        return skate;
    }

    return none;
}

bool raft_type_uses_setting(sla::RaftType type, const std::string& key)
{
    const std::vector<std::string>& settings = raft_type_visible_settings(type);
    return std::ranges::find(settings, key) != settings.end();
}

const std::vector<std::string>& raft_infill_visible_settings(sla::RaftInfillType infill)
{
    // The three knobs that shape the pattern, or nothing at all for the solid raft.
    static const std::vector<std::string> none{};
    static const std::vector<std::string> patterned{raft_infill_shape_settings()};

    return infill == sla::RaftInfillType::None ? none : patterned;
}

bool raft_infill_uses_setting(sla::RaftInfillType infill, const std::string& key)
{
    const std::vector<std::string>& settings = raft_infill_visible_settings(infill);
    return std::ranges::find(settings, key) != settings.end();
}

const std::vector<std::string>&
raft_visible_settings(sla::RaftType type, sla::RaftInfillType infill)
{
    // The rule is a pure mapping, so the twelve lists are built once. std::map keeps the
    // references stable, which is what the const reference return promises.
    using Key = std::pair<sla::RaftType, sla::RaftInfillType>;
    static const std::map<Key, std::vector<std::string>> all_settings{
        {Key{sla::RaftType::None, sla::RaftInfillType::None},
         visible_settings_for(sla::RaftType::None, sla::RaftInfillType::None)},
        {Key{sla::RaftType::None, sla::RaftInfillType::Grid},
         visible_settings_for(sla::RaftType::None, sla::RaftInfillType::Grid)},
        {Key{sla::RaftType::None, sla::RaftInfillType::Honeycomb},
         visible_settings_for(sla::RaftType::None, sla::RaftInfillType::Honeycomb)},
        {Key{sla::RaftType::Full, sla::RaftInfillType::None},
         visible_settings_for(sla::RaftType::Full, sla::RaftInfillType::None)},
        {Key{sla::RaftType::Full, sla::RaftInfillType::Grid},
         visible_settings_for(sla::RaftType::Full, sla::RaftInfillType::Grid)},
        {Key{sla::RaftType::Full, sla::RaftInfillType::Honeycomb},
         visible_settings_for(sla::RaftType::Full, sla::RaftInfillType::Honeycomb)},
        {Key{sla::RaftType::AroundObject, sla::RaftInfillType::None},
         visible_settings_for(sla::RaftType::AroundObject, sla::RaftInfillType::None)},
        {Key{sla::RaftType::AroundObject, sla::RaftInfillType::Grid},
         visible_settings_for(sla::RaftType::AroundObject, sla::RaftInfillType::Grid)},
        {Key{sla::RaftType::AroundObject, sla::RaftInfillType::Honeycomb},
         visible_settings_for(sla::RaftType::AroundObject, sla::RaftInfillType::Honeycomb)},
        {Key{sla::RaftType::Skate, sla::RaftInfillType::None},
         visible_settings_for(sla::RaftType::Skate, sla::RaftInfillType::None)},
        {Key{sla::RaftType::Skate, sla::RaftInfillType::Grid},
         visible_settings_for(sla::RaftType::Skate, sla::RaftInfillType::Grid)},
        {Key{sla::RaftType::Skate, sla::RaftInfillType::Honeycomb},
         visible_settings_for(sla::RaftType::Skate, sla::RaftInfillType::Honeycomb)},
    };

    static const std::vector<std::string> fallback{
        visible_settings_for(sla::RaftType::None, sla::RaftInfillType::None)
    };

    const auto it = all_settings.find(Key{type, infill});
    return it != all_settings.end() ? it->second : fallback;
}

bool raft_uses_setting(sla::RaftType type, sla::RaftInfillType infill, const std::string& key)
{
    const std::vector<std::string>& settings = raft_visible_settings(type, infill);
    return std::ranges::find(settings, key) != settings.end();
}

} // namespace Slic3r::Domain::SLA
