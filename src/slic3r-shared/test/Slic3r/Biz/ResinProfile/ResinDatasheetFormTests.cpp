#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/ResinProfile/ResinDatasheetForm.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileImportInteractor.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileImportTestFixture.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileMapper.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::Biz;
using namespace Slic3r::Biz::ResinProfile;

namespace {

/// @brief A field of the form and what it is called, so a failing case says which one it was.
struct NamedField
{
    const char* name;
    std::string ResinDatasheet::* value;
};

/// @brief A number field of the form, the key the mapping table reads it under, and the resin
/// setting it is written to. The mirror of the rules in ResinDatasheetForm.cpp, so a rule that
/// names a setting that does not exist, or not the one the mapping table writes, is a failing case.
struct OptionField
{
    const char* label;
    const char* key;
    const char* option;
    /// How many units of @ref option one unit of the form is worth: the speeds are asked in mm/min
    /// and the setting holds mm/s, everything else is one.
    double to_option_unit;
    /// Whether the mapping table writes the setting, or only reads the value with another one. A
    /// bottle volume is the latter: it turns a per-litre price into a bottle cost and is written
    /// nowhere of its own.
    bool written;
    /// A key the mapping table reads together with @ref key, where the value only means something
    /// next to it: a price is only a bottle cost when the unit says it is a per-litre price.
    const char* companion_key;
    const char* companion_value;
    std::string ResinDatasheet::* value;
};

/// @brief Every number field of the form, with the setting the mapping table writes it to.
const std::vector<OptionField>& option_fields()
{
    static const std::vector<OptionField> fields{
        {"Layer height",
         "layerHeight",
         "resin_layer_height",
         1.,
         true,
         nullptr,
         nullptr,
         &ResinDatasheet::layer_height_mm},
        {"Normal exposure time",
         "normalExposureTime",
         "exposure_time",
         1.,
         true,
         nullptr,
         nullptr,
         &ResinDatasheet::normal_exposure_s},
        {"Bottom layer exposure time",
         "bottomLayerExposureTime",
         "initial_exposure_time",
         1.,
         true,
         nullptr,
         nullptr,
         &ResinDatasheet::bottom_exposure_s},
        {"Number of bottom layers",
         "bottomLayerCount",
         "bottom_layer_count",
         1.,
         true,
         nullptr,
         nullptr,
         &ResinDatasheet::bottom_layer_count},
        {"Light-off delay",
         "lightOffTime",
         "delay_before_exposure",
         1.,
         true,
         nullptr,
         nullptr,
         &ResinDatasheet::light_off_delay_s},
        // A price is a bottle cost only when the unit says how it is counted, so the unit comes
        // along: without it the mapping table writes nothing and the row has no target. The one the
        // form writes is "/bottle" (M3.11c), a per-litre one lands in the same setting.
        {"Price of a bottle",
         "resinPrice",
         "bottle_cost",
         1.,
         true,
         "resinUnit",
         "/bottle",
         &ResinDatasheet::price_per_bottle},
        {"Bottle volume",
         "bottleVolume",
         "bottle_volume",
         1.,
         false,
         nullptr,
         nullptr,
         &ResinDatasheet::bottle_volume_ml},
        {"Lift distance",
         "normalLayerLiftHeight",
         "lift_height",
         1.,
         true,
         nullptr,
         nullptr,
         &ResinDatasheet::lift_distance_mm},
        // The speeds are asked in mm/min and written in mm/s, so the limit of the setting is
        // compared against the converted number, not against what was typed.
        {"Lift speed",
         "normalLayerLiftSpeed",
         "lift_speed",
         1. / 60.,
         true,
         nullptr,
         nullptr,
         &ResinDatasheet::lift_speed_mm_min},
        {"Retract speed",
         "normalDropSpeed",
         "retract_speed",
         1. / 60.,
         true,
         nullptr,
         nullptr,
         &ResinDatasheet::retract_speed_mm_min},
        {"Number of transition layers",
         "transitionLayers",
         "resin_faded_layers",
         1.,
         true,
         nullptr,
         nullptr,
         &ResinDatasheet::transition_layer_count},
    };
    return fields;
}

/// @brief The definition of a resin setting of this build, so a case reads the limits out of the
/// config rather than repeating numbers that ConfigDefsSLA.cpp could change. Nothing when this build
/// has no such setting, which is what the test that walks the fields reports.
const Domain::ConfigItemDef* option_of(const char* name)
{
    for (const Domain::ConfigItemDef& def : Domain::get_defs_sla().defs())
        if (def.name == name)
            return &def;
    return nullptr;
}

/// @brief A profile written by hand from a key/value map, the way a reader would hand it over.
ForeignResinProfile profile_of(std::map<std::string, std::string> values)
{
    ForeignResinProfile profile;
    profile.source_format = "chitubox-cfg";
    profile.raw_values    = std::move(values);
    return profile;
}

/// @brief A number the way the config and the message spell it: six significant digits, no trailing
/// zeros.
std::string as_number(double value)
{
    return fmt::format("{:g}", value);
}

/// @brief The four settings of the form a datasheet cannot leave out.
const std::vector<NamedField>& required_fields()
{
    static const std::vector<NamedField> fields{
        {"layer height", &ResinDatasheet::layer_height_mm},
        {"normal exposure", &ResinDatasheet::normal_exposure_s},
        {"bottom exposure", &ResinDatasheet::bottom_exposure_s},
        {"bottom layer count", &ResinDatasheet::bottom_layer_count},
    };
    return fields;
}

/// @brief A datasheet as a user would type it: what a vendor states for a grey resin, in the units
/// the datasheets use.
ResinDatasheet filled_datasheet()
{
    ResinDatasheet datasheet;
    datasheet.resin_name         = "Grey resin";
    datasheet.vendor             = "Anycubic";
    datasheet.layer_height_mm    = "0.05";
    datasheet.normal_exposure_s  = "2.5";
    datasheet.bottom_exposure_s  = "30";
    datasheet.bottom_layer_count = "8";
    return datasheet;
}

/// @brief The same form with the optional fields filled in as well: the delay and the price, and
/// the layer separation of a printer that lifts the build plate.
ResinDatasheet full_datasheet()
{
    ResinDatasheet datasheet         = filled_datasheet();
    datasheet.light_off_delay_s      = "1";
    datasheet.price_per_bottle       = "25";
    datasheet.bottle_volume_ml       = "500";
    datasheet.lift_distance_mm       = "6";
    datasheet.lift_speed_mm_min      = "60";
    datasheet.retract_speed_mm_min   = "180";
    datasheet.transition_layer_count = "10";
    return datasheet;
}

} // namespace

TEST_CASE("validate_datasheet accepts a datasheet a vendor would print", "[resin_datasheet]")
{
    SECTION("the six settings alone are enough")
    {
        CHECK(validate_datasheet(filled_datasheet()).empty());
    }

    SECTION("the optional ones may be there")
    {
        CHECK(validate_datasheet(full_datasheet()).empty());
    }

    SECTION("a number of a datasheet may be written with spaces around it")
    {
        ResinDatasheet datasheet     = full_datasheet();
        datasheet.resin_name         = "  Grey resin  ";
        datasheet.layer_height_mm    = " 0.05 ";
        datasheet.bottom_layer_count = " 8 ";
        CHECK(validate_datasheet(datasheet).empty());
    }
}

TEST_CASE("validate_datasheet refuses what cannot become a resin setting", "[resin_datasheet]")
{
    SECTION("no name to save the profile under")
    {
        ResinDatasheet datasheet = filled_datasheet();
        datasheet.resin_name     = "";
        CHECK_FALSE(validate_datasheet(datasheet).empty());

        datasheet.resin_name = "   ";
        CHECK_FALSE(validate_datasheet(datasheet).empty());
    }

    SECTION("a required number that is missing")
    {
        for (const NamedField& field : required_fields()) {
            INFO("field " << field.name);
            ResinDatasheet datasheet = filled_datasheet();
            datasheet.*(field.value) = "";
            CHECK_FALSE(validate_datasheet(datasheet).empty());
        }
    }

    SECTION("a required number that is not a positive number")
    {
        for (const NamedField& field : required_fields()) {
            for (const std::string& value :
                 {"0", "-0.05", "0.05 mm", "half a millimetre", "nan", "inf"})
            {
                INFO("field " << field.name << " value " << value);
                ResinDatasheet datasheet = filled_datasheet();
                datasheet.*(field.value) = value;
                CHECK_FALSE(validate_datasheet(datasheet).empty());
            }
        }
    }

    SECTION("a layer count is a count, not a measurement")
    {
        ResinDatasheet datasheet     = filled_datasheet();
        datasheet.bottom_layer_count = "8.5";
        CHECK_FALSE(validate_datasheet(datasheet).empty());

        datasheet.bottom_layer_count = "0";
        CHECK_FALSE(validate_datasheet(datasheet).empty());

        datasheet.bottom_layer_count = "-4";
        CHECK_FALSE(validate_datasheet(datasheet).empty());

        // A datasheet that rounds to a whole number is fine: the count is what the number says.
        datasheet.bottom_layer_count = "8.0";
        CHECK(validate_datasheet(datasheet).empty());
    }

    SECTION("an optional field may be left empty, but not filled with nonsense")
    {
        ResinDatasheet datasheet    = full_datasheet();
        datasheet.light_off_delay_s = "";
        datasheet.price_per_bottle  = "";
        datasheet.bottle_volume_ml  = "";
        CHECK(validate_datasheet(datasheet).empty());

        // A resin with no light-off delay and a bottle that came free are values a datasheet states.
        datasheet.light_off_delay_s = "0";
        datasheet.price_per_bottle  = "0";
        CHECK(validate_datasheet(datasheet).empty());

        datasheet.light_off_delay_s = "-1";
        CHECK_FALSE(validate_datasheet(datasheet).empty());

        datasheet.light_off_delay_s = "1";
        datasheet.price_per_bottle  = "-25";
        CHECK_FALSE(validate_datasheet(datasheet).empty());

        // A bottle of no resin is not a bottle.
        datasheet.price_per_bottle = "25";
        datasheet.bottle_volume_ml = "0";
        CHECK_FALSE(validate_datasheet(datasheet).empty());

        datasheet.bottle_volume_ml = "half a litre";
        CHECK_FALSE(validate_datasheet(datasheet).empty());
    }
}

TEST_CASE(
    "the optional layer separation of a generic MSLA datasheet is accepted",
    "[resin_datasheet]"
)
{
    SECTION("it may be left out entirely")
    {
        ResinDatasheet datasheet = filled_datasheet();
        CHECK(validate_datasheet(datasheet).empty());
    }

    SECTION("a lift distance, a lift speed and a retract speed are positive numbers")
    {
        const std::vector<NamedField>& fields{
            {"lift distance", &ResinDatasheet::lift_distance_mm},
            {"lift speed", &ResinDatasheet::lift_speed_mm_min},
            {"retract speed", &ResinDatasheet::retract_speed_mm_min},
        };
        for (const NamedField& field : fields) {
            INFO("field " << field.name);
            ResinDatasheet datasheet = full_datasheet();
            // A lift of nothing separates no layers, so these have no zero, the way an exposure
            // has none either.
            datasheet.*(field.value) = "0";
            CHECK_FALSE(validate_datasheet(datasheet).empty());

            datasheet.*(field.value) = "-1";
            CHECK_FALSE(validate_datasheet(datasheet).empty());

            datasheet.*(field.value) = "6 mm/min";
            CHECK_FALSE(validate_datasheet(datasheet).empty());

            datasheet.*(field.value) = "6";
            CHECK(validate_datasheet(datasheet).empty());
        }
    }

    SECTION("the transition layer count is a count, like the bottom layer count")
    {
        ResinDatasheet datasheet         = full_datasheet();
        datasheet.transition_layer_count = "4.5";
        CHECK_FALSE(validate_datasheet(datasheet).empty());

        datasheet.transition_layer_count = "0";
        CHECK_FALSE(validate_datasheet(datasheet).empty());

        // A datasheet that rounds to a whole number is fine: the count is what the number says.
        datasheet.transition_layer_count = "4.0";
        CHECK(validate_datasheet(datasheet).empty());
    }
}

TEST_CASE(
    "every field of the form is written to a resin setting that declares a range",
    "[resin_datasheet]"
)
{
    // A rule that named a setting this build does not have would check nothing, and a value outside
    // what the setting takes would then be written and shown in the review table. Walking the fields
    // is what keeps that from being silent.
    for (const OptionField& field : option_fields()) {
        INFO("field " << field.label);
        const Domain::ConfigItemDef* def = option_of(field.option);
        REQUIRE(def != nullptr);
        // A setting of a resin preset, never a printer or a print one.
        CHECK(Domain::get_location_name(def->location) == "sla_material_settings");
        // A setting with no minimum would be a field nothing checks on the low side.
        CHECK(def->min.has_value());
    }
}

TEST_CASE(
    "every field of the form is checked against the setting the mapping table writes it to",
    "[resin_datasheet][mapper]"
)
{
    // The limits are only the right ones if they are read off the setting the value really lands
    // in, so the mapping table is asked where each key of the form goes and has to agree with what
    // the rule says. The mapping runs for a generic MSLA printer, which is the one that takes the
    // layer separation; a tilt printer writes none of those, which its own rows say.
    for (const OptionField& field : option_fields()) {
        INFO("field " << field.label);
        // A value the setting takes, so the row is written rather than refused for its content, and
        // the speeds are the other way round: one unit of the setting is what the form asks in.
        std::map<std::string, std::string> values{{field.key, as_number(1. / field.to_option_unit)}};
        if (field.companion_key != nullptr)
            values[field.companion_key] = field.companion_value;

        const MappingResult mapping =
            map_resin_profile(profile_of(std::move(values)), TargetPrinterClass::GenericMsla);

        const MappedField* row = nullptr;
        for (const MappedField& candidate : mapping.report)
            if (candidate.source_key == field.key)
                row = &candidate;
        REQUIRE(row != nullptr);

        if (field.written) {
            CHECK(row->target_key == field.option);
        } else {
            // A bottle volume is read with the price and written nowhere of its own, so the setting
            // it is checked against is the one a resin preset holds a bottle's size in rather than
            // the one its row names.
            CHECK(row->target_key.empty());
        }
    }
}

TEST_CASE(
    "validate_datasheet refuses a number the setting it is written to does not take",
    "[resin_datasheet]"
)
{
    // The form used to ask only whether a number was a positive number, so a datasheet outside the
    // range of its setting was written and shown. The range comes from the option itself, and the
    // message names the field and the limit, so the user knows which number to change.
    for (const OptionField& field : option_fields()) {
        const Domain::ConfigItemDef* def = option_of(field.option);
        REQUIRE(def != nullptr);
        if (!def->max)
            continue;

        INFO("field " << field.label);
        // The limit of the setting is in the unit the setting holds, so the value that breaks it is
        // that limit typed in the unit of the form.
        const double typed_max   = *def->max / field.to_option_unit;
        const std::string limit  = as_number(*def->max);
        const std::string unit   = def->units.empty() ? std::string{} : def->units.front();
        const std::string number = unit.empty() ? limit : limit + " " + unit;

        ResinDatasheet datasheet = full_datasheet();
        datasheet.*(field.value) = as_number(typed_max * 2.);
        const std::string error  = validate_datasheet(datasheet);
        CHECK_FALSE(error.empty());
        // The message names the field the way the form calls it, and the limit that was broken.
        CHECK(error.find(field.label) != std::string::npos);
        CHECK(error.find(number) != std::string::npos);

        // The limit itself is inside the range, so a datasheet that states exactly what the setting
        // takes is accepted.
        datasheet.*(field.value) = as_number(typed_max);
        CHECK(validate_datasheet(datasheet).empty());
    }
}

TEST_CASE("validate_datasheet refuses a bottle smaller than the setting takes", "[resin_datasheet]")
{
    // The other half of a range: bottle_volume has a minimum and no maximum, so the message is the
    // one that names the smallest bottle there is.
    const Domain::ConfigItemDef* def = option_of("bottle_volume");
    REQUIRE(def != nullptr);
    REQUIRE(def->min.has_value());

    ResinDatasheet datasheet   = full_datasheet();
    datasheet.bottle_volume_ml = as_number(*def->min / 2.);
    const std::string error    = validate_datasheet(datasheet);
    CHECK_FALSE(error.empty());
    CHECK(error.find("Bottle volume") != std::string::npos);
    CHECK(error.find(as_number(*def->min)) != std::string::npos);

    // A bottle of exactly the smallest size is what the setting takes, so it is accepted.
    datasheet.bottle_volume_ml = as_number(*def->min);
    CHECK(validate_datasheet(datasheet).empty());
}

TEST_CASE("datasheet_to_profile writes the values the mapping table reads", "[resin_datasheet]")
{
    SECTION("the format says where the values came from")
    {
        const ForeignResinProfile profile = datasheet_to_profile(full_datasheet());
        CHECK(profile.source_format == SOURCE_FORMAT_DATASHEET);
        // A form has no file behind it, so the profile names none.
        CHECK(profile.source_path.empty());
        CHECK(profile.material.material_name.value_or(std::string{}) == "Grey resin");
        CHECK(profile.material.material_vendor.value_or(std::string{}) == "Anycubic");
    }

    SECTION("every value goes under the key of the mapping table it belongs to")
    {
        const ForeignResinProfile profile = datasheet_to_profile(full_datasheet());
        CHECK(profile.raw_values.at("layerHeight") == "0.05");
        CHECK(profile.raw_values.at("normalExposureTime") == "2.5");
        CHECK(profile.raw_values.at("bottomLayerExposureTime") == "30");
        CHECK(profile.raw_values.at("bottomLayerCount") == "8");
        CHECK(profile.raw_values.at("lightOffTime") == "1");
        // The name is written the way a Chitubox file writes it, so the mapper suggests the preset
        // name from it and the importer looks for a resin of that name to use as the base.
        CHECK(profile.raw_values.at("currProfile") == "Grey resin");
        // The price of a bottle goes in as the price of a bottle, which is what bottle_cost holds,
        // so the review table shows the number that was typed rather than one the form had to make
        // up. 25 for a 500 ml bottle is 50 per litre, and that is what the note of the row carries.
        CHECK(profile.raw_values.at("resinPrice") == "25");
        CHECK(profile.raw_values.at("resinUnit") == "/bottle");
        CHECK(profile.raw_values.at("bottleVolume") == "500");
        // The layer separation goes in under the keys a foreign .cfg states it with, and the speeds
        // in the unit that file uses, so the mapper converts them the way it converts a file's.
        CHECK(profile.raw_values.at("normalLayerLiftHeight") == "6");
        CHECK(profile.raw_values.at("normalLayerLiftSpeed") == "60");
        CHECK(profile.raw_values.at("normalDropSpeed") == "180");
        CHECK(profile.raw_values.at("transitionLayers") == "10");
    }

    SECTION("a setting the datasheet does not state is left out, not written as a zero")
    {
        const ForeignResinProfile profile = datasheet_to_profile(filled_datasheet());
        CHECK(profile.raw_values.count("lightOffTime") == 0);
        CHECK(profile.raw_values.count("resinPrice") == 0);
        CHECK(profile.raw_values.count("resinUnit") == 0);
        CHECK(profile.raw_values.count("bottleVolume") == 0);
        // The lift, the retract and the transition layers are optional as well: a datasheet of a
        // printer that lifts states them, a datasheet of a resin does not have to.
        CHECK(profile.raw_values.count("normalLayerLiftHeight") == 0);
        CHECK(profile.raw_values.count("normalLayerLiftSpeed") == 0);
        CHECK(profile.raw_values.count("normalDropSpeed") == 0);
        CHECK(profile.raw_values.count("transitionLayers") == 0);
        // The vendor is optional too, and it is not a printer hint either.
        CHECK(profile.raw_values.count("machineName") == 0);
    }

    SECTION("a price without a bottle size is still the price of a bottle")
    {
        ResinDatasheet datasheet          = full_datasheet();
        datasheet.bottle_volume_ml        = "";
        const ForeignResinProfile profile = datasheet_to_profile(datasheet);
        // The price no longer depends on the bottle at all, so nothing about the price changes with
        // it; the per-litre equivalent the note gives is the assumed 1 litre bottle, as it is for a
        // file that states no bottle size either.
        CHECK(profile.raw_values.at("resinPrice") == "25");
        CHECK(profile.raw_values.at("resinUnit") == "/bottle");
        CHECK(profile.raw_values.count("bottleVolume") == 0);
    }
}

TEST_CASE(
    "the review row of the price shows the price the datasheet stated",
    "[resin_datasheet][mapper]"
)
{
    // The row used to name a per-litre price the form had computed, so the number on the left was
    // not the one that was typed. It is now the price as stated, and the conversion is in the note.
    const MappingResult mapping =
        map_resin_profile(datasheet_to_profile(full_datasheet()), TargetPrinterClass::GenericMsla);

    const MappedField* price_row = nullptr;
    for (const MappedField& row : mapping.report)
        if (row.source_key == "resinPrice")
            price_row = &row;
    REQUIRE(price_row != nullptr);

    // What the datasheet said: 25 for a 500 ml bottle.
    CHECK(price_row->source_value == "25");
    // What is written is the same number, because a price per bottle is what bottle_cost holds.
    CHECK(price_row->target_key == "bottle_cost");
    CHECK(price_row->value == "25");
    // The per-litre equivalent the row does not show any more is in its note, so the conversion
    // stays auditable.
    CHECK(price_row->note.find("50") != std::string::npos);
    CHECK(price_row->note.find("per litre") != std::string::npos);
    CHECK(price_row->note.find("500") != std::string::npos);

    // A price per bottle is a price this preset already holds, so the unit says so rather than
    // naming a per-litre price the source never gave.
    const MappedField* unit_row = nullptr;
    for (const MappedField& row : mapping.report)
        if (row.source_key == "resinUnit")
            unit_row = &row;
    REQUIRE(unit_row != nullptr);
    CHECK(unit_row->source_value == "/bottle");
}

TEST_CASE(
    "a datasheet profile goes through the same mapping as a file (dry run)",
    "[resin_datasheet][import]"
)
{
    Slic3r::Test::ResinImportFixture fx;
    ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor);

    const ResinProfile::ResinImportResult result = interactor.import_profile(
        datasheet_to_profile(full_datasheet()),
        fx.target(),
        /*dry_run=*/true
    );

    REQUIRE(result.ok);
    // What the profile says about itself, for the source summary of the review dialog.
    CHECK(result.source_format == SOURCE_FORMAT_DATASHEET);
    CHECK(result.resin_name == "Grey resin");
    CHECK(result.resin_vendor == "Anycubic");
    // The printer takes a resin, so a system one is the base, and the name the user typed is what
    // the preset would be saved under.
    CHECK(result.base_preset == Slic3r::Test::fallback_resin);
    CHECK(result.preset_name == "Grey resin");
    // The printer of the test bundle lifts the build plate, so the bottom layer count is kept and
    // not turned into the transition layer count of a tilt printer.
    CHECK(result.mapping.material_values.at("exposure_time") == "2.5");
    CHECK(result.mapping.material_values.at("initial_exposure_time") == "30");
    CHECK(result.mapping.material_values.at("resin_layer_height") == "0.05");
    CHECK(result.mapping.material_values.at("bottom_layer_count") == "8");
    // The optional ones the material settings have keys for: a light-off delay and a bottle cost.
    CHECK(result.mapping.material_values.at("delay_before_exposure") == "1,1");
    CHECK(result.mapping.material_values.at("bottle_cost") == "25");
    // The layer separation the datasheet states, mapped through the same keys a .cfg is read with:
    // a distance in mm is written as it is and a speed in mm/min is converted to the mm/s the
    // setting holds. The transition layer count is the one both printer classes keep as it is.
    CHECK(result.mapping.material_values.at("lift_height") == "6");
    CHECK(result.mapping.material_values.at("lift_speed") == "1");
    CHECK(result.mapping.material_values.at("retract_speed") == "3");
    CHECK(result.mapping.material_values.at("resin_faded_layers") == "10");

    // Nothing the form states is lost: every key it wrote is a row of the review table.
    for (const std::string& key :
         {"layerHeight",
          "normalExposureTime",
          "bottomLayerExposureTime",
          "bottomLayerCount",
          "lightOffTime",
          "resinPrice",
          "resinUnit",
          "bottleVolume",
          "normalLayerLiftHeight",
          "normalLayerLiftSpeed",
          "normalDropSpeed",
          "transitionLayers",
          "currProfile"})
    {
        INFO("key " << key);
        const bool reported = std::ranges::any_of(
            result.mapping.report,
            [&key](const MappedField& row) { return row.source_key == key; }
        );
        CHECK(reported);
    }

    // A resin preset value has to be a material option of this build, never a printer or print one.
    for (const auto& [key, value] : result.mapping.material_values) {
        INFO("key " << key);
        CAPTURE(value);
        const Domain::ConfigItemDef* def = nullptr;
        for (const Domain::ConfigItemDef& candidate : Domain::get_defs_sla().defs())
            if (candidate.name == key)
                def = &candidate;
        REQUIRE(def != nullptr);
        CHECK(Domain::get_location_name(def->location) == "sla_material_settings");
    }

    // A dry run writes nothing, exactly as for a file.
    CHECK(fx.material("Grey resin") == nullptr);
}

TEST_CASE("a datasheet profile saves a user resin preset", "[resin_datasheet][import]")
{
    Slic3r::Test::ResinImportFixture fx;
    ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor);

    const ResinProfile::ResinImportResult result =
        interactor.import_profile(datasheet_to_profile(full_datasheet()), fx.target());

    REQUIRE(result.ok);
    CHECK(result.preset_name == "Grey resin");

    const Domain::Preset::EvaluatedMaterialPreset::Preset* imported = fx.material("Grey resin");
    REQUIRE(imported != nullptr);
    CHECK(imported->origin == Domain::Preset::PresetOrigin::User);

    const Domain::ConfigItem* exposure = imported->config_box().items.find("exposure_time");
    REQUIRE(exposure != nullptr);
    CHECK(exposure->get<double>() == 2.5);

    const Domain::ConfigItem* bottle_cost = imported->config_box().items.find("bottle_cost");
    REQUIRE(bottle_cost != nullptr);
    CHECK(bottle_cost->get<double>() == 25.);

    // The bottle that cost is for is a resin setting of its own, so the price per litre of the
    // datasheet can be written back out of the two of them (M3.15b).
    const Domain::ConfigItem* bottle_volume = imported->config_box().items.find("bottle_volume");
    REQUIRE(bottle_volume != nullptr);
    CHECK(bottle_volume->get<double>() == 500.);

    // The layer separation the datasheet stated, mapped through the same keys a .cfg is read
    // with, so the speeds arrive as mm/s and the counts as counts.
    const Domain::ConfigItem* lift_height = imported->config_box().items.find("lift_height");
    REQUIRE(lift_height != nullptr);
    CHECK(lift_height->get<double>() == 6.);

    const Domain::ConfigItem* lift_speed = imported->config_box().items.find("lift_speed");
    REQUIRE(lift_speed != nullptr);
    CHECK(lift_speed->get<double>() == 1.);

    const Domain::ConfigItem* retract_speed = imported->config_box().items.find("retract_speed");
    REQUIRE(retract_speed != nullptr);
    CHECK(retract_speed->get<double>() == 3.);

    const Domain::ConfigItem* faded_layers =
        imported->config_box().items.find("resin_faded_layers");
    REQUIRE(faded_layers != nullptr);
    CHECK(faded_layers->get<int>() == 10);

    // The note says where the values came from: there is no file, so it names the datasheet and the
    // day it was entered, whose shape is all the test can know.
    const Domain::ConfigItem* note = imported->config_box().items.find("material_source_note");
    REQUIRE(note != nullptr);
    constexpr std::string_view note_prefix = "Datasheet, entered ";
    const std::string source_note          = note->get<std::string>();
    REQUIRE(source_note.starts_with(note_prefix));
    CHECK(Slic3r::Test::is_iso_date(source_note.substr(note_prefix.size())));

    // The save reloaded the vendor presets, so the work the import posted is delivered against the
    // re-resolved ones; in the app that happens on the next idle.
    fx.drain_main_thread_queue();
}
