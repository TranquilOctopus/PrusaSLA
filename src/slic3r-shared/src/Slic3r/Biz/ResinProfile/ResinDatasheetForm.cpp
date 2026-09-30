#include "Slic3r/Biz/ResinProfile/ResinDatasheetForm.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include <boost/algorithm/string/trim.hpp>
#include <boost/lexical_cast.hpp>

#include <fmt/format.h>

#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r::Biz::ResinProfile {

namespace {

/// @brief One field of the form and what it is allowed to hold. The key is the one of the mapping
/// table of the M3.6 mapper, so the review table names the field the way the datasheet does.
struct FieldRule
{
    const char* key;
    /// How the field is called in the form, and in the message a wrong value gets.
    const char* label;
    /// The form field the value is typed into.
    const std::string ResinDatasheet::* value;
    /// Whether the datasheet cannot be used without it.
    bool required;
    /// Whether it is a count rather than a measurement, so a decimal in it is a mistake.
    bool whole;
    /// @brief Whether zero is a value a datasheet may state for it. A resin with no light-off delay
    /// and a bottle that came free are, an exposure of zero seconds and a bottle of no resin are
    /// not, and neither of those is a number the mapper can write.
    bool allow_zero;
};

const std::vector<FieldRule>& field_rules()
{
    static const std::vector<FieldRule> rules{
        {.key        = "layerHeight",
         .label      = "Layer height",
         .value      = &ResinDatasheet::layer_height_mm,
         .required   = true,
         .whole      = false,
         .allow_zero = false},
        {.key        = "normalExposureTime",
         .label      = "Normal exposure time",
         .value      = &ResinDatasheet::normal_exposure_s,
         .required   = true,
         .whole      = false,
         .allow_zero = false},
        {.key        = "bottomLayerExposureTime",
         .label      = "Bottom layer exposure time",
         .value      = &ResinDatasheet::bottom_exposure_s,
         .required   = true,
         .whole      = false,
         .allow_zero = false},
        {.key        = "bottomLayerCount",
         .label      = "Number of bottom layers",
         .value      = &ResinDatasheet::bottom_layer_count,
         .required   = true,
         .whole      = true,
         .allow_zero = false},
        {.key        = "lightOffTime",
         .label      = "Light-off delay",
         .value      = &ResinDatasheet::light_off_delay_s,
         .required   = false,
         .whole      = false,
         .allow_zero = true},
        {.key        = "resinPrice",
         .label      = "Price of a bottle",
         .value      = &ResinDatasheet::price_per_bottle,
         .required   = false,
         .whole      = false,
         .allow_zero = true},
        {.key        = "bottleVolume",
         .label      = "Bottle volume",
         .value      = &ResinDatasheet::bottle_volume_ml,
         .required   = false,
         .whole      = false,
         .allow_zero = false},
    };
    return rules;
}

std::string trim(const std::string& text)
{
    return boost::trim_copy(text);
}

/// @brief The number in @p text, or nothing when it is not one. The text is what a user typed, so
/// nothing is guessed: an expression is refused rather than evaluated here, and the spin buttons of
/// the fields are what write an expression's result back into the field.
std::optional<double> parse_number(const std::string& text)
{
    try {
        // lexical_cast reads "inf" and "nan" as the numbers they are, so they are refused here.
        const double value = boost::lexical_cast<double>(text);
        if (std::isfinite(value))
            return value;
    } catch (const boost::bad_lexical_cast&) {
    }
    return std::nullopt;
}

/// @brief The value @p rule has to hold, empty when the rule does not ask for one. A field that is
/// empty says the same thing as being missing for an optional field, so one message covers both.
std::string value_message(const FieldRule& rule)
{
    if (rule.whole) {
        // TRN: The bottom layer count of the "New resin from datasheet" form is not a whole number.
        // {0} is the name of the field.
        return fmt::format(
            fmt::runtime(_u8L("{0} must be a whole number greater than zero.")),
            _u8L(rule.label)
        );
    }
    if (rule.allow_zero) {
        // TRN: An optional number of the "New resin from datasheet" form is not a number. {0} is the
        // name of the field.
        return fmt::format(
            fmt::runtime(_u8L("{0} must be a number, zero or more, or left empty.")),
            _u8L(rule.label)
        );
    }
    // TRN: A number of the "New resin from datasheet" form is not a positive number. {0} is the name
    // of the field.
    return fmt::
        format(fmt::runtime(_u8L("{0} must be a number greater than zero.")), _u8L(rule.label));
}

/// @brief A number the way the config writes it into an .ini file: six significant digits and no
/// trailing zeros ("2.5", "1.66667"), which is what the mapper parses back.
std::string format_number(double value)
{
    return fmt::format("{:g}", value);
}

/// @brief @p value under @p key, or nothing at all when it is not a number. A field that is empty
/// is a setting the datasheet does not state, and a setting that is not stated is left out of the
/// profile rather than written as a zero.
void put_number(ForeignResinProfile& profile, const char* key, const std::string& value)
{
    const std::string text = trim(value);
    if (text.empty())
        return;
    if (parse_number(text))
        profile.raw_values[key] = text;
}

// The bottle the mapper assumes when a profile states no bottle size, the same 1 litre bottle it
// assumes for a Chitubox price per litre.
constexpr double ASSUMED_BOTTLE_VOLUME_ML = 1000.;

// The two keys the price block writes, which go in together rather than one field at a time.
constexpr std::string_view PRICE_KEY{"resinPrice"};
constexpr std::string_view VOLUME_KEY{"bottleVolume"};

} // namespace

std::string validate_datasheet(const ResinDatasheet& datasheet)
{
    // The name is what the preset is saved under, so an empty form has nothing to save.
    if (trim(datasheet.resin_name).empty())
        return _u8L("Enter the name of the resin.");

    for (const FieldRule& rule : field_rules()) {
        const std::string value = trim(datasheet.*(rule.value));
        if (value.empty()) {
            // An optional field the datasheet does not state is not an error; a required one is,
            // and the message for a wrong value is the one the user needs for an empty field too.
            if (rule.required)
                return value_message(rule);
            continue;
        }
        const std::optional<double> number = parse_number(value);
        if (!number)
            return value_message(rule);
        if (rule.whole && *number != std::round(*number))
            return value_message(rule);
        const bool out_of_range = rule.allow_zero ? *number < 0. : *number <= 0.;
        if (out_of_range)
            return value_message(rule);
    }

    return {};
}

ForeignResinProfile datasheet_to_profile(const ResinDatasheet& datasheet)
{
    ForeignResinProfile profile;
    profile.source_format = SOURCE_FORMAT_DATASHEET;

    // The name goes in under currProfile, the key a Chitubox file uses for it, because that is what
    // the mapper suggests the preset name from and what the importer looks for among the system
    // resins to pick as the base.
    const std::string name = trim(datasheet.resin_name);
    if (!name.empty()) {
        profile.raw_values["currProfile"] = name;
        profile.material.material_name    = name;
    }
    // The vendor is not a printer hint, so it stays out of raw_values: it is what the profile is
    // about, not what it is for, and the mapping table would read it as the latter.
    const std::string vendor = trim(datasheet.vendor);
    if (!vendor.empty())
        profile.material.material_vendor = vendor;

    for (const FieldRule& rule : field_rules()) {
        // The price and the bottle volume are written together, below.
        if (std::string_view(rule.key) == PRICE_KEY || std::string_view(rule.key) == VOLUME_KEY)
            continue;
        put_number(profile, rule.key, datasheet.*(rule.value));
    }

    // The mapping table only turns a per-litre price into a bottle cost, and a datasheet states a
    // price per bottle. So the price is written as the per-litre price of that same bottle:
    // bottle_cost = price x volume / 1000, and with the price written as the per-litre price of the
    // bottle the volume came in, that is the price of the bottle again. Both numbers are in the
    // mapping report, so the arithmetic stays auditable in the review table.
    const std::optional<double> price = parse_number(trim(datasheet.price_per_bottle));
    const std::optional<double> volume = parse_number(trim(datasheet.bottle_volume_ml));
    // A datasheet that states no bottle size gets the mapper's own assumption, which is what it
    // would assume for a file that states none. A bottle of no resin is not a bottle, so it is never
    // turned into a per-litre price either.
    const double volume_ml = volume && *volume > 0. ? *volume : ASSUMED_BOTTLE_VOLUME_ML;
    if (price) {
        profile.raw_values[std::string(PRICE_KEY)] = format_number(*price * 1000. / volume_ml);
        // The unit says how the price is counted, and it is what tells the mapping table that this
        // is a price it can turn into a bottle cost at all.
        profile.raw_values["resinUnit"] = "/L";
    }
    if (volume && *volume > 0.)
        profile.raw_values[std::string(VOLUME_KEY)] = format_number(*volume);

    return profile;
}

} // namespace Slic3r::Biz::ResinProfile
