#include "Slic3r/Biz/ResinProfile/ResinProfileMapper.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/lexical_cast.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r::Biz::ResinProfile {

std::string to_string(MappingStatus status)
{
    switch (status) {
    case MappingStatus::Exact:
        return "Exact";
    case MappingStatus::Converted:
        return "Converted";
    case MappingStatus::Approximated:
        return "Approximated";
    case MappingStatus::NotApplicable:
        return "Not applicable";
    case MappingStatus::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

std::string to_string(TargetPrinterClass printer_class)
{
    return printer_class == TargetPrinterClass::Tilt ? "Tilt" : "Generic MSLA";
}

namespace {

// The exposure fade of a tilt printer is stepped down over a fixed range of layers, see
// ConfigDefsSLA.cpp "faded_layers" (min 3, max 20).
constexpr int MIN_FADED_LAYERS = 3;
constexpr int MAX_FADED_LAYERS = 20;

// Chitubox states the motion speeds in mm/min (unverified, see M3.1/M3.2), the SLA config wants mm/s.
constexpr double MM_PER_MIN_IN_MM_PER_S = 60.;

// A price per litre is a price per 1000 ml bottle only if the bottle is that big. Without a bottle
// size in the profile the mapper assumes the usual 1 litre bottle and says so in the note.
constexpr double ASSUMED_BOTTLE_VOLUME_ML = 1000.;

// The "verify" of the mapping table: what unit Chitubox writes is settled by M3.1/M3.2, so every
// converted speed says so instead of claiming the unit is known.
constexpr std::string_view SPEED_UNIT_CAVEAT =
    "Converted from mm/min to mm/s, the source unit is not verified yet (M3.1/M3.2). ";

// The units of the mapping table. A row shows the value of the file next to the value that is
// written, each with its unit, so a converted value can be read without opening the note. The
// spellings are the ones ConfigDefsSLA.cpp gives the same quantity; they are not translated, because
// a report and a table must read the same in every language. A key that is not a quantity carries
// none, and so does a key whose unit M3.1/M3.2 have not settled: a unit that is not known is not
// written down.
constexpr const char* UNIT_SECONDS    = "s";
constexpr const char* UNIT_MM         = "mm";
constexpr const char* UNIT_MM_PER_MIN = "mm/min";
constexpr const char* UNIT_MM_PER_SEC = "mm/s";
constexpr const char* UNIT_G_PER_ML   = "g/ml";
constexpr const char* UNIT_ML         = "ml";

std::string trim(const std::string& text)
{
    return boost::trim_copy(text);
}

/// Parse a foreign value. The file is untrusted input, so anything that is not a number is
/// reported instead of silently becoming zero.
std::optional<double> parse_number(const std::string& text)
{
    const std::string trimmed = trim(text);
    if (trimmed.empty())
        return std::nullopt;
    try {
        return boost::lexical_cast<double>(trimmed);
    } catch (const boost::bad_lexical_cast&) {
        return std::nullopt;
    }
}

/// A value the config stores as an int, e.g. a layer count or a PWM level.
std::optional<int> parse_int(const std::string& text)
{
    const std::optional<double> number = parse_number(text);
    if (!number || std::fabs(*number - std::round(*number)) > 1e-9)
        return std::nullopt;
    return static_cast<int>(std::llround(*number));
}

/// Format a number the way the config writes it into an .ini file: six significant digits,
/// no trailing zeros ("2.5", "1.66667").
std::string format_number(double value)
{
    return fmt::format("{:g}", value);
}

std::string format_int(int value)
{
    return std::to_string(value);
}

/// Does a resin unit name a litre? The exact spellings Chitubox writes are settled by M3.1/M3.2,
/// so this accepts the usual ways of writing "per litre" and nothing else.
bool is_per_litre(const std::string& unit)
{
    std::string text = boost::to_lower_copy(trim(unit));
    text.erase(
        std::remove_if(
            text.begin(),
            text.end(),
            [](unsigned char c) { return std::isspace(c) || c == '.' || c == '_'; }
        ),
        text.end()
    );
    if (text.empty())
        return false;
    for (const std::string& per_litre : {"/l", "perliter", "perliters", "perlitre", "perlitres"})
        if (text.ends_with(per_litre))
            return true;
    for (const std::string& litre : {"l", "1l", "liter", "liters", "litre", "litres"})
        if (text == litre)
            return true;
    return false;
}

bool contains_ignore_case(const std::string& haystack, const std::string& needle)
{
    return boost::algorithm::contains(boost::to_lower_copy(haystack), boost::to_lower_copy(needle));
}

/// What has to happen to a foreign value to make it a material preset value.
enum class Transform
{
    None, ///< The row documents a key; nothing is written.
    Copy, ///< The value is written as it appears in the file.
    CopyInt, ///< The value is a count or a level, written as an int.
    MmPerMinToMmPerSec, ///< Motion speed, converted from mm/min to mm/s.
    ClampFadedLayers, ///< Layer count, clamped into the range a tilt printer fades over.
    ExposureDelayPair, ///< One time, written into a [below area fill, above area fill] pair.
    BottleCost, ///< Price plus unit, turned into the cost of one bottle.
    SuggestedName, ///< The value becomes the suggested preset name.
    PrinterHint, ///< The value says which printer the profile is for.
};

/// What one printer class does with a source key. An empty key means "not written".
struct Target
{
    Transform transform{Transform::None};
    std::string key;
    MappingStatus status{MappingStatus::NotApplicable};
    /// The unit the written value is in, empty when the key is not written or is not a quantity.
    std::string unit;
};

/// One row of the mapping table of doc/sla-fork/ROADMAP.md.
struct Rule
{
    /// Foreign keys this rule reads, the first one present in the profile wins.
    std::vector<std::string> sources;
    /// Foreign key prefixes this rule reads, each match is reported on its own.
    std::vector<std::string> prefixes;
    Target tilt;
    Target generic;
    /// The unit the source value is in, empty when it is not a quantity or not settled yet.
    std::string source_unit;
    std::string note;
};

/// The value a transform produced, plus what the row has to say about it.
struct Applied
{
    std::optional<std::string> value;
    std::string note;
};

Applied apply(
    Transform transform,
    const ForeignResinProfile& profile,
    const std::string& source_value,
    const std::string& rule_note
)
{
    Applied applied;
    applied.note = rule_note;

    switch (transform) {
    case Transform::None:
    case Transform::SuggestedName:
    case Transform::PrinterHint:
        break;

    case Transform::Copy: {
        if (!parse_number(source_value)) {
            applied.note += " Nothing written: the value \"" + source_value + "\" is not a number.";
            return applied;
        }
        applied.value = trim(source_value);
        break;
    }

    case Transform::CopyInt: {
        const std::optional<int> count = parse_int(source_value);
        if (!count) {
            applied.note +=
                " Nothing written: the value \"" + source_value + "\" is not a whole number.";
            return applied;
        }
        applied.value = format_int(*count);
        break;
    }

    case Transform::MmPerMinToMmPerSec: {
        const std::optional<double> mm_per_min = parse_number(source_value);
        if (!mm_per_min) {
            applied.note += " Nothing written: the value \"" + source_value + "\" is not a number.";
            return applied;
        }
        applied.value = format_number(*mm_per_min / MM_PER_MIN_IN_MM_PER_S);
        break;
    }

    case Transform::ClampFadedLayers: {
        const std::optional<int> layers = parse_int(source_value);
        if (!layers) {
            applied.note +=
                " Nothing written: the value \"" + source_value + "\" is not a layer count.";
            return applied;
        }
        const int clamped = std::clamp(*layers, MIN_FADED_LAYERS, MAX_FADED_LAYERS);
        applied.value     = format_int(clamped);
        if (clamped != *layers)
            applied.note += fmt::format(
                " Clamped from {} to {}, the only range a tilt printer fades over.",
                *layers,
                clamped
            );
        break;
    }

    case Transform::ExposureDelayPair: {
        const std::optional<double> seconds = parse_number(source_value);
        if (!seconds) {
            applied.note +=
                " Nothing written: the value \"" + source_value + "\" is not a number of seconds.";
            return applied;
        }
        // One time, used for both the area below the fill threshold and the area above it.
        applied.value = format_number(*seconds) + "," + format_number(*seconds);
        break;
    }

    case Transform::BottleCost: {
        const std::optional<double> price = parse_number(source_value);
        if (!price) {
            applied.note += " Nothing written: the price \"" + source_value + "\" is not a number.";
            return applied;
        }
        const std::string unit = profile.raw_values.count("resinUnit") ?
            profile.raw_values.at("resinUnit") :
            std::string{};
        if (!is_per_litre(unit)) {
            applied.note += unit.empty() ?
                " Nothing written: the profile does not say the price is per litre." :
                " Nothing written: \"" + unit + "\" is not a per-litre price.";
            return applied;
        }
        double bottle_volume_ml = ASSUMED_BOTTLE_VOLUME_ML;
        for (const std::string& key : {"bottleVolume", "bottle_volume"}) {
            const std::optional<double> volume = parse_number(
                profile.raw_values.count(key) ? profile.raw_values.at(key) : std::string{}
            );
            if (volume && *volume > 0.)
                bottle_volume_ml = *volume;
        }
        applied.value = format_number(*price * bottle_volume_ml / 1000.);
        applied.note += fmt::format(
            " Computed from {:.6g} per litre and a {:.6g} ml bottle.",
            *price,
            bottle_volume_ml
        );
        break;
    }
    }

    return applied;
}

// The mapping table, row by row, in the order of doc/sla-fork/ROADMAP.md. Every source key of a
// profile ends up in the report: the keys matched here with a status of their own, the rest as
// Unknown. Nothing here writes a key that is not a material option of the SLA config.
const std::vector<Rule>& mapping_table()
{
    static const std::vector<Rule> table{
        // Exposure. expTime is the spelling of an SL1 archive, which store_sl1 writes straight
        // off exposure_time, so it is seconds too and needs no conversion.
        {.sources     = {"normalExposureTime", "expTime"},
         .tilt        = {Transform::Copy, "exposure_time", MappingStatus::Exact, UNIT_SECONDS},
         .generic     = {Transform::Copy, "exposure_time", MappingStatus::Exact, UNIT_SECONDS},
         .source_unit = UNIT_SECONDS,
         .note        = "Both in seconds, no conversion."},

        // Bottom exposure, under either spelling. expTimeFirst is what an SL1 archive calls it,
        // in seconds as well, which store_sl1 writes straight off initial_exposure_time.
        {.sources = {"bottomLayerExposureTime", "bottomLayExposureTime", "expTimeFirst"},
         .tilt    = {Transform::Copy, "initial_exposure_time", MappingStatus::Exact, UNIT_SECONDS},
         .generic = {Transform::Copy, "initial_exposure_time", MappingStatus::Exact, UNIT_SECONDS},
         .source_unit = UNIT_SECONDS,
         .note        = "Both in seconds, no conversion."},

        // A transition layer count is the same thing on both printer classes, and it is a resin
        // setting: the print preset's faded_layers only applies when the resin has none.
        {
            .sources =
                {"transitionLayers", "transitionLayerCount", "fadedLayers", "fadedLayerCount",
                 "numFade"},
            .tilt    = {Transform::CopyInt, "resin_faded_layers", MappingStatus::Exact},
            .generic = {Transform::CopyInt, "resin_faded_layers", MappingStatus::Exact},
            .note =
                "The number of layers the exposure is faded over; as a resin setting it overrides the print preset's faded_layers."
        },

        // The bottom block, which the two printer classes express differently.
        {
            .sources = {"bottomLayerCount", "bottomLayCount"},
            .tilt =
                {Transform::ClampFadedLayers, "resin_faded_layers", MappingStatus::Approximated},
            .generic = {Transform::CopyInt, "bottom_layer_count", MappingStatus::Exact},
            .note =
                "A tilt printer has no block of bottom layers, it fades the exposure over 3 to 20 layers, so the count becomes the transition layer count and is clamped. A generic MSLA printer keeps the count and prints it with the bottom_* settings."
        },

        // Layer height is a resin setting since M0.4, so no preset variant is needed.
        {
            .sources     = {"layerHeight"},
            .tilt        = {Transform::Copy, "resin_layer_height", MappingStatus::Exact, UNIT_MM},
            .generic     = {Transform::Copy, "resin_layer_height", MappingStatus::Exact, UNIT_MM},
            .source_unit = UNIT_MM,
            .note =
                "Layer height is a resin setting; 0 would mean \"use the print preset's layer height\"."
        },

        {.sources     = {"resinDensity"},
         .tilt        = {Transform::Copy, "material_density", MappingStatus::Exact, UNIT_G_PER_ML},
         .generic     = {Transform::Copy, "material_density", MappingStatus::Exact, UNIT_G_PER_ML},
         .source_unit = UNIT_G_PER_ML,
         .note        = "Both in g/ml, no conversion."},

        {.sources = {"resinPrice"},
         .tilt    = {Transform::BottleCost, "bottle_cost", MappingStatus::Converted},
         .generic = {Transform::BottleCost, "bottle_cost", MappingStatus::Converted},
         .note =
             "bottle_cost = price x bottle volume / 1000, and only for a per-litre price. The currency is not converted."},

        {.sources = {"resinUnit"},
         .tilt    = {Transform::None, "", MappingStatus::Converted},
         .generic = {Transform::None, "", MappingStatus::Converted},
         .note =
             "Read together with resinPrice: a per-litre price becomes a bottle cost, any other unit does not."},

        {.sources     = {"bottleVolume", "bottle_volume"},
         .tilt        = {Transform::None, "", MappingStatus::Converted},
         .generic     = {Transform::None, "", MappingStatus::Converted},
         .source_unit = UNIT_ML,
         .note =
             "The bottle size resinPrice is turned into a bottle cost with; without it a 1 litre bottle is assumed."},

        {.sources = {"lightOffTime", "bottomLightOffTime"},
         .tilt =
             {Transform::ExposureDelayPair,
              "delay_before_exposure",
              MappingStatus::Approximated,
              UNIT_SECONDS},
         .generic =
             {Transform::ExposureDelayPair,
              "delay_before_exposure",
              MappingStatus::Approximated,
              UNIT_SECONDS},
         .source_unit = UNIT_SECONDS,
         .note =
             "The light-off time is written as a delay before exposure, the same below and above the area fill. The meaning is not verified yet (M3.1/M3.2)."},

        {.sources = {"resetTimeBeforeLift"},
         .tilt =
             {Transform::ExposureDelayPair,
              "delay_after_exposure",
              MappingStatus::Approximated,
              UNIT_SECONDS},
         .generic     = {Transform::Copy, "wait_before_lift", MappingStatus::Exact, UNIT_SECONDS},
         .source_unit = UNIT_SECONDS,
         .note =
             "A tilt printer has no wait before a lift, so the time becomes a delay after the exposure, the same below and above the area fill."},

        {.sources     = {"resetTimeAfterLift"},
         .tilt        = {Transform::None, "", MappingStatus::NotApplicable},
         .generic     = {Transform::Copy, "wait_after_lift", MappingStatus::Exact, UNIT_SECONDS},
         .source_unit = UNIT_SECONDS,
         .note        = "A tilt printer separates layers by tilting and has no wait after a lift."},

        // Layer separation by lift: heights first, then the speeds, all only on generic MSLA
        // printers. The short key spellings are the ones M3.4's two-stage lift fixture uses.
        {.sources     = {"normalLayerLiftHeight", "liftHeight"},
         .tilt        = {Transform::None, "", MappingStatus::NotApplicable},
         .generic     = {Transform::Copy, "lift_height", MappingStatus::Exact, UNIT_MM},
         .source_unit = UNIT_MM,
         .note        = "A tilt printer separates layers by tilting, so it has no lift height."},
        {.sources     = {"normalLayerLiftHeight2", "liftHeight2"},
         .tilt        = {Transform::None, "", MappingStatus::NotApplicable},
         .generic     = {Transform::Copy, "lift_height_2", MappingStatus::Exact, UNIT_MM},
         .source_unit = UNIT_MM,
         .note        = "Second stage of the two-stage lift, above the area fill threshold."},
        {.sources     = {"bottomLayerLiftHeight", "bottomLiftHeight"},
         .tilt        = {Transform::None, "", MappingStatus::NotApplicable},
         .generic     = {Transform::Copy, "bottom_lift_height", MappingStatus::Exact, UNIT_MM},
         .source_unit = UNIT_MM,
         .note        = "Lift height for the bottom layers."},
        {.sources     = {"bottomLayerLiftHeight2", "bottomLiftHeight2"},
         .tilt        = {Transform::None, "", MappingStatus::NotApplicable},
         .generic     = {Transform::Copy, "bottom_lift_height_2", MappingStatus::Exact, UNIT_MM},
         .source_unit = UNIT_MM,
         .note        = "Second stage of the two-stage lift on the bottom layers."},

        {.sources = {"normalLayerLiftSpeed", "liftSpeed"},
         .tilt    = {Transform::None, "", MappingStatus::NotApplicable},
         .generic =
             {Transform::MmPerMinToMmPerSec,
              "lift_speed",
              MappingStatus::Converted,
              UNIT_MM_PER_SEC},
         .source_unit = UNIT_MM_PER_MIN,
         .note        = std::string(SPEED_UNIT_CAVEAT) + "A tilt printer has no lift speed."},
        {.sources = {"normalLayerLiftSpeed2", "liftSpeed2"},
         .tilt    = {Transform::None, "", MappingStatus::NotApplicable},
         .generic =
             {Transform::MmPerMinToMmPerSec,
              "lift_speed_2",
              MappingStatus::Converted,
              UNIT_MM_PER_SEC},
         .source_unit = UNIT_MM_PER_MIN,
         .note        = std::string(SPEED_UNIT_CAVEAT)
             + "Second stage of the two-stage lift, above the area fill threshold."},
        {.sources = {"bottomLayerLiftSpeed", "bottomLiftSpeed"},
         .tilt    = {Transform::None, "", MappingStatus::NotApplicable},
         .generic =
             {Transform::MmPerMinToMmPerSec,
              "bottom_lift_speed",
              MappingStatus::Converted,
              UNIT_MM_PER_SEC},
         .source_unit = UNIT_MM_PER_MIN,
         .note        = std::string(SPEED_UNIT_CAVEAT) + "Lift speed for the bottom layers."},
        {.sources = {"bottomLayerLiftSpeed2", "bottomLiftSpeed2"},
         .tilt    = {Transform::None, "", MappingStatus::NotApplicable},
         .generic =
             {Transform::MmPerMinToMmPerSec,
              "bottom_lift_speed_2",
              MappingStatus::Converted,
              UNIT_MM_PER_SEC},
         .source_unit = UNIT_MM_PER_MIN,
         .note        = std::string(SPEED_UNIT_CAVEAT)
             + "Second stage of the two-stage lift on the bottom layers."},
        {.sources = {"normalDropSpeed"},
         .tilt    = {Transform::None, "", MappingStatus::NotApplicable},
         .generic =
             {Transform::MmPerMinToMmPerSec,
              "retract_speed",
              MappingStatus::Converted,
              UNIT_MM_PER_SEC},
         .source_unit = UNIT_MM_PER_MIN,
         .note        = std::string(SPEED_UNIT_CAVEAT) + "The drop speed is the retract speed."},
        {.sources = {"normalDropSpeed2"},
         .tilt    = {Transform::None, "", MappingStatus::NotApplicable},
         .generic =
             {Transform::MmPerMinToMmPerSec,
              "retract_speed_2",
              MappingStatus::Converted,
              UNIT_MM_PER_SEC},
         .source_unit = UNIT_MM_PER_MIN,
         .note        = std::string(SPEED_UNIT_CAVEAT)
             + "Second stage of the two-stage drop, above the area fill threshold."},

        {.sources = {"normalLightIntensityPWM"},
         .tilt    = {Transform::None, "", MappingStatus::NotApplicable},
         .generic = {Transform::CopyInt, "light_pwm", MappingStatus::Exact},
         .note =
             "A tilt printer drives its LEDs from the machine calibration, it has no resin PWM."},
        {.sources = {"bottomLightIntensityPWM"},
         .tilt    = {Transform::None, "", MappingStatus::NotApplicable},
         .generic = {Transform::CopyInt, "bottom_light_pwm", MappingStatus::Exact},
         .note =
             "A tilt printer drives its LEDs from the machine calibration, it has no resin PWM."},

        // Shown for information, never a material setting.
        {.sources =
             {"bAntiAliasing", "antiAliasLevel", "bImageBlur", "minGreyLevel", "maxGreyLevel"},
         .tilt    = {Transform::None, "", MappingStatus::NotApplicable},
         .generic = {Transform::None, "", MappingStatus::NotApplicable},
         .note =
             "A preview image setting; PrusaSLA takes the grey levels from the printer calibration."},

        // The name of the profile and what it was written for: used to suggest a printer and a
        // preset name, never written to the material.
        {.sources = {"currProfile"},
         .tilt    = {Transform::SuggestedName, "", MappingStatus::Converted},
         .generic = {Transform::SuggestedName, "", MappingStatus::Converted},
         .note    = "Used as the suggested preset name; never written to the material."},
        {.sources =
             {"resolutionX",
              "resolutionY",
              "machineWidth",
              "machineDepth",
              "machineHeight",
              "projectType",
              "machineType",
              "machineName",
              "printerName",
              "name"},
         .tilt    = {Transform::PrinterHint, "", MappingStatus::Converted},
         .generic = {Transform::PrinterHint, "", MappingStatus::Converted},
         .note =
             "A printer hint, used to suggest a matching printer; never written to the material."},

        // Never imported.
        {.sources = {"startGcode", "layerGcode", "endGcode"},
         .tilt    = {Transform::None, "", MappingStatus::NotApplicable},
         .generic = {Transform::None, "", MappingStatus::NotApplicable},
         .note    = "Foreign G-code is never imported, it belongs to the machine profile."},

        // The bookkeeping an SL1 archive carries in its config.ini: what the job was, how long it
        // was estimated to take, which machine and slicer wrote it, and the counts that describe
        // the print rather than set it up. Reported so the review does not read as a wall of
        // unknown keys, never written.
        {
            .sources =
                {"jobDir", "action", "printerModel", "printerVariant", "expUserProfile",
                 "usedMaterial", "numSlow", "numFast", "hollow", "printTime",
                 "fileCreationTimestamp", "prusaSlicerVersion", "usedMaterialName"},
            .tilt    = {Transform::None, "", MappingStatus::NotApplicable},
            .generic = {Transform::None, "", MappingStatus::NotApplicable},
            .note =
                "A statistic or a job record of the archive the print came from; a resin preset is "
                "not where the history of one print belongs."
        },
        {.prefixes = {"displayCorrect", "buildAreaOffset"},
         .tilt     = {Transform::None, "", MappingStatus::NotApplicable},
         .generic  = {Transform::None, "", MappingStatus::NotApplicable},
         .note =
             "A display or build area correction belongs to the printer calibration; never imported."},
    };
    return table;
}

} // namespace

MappingResult
map_resin_profile(const ForeignResinProfile& profile, TargetPrinterClass printer_class)
{
    MappingResult result;
    std::set<std::string> reported;

    for (const Rule& rule : mapping_table()) {
        const Target& target = printer_class == TargetPrinterClass::Tilt ? rule.tilt : rule.generic;

        // The keys this rule reads, in a stable order: the spellings of the table first, then the
        // keys matching one of its prefixes.
        std::vector<std::string> keys;
        for (const std::string& source : rule.sources)
            if (profile.raw_values.count(source))
                keys.push_back(source);
        for (const std::string& prefix : rule.prefixes)
            for (const auto& entry : profile.raw_values)
                if (entry.first.starts_with(prefix))
                    keys.push_back(entry.first);

        bool wrote = false;
        for (std::size_t i = 0; i < keys.size(); ++i) {
            const std::string& key = keys[i];
            reported.insert(key);
            const std::string& value = profile.raw_values.at(key);

            if (i > 0 && !rule.sources.empty()) {
                // Another spelling of the same setting is in the file: the first one is the row's.
                const std::string note = "Another spelling of this setting ("
                    + keys.front()
                    + ") is in the file"
                    + (wrote ? " and was used instead." : "; see that row.");
                result.report.push_back(
                    {.source_key   = key,
                     .source_value = value,
                     .source_unit  = rule.source_unit,
                     .target_key   = {},
                     .value        = {},
                     .status       = MappingStatus::NotApplicable,
                     .note         = note}
                );
                continue;
            }

            const Applied applied = apply(target.transform, profile, value, rule.note);
            if (target.transform == Transform::SuggestedName && result.suggested_name.empty())
                result.suggested_name = trim(value);

            if (!applied.value || target.key.empty()) {
                result.report.push_back(
                    {.source_key   = key,
                     .source_value = value,
                     .source_unit  = rule.source_unit,
                     .target_key   = {},
                     .value        = {},
                     .status       = target.status,
                     .note         = applied.note}
                );
                continue;
            }
            if (result.material_values.count(target.key)) {
                // Two rules want the same material key (the transition layer count and the bottom
                // layer count on a tilt printer, for example). The first one keeps it.
                result.report.push_back(
                    {.source_key   = key,
                     .source_value = value,
                     .source_unit  = rule.source_unit,
                     .target_key   = {},
                     .value        = {},
                     .status       = target.status,
                     .note         = applied.note
                         + " "
                         + target.key
                         + " already carries another value, this one was not written."}
                );
                continue;
            }
            result.material_values[target.key] = *applied.value;
            wrote                              = true;
            result.report.push_back(
                {.source_key   = key,
                 .source_value = value,
                 .source_unit  = rule.source_unit,
                 .target_key   = target.key,
                 .value        = *applied.value,
                 .target_unit  = target.unit,
                 .status       = target.status,
                 .note         = applied.note}
            );
        }
    }

    // Everything the table does not know about is reported, never imported.
    for (const auto& [key, value] : profile.raw_values) {
        if (reported.count(key))
            continue;
        // The value is a field of the row of its own, so the note does not have to repeat it: a key
        // the table does not know carries no unit either, and none is guessed here.
        result.report.push_back(
            {.source_key   = key,
             .source_value = value,
             .target_key   = {},
             .value        = {},
             .status       = MappingStatus::Unknown,
             .note         = "Not a PrusaSLA resin setting; kept in the report only."}
        );
    }

    if (result.suggested_name.empty() && profile.printer_hint)
        result.suggested_name = trim(*profile.printer_hint);

    return result;
}

TargetPrinterClass printer_class_from_config(const Domain::ConfigView& printer_config)
{
    const auto& values = printer_config.values();

    // The printer model decides, and only the Prusa machines that separate the layers by tilting
    // are Tilt printers - the same list the engine uses in SLAPrint::is_prusa_print(). Every other
    // model (Anycubic, Elegoo, ...) lifts the build plate, so it is a generic MSLA printer.
    if (const auto it = values.find("printer_model");
        it != values.end() && it->second.holds_alternative<std::string>())
    {
        const std::string& model = it->second.get<std::string>();
        if (!model.empty()) {
            static const std::vector<std::string> prusa_tilt_models{ "SL1", "SL1S", "M1", "SLX" };
            const bool tilts = std::ranges::any_of(
                prusa_tilt_models,
                [&model](const std::string& tilt_model) {
                    return contains_ignore_case(model, tilt_model);
                }
            );
            return tilts ? TargetPrinterClass::Tilt : TargetPrinterClass::GenericMsla;
        }
    }

    // No model to go by. A view always carries use_tilt (its default is on), so it says nothing
    // about an unknown printer either - it is the fallback for a settings set that names no model.
    if (const auto it = values.find("use_tilt"); it != values.end()) {
        if (it->second.holds_alternative<std::vector<bool>>()) {
            const std::vector<bool>& tilt = it->second.get<std::vector<bool>>();
            return std::ranges::any_of(tilt, [](bool uses_tilt) { return uses_tilt; }) ?
                TargetPrinterClass::Tilt :
                TargetPrinterClass::GenericMsla;
        }
        if (it->second.holds_alternative<bool>())
            return it->second.get<bool>() ?
                TargetPrinterClass::Tilt :
                TargetPrinterClass::GenericMsla;
    }

    // Neither model nor use_tilt: this fork is an SL1, so assume the tilt machine.
    return TargetPrinterClass::Tilt;
}

} // namespace Slic3r::Biz::ResinProfile
