#include "Slic3r/Biz/ResinProfile/ResinDatasheetForm.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"

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
/// table of the M3.6 mapper, so the review table names the field the way the datasheet does, and
/// the option is the resin setting the mapping table writes that key to, whose own <min, max> is
/// the range the value is checked against.
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
    /// The `sla_material_settings` key the value ends up in.
    const char* option;
    /// How many units of @ref option one unit of the form is worth: the speeds are asked in mm/min
    /// and the setting holds mm/s, and everything else is one.
    double to_option_unit;
};

/// The unit the mapping table converts a mm/min speed from. The same number the mapper divides by,
/// spelled here rather than included from the mapper so the form does not have to reach into it.
constexpr double MM_PER_MIN_IN_MM_PER_S = 60.;

const std::vector<FieldRule>& field_rules()
{
    static const std::vector<FieldRule> rules{
        {.key            = "layerHeight",
         .label          = "Layer height",
         .value          = &ResinDatasheet::layer_height_mm,
         .required       = true,
         .whole          = false,
         .allow_zero     = false,
         .option         = "resin_layer_height",
         .to_option_unit = 1.},
        {.key            = "normalExposureTime",
         .label          = "Normal exposure time",
         .value          = &ResinDatasheet::normal_exposure_s,
         .required       = true,
         .whole          = false,
         .allow_zero     = false,
         .option         = "exposure_time",
         .to_option_unit = 1.},
        {.key            = "bottomLayerExposureTime",
         .label          = "Bottom layer exposure time",
         .value          = &ResinDatasheet::bottom_exposure_s,
         .required       = true,
         .whole          = false,
         .allow_zero     = false,
         .option         = "initial_exposure_time",
         .to_option_unit = 1.},
        // A tilt printer takes this count as the number of layers the exposure is faded over and
        // clamps it into its own range, which the mapper does and the note of that row says; the
        // option checked here is the one a printer that lifts the build plate keeps the count in.
        {.key            = "bottomLayerCount",
         .label          = "Number of bottom layers",
         .value          = &ResinDatasheet::bottom_layer_count,
         .required       = true,
         .whole          = true,
         .allow_zero     = false,
         .option         = "bottom_layer_count",
         .to_option_unit = 1.},
        {.key            = "lightOffTime",
         .label          = "Light-off delay",
         .value          = &ResinDatasheet::light_off_delay_s,
         .required       = false,
         .whole          = false,
         .allow_zero     = true,
         .option         = "delay_before_exposure",
         .to_option_unit = 1.},
        {.key            = "resinPrice",
         .label          = "Price of a bottle",
         .value          = &ResinDatasheet::price_per_bottle,
         .required       = false,
         .whole          = false,
         .allow_zero     = true,
         .option         = "bottle_cost",
         .to_option_unit = 1.},
        // The mapping table only reads a bottle volume to turn a per-litre price into a bottle
        // cost, and writes none of its own; the option is the one that holds a bottle's size, and
        // it is the range a bottle of that size has to be in either way.
        {.key            = "bottleVolume",
         .label          = "Bottle volume",
         .value          = &ResinDatasheet::bottle_volume_ml,
         .required       = false,
         .whole          = false,
         .allow_zero     = false,
         .option         = "bottle_volume",
         .to_option_unit = 1.},
        // The layer separation of a printer that lifts the build plate, under the keys the .cfg of
        // the foreign slicer states them with, so the mapper maps them as it maps a file's.
        {.key            = "normalLayerLiftHeight",
         .label          = "Lift distance",
         .value          = &ResinDatasheet::lift_distance_mm,
         .required       = false,
         .whole          = false,
         .allow_zero     = false,
         .option         = "lift_height",
         .to_option_unit = 1.},
        {.key            = "normalLayerLiftSpeed",
         .label          = "Lift speed",
         .value          = &ResinDatasheet::lift_speed_mm_min,
         .required       = false,
         .whole          = false,
         .allow_zero     = false,
         .option         = "lift_speed",
         .to_option_unit = 1. / MM_PER_MIN_IN_MM_PER_S},
        {.key            = "normalDropSpeed",
         .label          = "Retract speed",
         .value          = &ResinDatasheet::retract_speed_mm_min,
         .required       = false,
         .whole          = false,
         .allow_zero     = false,
         .option         = "retract_speed",
         .to_option_unit = 1. / MM_PER_MIN_IN_MM_PER_S},
        {.key            = "transitionLayers",
         .label          = "Number of transition layers",
         .value          = &ResinDatasheet::transition_layer_count,
         .required       = false,
         .whole          = true,
         .allow_zero     = false,
         .option         = "resin_faded_layers",
         .to_option_unit = 1.},
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

/// @brief The definition of @p name, or nothing when this build has no such option. The rules name
/// the option each field is written to, and a name that is not there is a rule that checks nothing:
/// the test that walks every field against the defs is what notices.
const Domain::ConfigItemDef* option_def(const char* name)
{
    for (const Domain::ConfigItemDef& def : Domain::get_defs_sla().defs())
        if (def.name == name)
            return &def;
    return nullptr;
}

/// @brief A limit of an option the way a message names it: "50 ml", or the number alone when the
/// option carries no unit. The unit is the option's own text, already translated by the config.
std::string limit_text(double value, const std::string& unit)
{
    const std::string number = format_number(value);
    return unit.empty() ? number : number + " " + unit;
}

/// @brief The message for a value that is a number, but not one the option @p rule is written to
/// accepts, empty when it is one it does. It names the field the way the form does and the limit
/// that was broken, so the user knows which number to change and by how much; the unit is the
/// option's own text and is already translated by the config.
std::string range_message(const FieldRule& rule, double number)
{
    const Domain::ConfigItemDef* def = option_def(rule.option);
    // An option this build does not have, or one that declares no limit, accepts anything.
    if (!def)
        return {};
    const double in_option_unit = number * rule.to_option_unit;
    const std::string unit      = def->units.empty() ? std::string{} : def->units.front();
    // The value is below the minimum or above the maximum, never both, so asking about the minimum
    // first tells the two apart.
    if (def->min && in_option_unit < *def->min) {
        // TRN: A number of the "New resin from datasheet" form is below what the resin setting
        // accepts. {0} is the name of the field, {1} the smallest value the setting takes.
        return fmt::format(
            fmt::runtime(_u8L("{0} must be {1} or more.")),
            _u8L(rule.label),
            limit_text(*def->min, unit)
        );
    }
    if (def->max && in_option_unit > *def->max) {
        // TRN: A number of the "New resin from datasheet" form is above what the resin setting
        // accepts. {0} is the name of the field, {1} the largest value the setting takes.
        return fmt::format(
            fmt::runtime(_u8L("{0} must be {1} or less.")),
            _u8L(rule.label),
            limit_text(*def->max, unit)
        );
    }
    return {};
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
        const bool not_a_number_the_field_takes = rule.allow_zero ? *number < 0. : *number <= 0.;
        if (not_a_number_the_field_takes)
            return value_message(rule);
        // A number can be a perfectly good number and still be one the setting it is written to
        // does not take: a 45 s light-off delay, or a bottle of 10 ml. Refusing it here is what
        // keeps such a datasheet from being written and shown in the review table.
        const std::string out_of_the_setting_range = range_message(rule, *number);
        if (!out_of_the_setting_range.empty())
            return out_of_the_setting_range;
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

    // A datasheet states a price per bottle, which is what bottle_cost holds, so the price goes in as
    // it is and the unit says so. The mapping table writes it unchanged and puts the per-litre
    // equivalent of that bottle in the note of the row, so the review table shows the number that
    // was typed instead of one this form had to invent, and the conversion is still auditable.
    const std::optional<double> price  = parse_number(trim(datasheet.price_per_bottle));
    const std::optional<double> volume = parse_number(trim(datasheet.bottle_volume_ml));
    if (price) {
        profile.raw_values[std::string(PRICE_KEY)] = format_number(*price);
        // The unit is what tells the mapping table how the price is counted, and a per-bottle price
        // is one it can use as it is.
        profile.raw_values["resinUnit"] = "/bottle";
    }
    // The bottle size does not change the price any more, but it is what the per-litre equivalent in
    // the note is counted for, and it is a setting a resin preset holds of its own.
    if (volume && *volume > 0.)
        profile.raw_values[std::string(VOLUME_KEY)] = format_number(*volume);

    return profile;
}

} // namespace Slic3r::Biz::ResinProfile
