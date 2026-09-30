#include "Slic3r/Biz/ResinProfile/ChituboxCfgExport.hpp"

#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"

#include <boost/algorithm/string/trim.hpp>

#include <fmt/format.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::Biz::ResinProfile {

namespace {

// The import turns mm/min into mm/s (see ResinProfileMapper.cpp), so going back is the same factor
// in the other direction.
constexpr double MM_PER_MIN_IN_MM_PER_S = 60.;

// The two halves of a price in a .cfg: a number and the unit it is per. Chitubox states a price per
// litre, and the price is only a price per litre together with the unit, so both keys are written
// or neither is. The spelling of the unit is not confirmed by a real file yet (M3.1/M3.2); it is
// one of the ways the import reads as a litre, so a file written here comes back in as the same
// price (see is_per_litre() in ResinProfileMapper.cpp).
constexpr const char* PRICE_KEY        = "resinPrice";
constexpr const char* PRICE_UNIT_KEY   = "resinUnit";
constexpr const char* PRICE_UNIT_LITRE = "L";

// The resin preset keys the price is made out of: the cost of one bottle, and the size of that
// bottle. Both are keys of sla_material_settings, and the import writes both of them (M3.15b).
constexpr const char* BOTTLE_VOLUME_KEY = "bottle_volume";
constexpr double      ML_PER_LITRE      = 1000.;

// A bottle size the resin preset does not state: the usual 1 litre bottle, the same assumption the
// import makes when a file names no size, so that a price and the cost of it agree both ways.
constexpr double ASSUMED_BOTTLE_VOLUME_ML = 1000.;

/// A number the way the config writes it into an .ini file and the way a .cfg wants it: six
/// significant digits, no trailing zeros. The same form the mapper writes, so that a value that
/// makes the round trip is the same text both ways.
std::string format_number(double value)
{
    return fmt::format("{:g}", value);
}

/// A name out of a preset, put on one line of the file: a colon or a line break in it would end
/// the line early and the rest of the name would be read as a key.
std::string single_line(const std::string& text)
{
    std::string line = text;
    for (char& character : line) {
        if (character == ':' || character == '\n' || character == '\r')
            character = ' ';
    }
    return boost::trim_copy(line);
}

/// What has to happen to a resin preset value to make it a Chitubox value.
enum class ReverseTransform
{
    None, ///< The printer class has no Chitubox key for this setting.
    Copy, ///< Same unit, written as it is.
    CopyInt, ///< A count or a level, written as a whole number.
    MmPerSecToMmPerMin, ///< A motion speed, converted back to mm/min.
    FirstOfPair, ///< A [below area fill, above area fill] pair; Chitubox has one value for both.
    PricePerLitre, ///< The cost of one bottle, turned into a price per litre with the bottle size.
};

/// What one printer class does with one resin preset key.
struct ReverseTarget
{
    ReverseTransform transform{ReverseTransform::None};
    std::string key;
    MappingStatus status{MappingStatus::NotApplicable};
};

/// One row of the mapping table of doc/sla-fork/ROADMAP.md, read the other way round: the resin
/// preset key, and the Chitubox key of each of the two printer classes.
struct ReverseRule
{
    std::string material_key;
    ReverseTarget tilt;
    ReverseTarget generic;
    std::string note;
};

/// The value a transform produced, plus what the row has to say about it.
struct Applied
{
    std::optional<std::string> value;
    /// The key of the unit, when the format states the unit as a key of its own.
    std::string unit_key;
    std::string unit_value;
    std::string note;
};

/// Read one number out of a resin preset, or nothing when the preset does not carry a number here.
std::optional<double> number_of(const Domain::ConfigItems& material, const std::string& key)
{
    const Domain::ConfigItem* item = material.find(key);
    if (item == nullptr || !item->holds_alternative<double>())
        return std::nullopt;
    return item->get<double>();
}

Applied apply(
    ReverseTransform          transform,
    const Domain::ConfigItems &material,
    const std::string         &material_key,
    const std::string         &rule_note
)
{
    Applied applied;
    applied.note = rule_note;
    const Domain::ConfigItem* item = material.find(material_key);

    switch (transform) {
    case ReverseTransform::None:
        // The note of the row says what the other printer class does with it, or why there is
        // nothing to write at all.
        break;

    case ReverseTransform::Copy:
    case ReverseTransform::MmPerSecToMmPerMin: {
        if (!item || !item->holds_alternative<double>()) {
            applied.note += " Nothing written: the resin preset does not carry a number here.";
            return applied;
        }
        const double value = item->get<double>();
        if (value <= 0.) {
            // Zero is not a setting but the absence of one in this config, and a .cfg key of zero
            // would be a value Chitubox tries to print with.
            applied.note += fmt::format(
                " Nothing written: the value is {:g}, which means the setting is not used.",
                value
            );
            return applied;
        }
        applied.value = format_number(
            transform == ReverseTransform::MmPerSecToMmPerMin ? value * MM_PER_MIN_IN_MM_PER_S : value
        );
        break;
    }

    case ReverseTransform::CopyInt: {
        if (!item || !item->holds_alternative<int>()) {
            applied.note += " Nothing written: the resin preset does not carry a whole number here.";
            return applied;
        }
        const int value = item->get<int>();
        if (value <= 0) {
            applied.note += fmt::format(
                " Nothing written: the value is {}, which means the setting is not used.",
                value
            );
            return applied;
        }
        applied.value = std::to_string(value);
        break;
    }

    case ReverseTransform::FirstOfPair: {
        if (!item || !item->holds_alternative<std::vector<double>>()) {
            applied.note += " Nothing written: the resin preset does not carry a pair of times here.";
            return applied;
        }
        const std::vector<double> pair = item->get<std::vector<double>>();
        if (pair.empty() || pair.front() <= 0.) {
            applied.note += " Nothing written: the setting is empty, which means it is not used.";
            return applied;
        }
        // The value below the area fill threshold is the one that is written: an import writes the
        // same value into both, so a profile that came from a .cfg has no way of telling them apart.
        applied.value = format_number(pair.front());
        if (pair.size() > 1 && pair[1] != pair[0]) {
            applied.note += fmt::format(
                " Only the value below the area fill threshold is written; the one above it is {:g} and Chitubox has no key for it.",
                pair[1]
            );
        }
        break;
    }

    case ReverseTransform::PricePerLitre: {
        const std::optional<double> cost = number_of(material, material_key);
        if (!cost) {
            applied.note += " Nothing written: the resin preset does not carry a number here.";
            return applied;
        }
        if (*cost <= 0.) {
            // The same rule as every other number: zero is not a price but the absence of one.
            applied.note += fmt::format(
                " Nothing written: the value is {:g}, which means the setting is not used.",
                *cost
            );
            return applied;
        }

        // The bottle the cost is for is a resin setting of its own since M3.15b, which is what makes
        // this possible. A bottle size of zero is not a bottle, so the assumed one is used and the
        // report says so rather than dividing by it.
        const std::optional<double> bottle_ml = number_of(material, BOTTLE_VOLUME_KEY);
        double                       volume    = ASSUMED_BOTTLE_VOLUME_ML;
        if (bottle_ml && *bottle_ml > 0.) {
            volume = *bottle_ml;
        } else {
            applied.note += fmt::format(
                " The bottle volume is {:g}, so the cost is priced per the usual {:g} ml bottle instead.",
                bottle_ml.value_or(0.),
                volume
            );
        }

        applied.value      = format_number(*cost * ML_PER_LITRE / volume);
        applied.unit_key   = PRICE_UNIT_KEY;
        applied.unit_value = PRICE_UNIT_LITRE;
        applied.note += fmt::format(
            " Computed from {:g} for a {:g} ml bottle. The unit is written as \"{}\", and the spelling that format uses is not verified yet (M3.1/M3.2).",
            *cost,
            volume,
            PRICE_UNIT_LITRE
        );
        break;
    }
    }

    return applied;
}

// The mapping table of doc/sla-fork/ROADMAP.md, row by row, in reverse: a resin preset key and the
// Chitubox key of each printer class. The order of the rows is the order the keys are written in,
// so the file reads like the profile the slicer would export. Every key the import table writes is
// here, and nothing else: a resin setting Chitubox has no key for is reported as left out rather
// than written under a key that means something else.
const std::vector<ReverseRule>& reverse_table()
{
    static const std::vector<ReverseRule> table{
        {.material_key = "exposure_time",
         .tilt         = {ReverseTransform::Copy, "normalExposureTime", MappingStatus::Exact},
         .generic      = {ReverseTransform::Copy, "normalExposureTime", MappingStatus::Exact},
         .note         = "Both in seconds, no conversion."},

        {.material_key = "initial_exposure_time",
         .tilt         = {ReverseTransform::Copy, "bottomLayerExposureTime", MappingStatus::Exact},
         .generic      = {ReverseTransform::Copy, "bottomLayerExposureTime", MappingStatus::Exact},
         .note         = "Both in seconds, no conversion."},

        // A tilt printer has no block of bottom layers: its transition layer count is the setting
        // the exposure is faded over, and that is what the import wrote for either count.
        {.material_key = "resin_faded_layers",
         .tilt    = {ReverseTransform::CopyInt, "transitionLayers", MappingStatus::Exact},
         .generic = {ReverseTransform::CopyInt, "transitionLayers", MappingStatus::Exact},
         .note    = "The number of layers the exposure is faded over, both printer classes."},

        {.material_key = "bottom_layer_count",
         .tilt         = {ReverseTransform::None, "", MappingStatus::NotApplicable},
         .generic      = {ReverseTransform::CopyInt, "bottomLayerCount", MappingStatus::Exact},
         .note         = "A tilt printer has no block of bottom layers, it fades the exposure over resin_faded_layers instead."},

        {.material_key = "resin_layer_height",
         .tilt    = {ReverseTransform::Copy, "layerHeight", MappingStatus::Exact},
         .generic = {ReverseTransform::Copy, "layerHeight", MappingStatus::Exact},
         .note    = "A layer height of zero would mean \"use the print preset's layer height\", so it is not written."},

        {.material_key = "material_density",
         .tilt    = {ReverseTransform::Copy, "resinDensity", MappingStatus::Exact},
         .generic = {ReverseTransform::Copy, "resinDensity", MappingStatus::Exact},
         .note    = "Both in g/ml, no conversion."},

        // The price is the one setting the import computes and the export has to take apart again.
        // The bottle it was computed with is a resin setting of its own, so a price per litre can be
        // written out of the two of them, with the unit that makes it a price per litre.
        {.material_key = "bottle_cost",
         .tilt         = {ReverseTransform::PricePerLitre, PRICE_KEY, MappingStatus::Converted},
         .generic      = {ReverseTransform::PricePerLitre, PRICE_KEY, MappingStatus::Converted},
         .note         = "A price per litre and the unit it is per, from the cost of one bottle and the size of that bottle. The currency is not converted."},

        {.material_key = "bottle_volume",
         .tilt         = {ReverseTransform::Copy, "bottleVolume", MappingStatus::Exact},
         .generic      = {ReverseTransform::Copy, "bottleVolume", MappingStatus::Exact},
         .note         = "The bottle resinPrice is a price per litre of, both in ml. Written so that a file that comes back is priced for the same bottle."},

        {.material_key = "delay_before_exposure",
         .tilt    = {ReverseTransform::FirstOfPair, "lightOffTime", MappingStatus::Approximated},
         .generic = {ReverseTransform::FirstOfPair, "lightOffTime", MappingStatus::Approximated},
         .note    = "The light-off time is one value here, where the resin preset has one per area fill. The meaning is not verified yet (M3.1/M3.2)."},

        // A tilt printer has no wait before a lift, so the import put the time into the delay after
        // the exposure, and a printer that lifts writes it into wait_before_lift instead.
        {.material_key = "delay_after_exposure",
         .tilt         = {ReverseTransform::FirstOfPair, "resetTimeBeforeLift", MappingStatus::Approximated},
         .generic      = {ReverseTransform::None, "", MappingStatus::NotApplicable},
         .note         = "A printer that lifts a build plate waits before the lift with wait_before_lift, which Chitubox states as a time of its own. The meaning is not verified yet (M3.1/M3.2)."},

        {.material_key = "wait_before_lift",
         .tilt         = {ReverseTransform::None, "", MappingStatus::NotApplicable},
         .generic      = {ReverseTransform::Copy, "resetTimeBeforeLift", MappingStatus::Exact},
         .note         = "A tilt printer separates layers by tilting and has no wait before a lift."},

        {.material_key = "wait_after_lift",
         .tilt         = {ReverseTransform::None, "", MappingStatus::NotApplicable},
         .generic      = {ReverseTransform::Copy, "resetTimeAfterLift", MappingStatus::Exact},
         .note         = "A tilt printer separates layers by tilting and has no wait after a lift."},

        // Layer separation by lift: the heights first, then the speeds, on generic MSLA printers
        // only. A tilt printer tilts and has none of them.
        {.material_key = "lift_height",
         .tilt         = {ReverseTransform::None, "", MappingStatus::NotApplicable},
         .generic      = {ReverseTransform::Copy, "normalLayerLiftHeight", MappingStatus::Exact},
         .note         = "A tilt printer separates layers by tilting, so it has no lift height."},
        {.material_key = "lift_height_2",
         .tilt         = {ReverseTransform::None, "", MappingStatus::NotApplicable},
         .generic      = {ReverseTransform::Copy, "normalLayerLiftHeight2", MappingStatus::Exact},
         .note         = "Second stage of the two-stage lift, above the area fill threshold."},
        {.material_key = "bottom_lift_height",
         .tilt         = {ReverseTransform::None, "", MappingStatus::NotApplicable},
         .generic      = {ReverseTransform::Copy, "bottomLayerLiftHeight", MappingStatus::Exact},
         .note         = "Lift height for the bottom layers."},
        {.material_key = "bottom_lift_height_2",
         .tilt         = {ReverseTransform::None, "", MappingStatus::NotApplicable},
         .generic      = {ReverseTransform::Copy, "bottomLayerLiftHeight2", MappingStatus::Exact},
         .note         = "Second stage of the two-stage lift on the bottom layers."},

        // The speeds the other way round: the resin preset is mm/s, Chitubox is assumed mm/min.
        {.material_key = "lift_speed",
         .tilt         = {ReverseTransform::None, "", MappingStatus::NotApplicable},
         .generic      = {ReverseTransform::MmPerSecToMmPerMin, "normalLayerLiftSpeed", MappingStatus::Converted},
         .note         = "Written back in mm/min, the source unit is not verified yet (M3.1/M3.2). A tilt printer has no lift speed."},
        {.material_key = "lift_speed_2",
         .tilt         = {ReverseTransform::None, "", MappingStatus::NotApplicable},
         .generic      = {ReverseTransform::MmPerSecToMmPerMin, "normalLayerLiftSpeed2", MappingStatus::Converted},
         .note         = "Written back in mm/min, the source unit is not verified yet (M3.1/M3.2). Second stage of the two-stage lift, above the area fill threshold."},
        {.material_key = "bottom_lift_speed",
         .tilt         = {ReverseTransform::None, "", MappingStatus::NotApplicable},
         .generic      = {ReverseTransform::MmPerSecToMmPerMin, "bottomLayerLiftSpeed", MappingStatus::Converted},
         .note         = "Written back in mm/min, the source unit is not verified yet (M3.1/M3.2). Lift speed for the bottom layers."},
        {.material_key = "bottom_lift_speed_2",
         .tilt         = {ReverseTransform::None, "", MappingStatus::NotApplicable},
         .generic      = {ReverseTransform::MmPerSecToMmPerMin, "bottomLayerLiftSpeed2", MappingStatus::Converted},
         .note         = "Written back in mm/min, the source unit is not verified yet (M3.1/M3.2). Second stage of the two-stage lift on the bottom layers."},
        {.material_key = "retract_speed",
         .tilt         = {ReverseTransform::None, "", MappingStatus::NotApplicable},
         .generic      = {ReverseTransform::MmPerSecToMmPerMin, "normalDropSpeed", MappingStatus::Converted},
         .note         = "Written back in mm/min, the source unit is not verified yet (M3.1/M3.2). The drop speed is the retract speed."},
        {.material_key = "retract_speed_2",
         .tilt         = {ReverseTransform::None, "", MappingStatus::NotApplicable},
         .generic      = {ReverseTransform::MmPerSecToMmPerMin, "normalDropSpeed2", MappingStatus::Converted},
         .note         = "Written back in mm/min, the source unit is not verified yet (M3.1/M3.2). Second stage of the two-stage drop, above the area fill threshold."},

        {.material_key = "light_pwm",
         .tilt         = {ReverseTransform::None, "", MappingStatus::NotApplicable},
         .generic      = {ReverseTransform::CopyInt, "normalLightIntensityPWM", MappingStatus::Exact},
         .note         = "A tilt printer drives its LEDs from the machine calibration, it has no resin PWM."},
        {.material_key = "bottom_light_pwm",
         .tilt         = {ReverseTransform::None, "", MappingStatus::NotApplicable},
         .generic      = {ReverseTransform::CopyInt, "bottomLightIntensityPWM", MappingStatus::Exact},
         .note         = "A tilt printer drives its LEDs from the machine calibration, it has no resin PWM."},

        // A .cfg states neither of these, so they are named in the report instead.
        {.material_key = "material_vendor",
         .tilt         = {ReverseTransform::None, "", MappingStatus::NotApplicable},
         .generic      = {ReverseTransform::None, "", MappingStatus::NotApplicable},
         .note         = "A Chitubox .cfg names no vendor of the resin."},
        {.material_key = "use_tilt",
         .tilt         = {ReverseTransform::None, "", MappingStatus::NotApplicable},
         .generic      = {ReverseTransform::None, "", MappingStatus::NotApplicable},
         .note         = "A setting of a machine, not of a resin; the mapping table is already chosen by it."},
    };
    return table;
}

} // anonymous namespace

ChituboxCfgExport export_chitubox_cfg_report(
    const Domain::ConfigItems& material,
    TargetPrinterClass printer_class,
    const std::string& profile_name
)
{
    ChituboxCfgExport result;

    // The header is a comment, which the reader skips. It says what the file is, because a .cfg
    // of a resin is otherwise indistinguishable from a .cfg of a print, which is the same file.
    std::string text = "# Resin profile written by PrusaSLA, in the format Chitubox reads\n";
    text += fmt::format("# Mapping: {}\n", to_string(printer_class));
    if (!profile_name.empty())
        text += fmt::format("currProfile: {}\n", single_line(profile_name));

    for (const ReverseRule& rule : reverse_table()) {
        const ReverseTarget& target = printer_class == TargetPrinterClass::Tilt ? rule.tilt : rule.generic;
        const Applied applied = apply(target.transform, material, rule.material_key, rule.note);

        if (!applied.value || target.key.empty()) {
            result.keys.push_back(
                {.material_key  = rule.material_key,
                 .chitubox_key  = {},
                 .value         = {},
                 .unit_key      = {},
                 .unit_value    = {},
                 .status        = target.status,
                 .note          = applied.note}
            );
            result.skipped.push_back(rule.material_key);
            continue;
        }

        text += fmt::format("{}: {}\n", target.key, *applied.value);
        // A price is a number and the unit it is per, and that format states them as two keys, so
        // the unit goes in next to the value or the price is not a price at all on the way back in.
        if (!applied.unit_key.empty())
            text += fmt::format("{}: {}\n", applied.unit_key, applied.unit_value);
        result.keys.push_back(
            {.material_key  = rule.material_key,
             .chitubox_key  = target.key,
             .value         = *applied.value,
             .unit_key      = applied.unit_key,
             .unit_value    = applied.unit_value,
             .status        = target.status,
             .note          = applied.note}
        );
    }

    result.text = std::move(text);
    return result;
}

std::string export_chitubox_cfg(
    const Domain::ConfigItems& material,
    TargetPrinterClass printer_class,
    const std::string& profile_name
)
{
    return export_chitubox_cfg_report(material, printer_class, profile_name).text;
}

TargetPrinterClass export_printer_class(const Domain::ConfigItems& material, const std::string& printer_model)
{
    // A view is what printer_class_from_config() reads, so the export builds one out of the two
    // hints it has: the use_tilt of the resin preset and the model of its printer. A resin preset
    // that turns use_tilt off is one of a printer that lifts, whatever the model is called, which
    // is the rule the import picks its table with.
    Domain::ConfigPackSLA pack;
    if (const Domain::ConfigItem* use_tilt = material.find("use_tilt");
        use_tilt && use_tilt->holds_alternative<std::vector<bool>>())
    {
        pack.sla_material_settings.items.opt("use_tilt").set(use_tilt->get<std::vector<bool>>());
    }
    if (!printer_model.empty())
        pack.sla_printer_settings.items.opt("printer_model").set(printer_model);

    auto full_config = std::make_shared<const Domain::FullConfigSLA>(
        pack,
        Domain::Preset::HwPrinterConfig{.technology = Domain::PrinterTechnology::SLA}
    );
    Domain::ConfigView view{std::move(full_config), {}};
    // Until finalize() runs, values() is empty and every lookup misses.
    view.finalize();
    return printer_class_from_config(view);
}

} // namespace Slic3r::Biz::ResinProfile
