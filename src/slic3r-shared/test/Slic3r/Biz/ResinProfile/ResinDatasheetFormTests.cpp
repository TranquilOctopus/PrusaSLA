#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/ResinProfile/ResinDatasheetForm.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileImportInteractor.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileImportTestFixture.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"

#include <algorithm>
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
        // The price of a bottle is written as the per-litre price of that bottle, because that is
        // the only price the mapping table turns into a bottle cost. 25 for 500 ml is 50 per litre.
        CHECK(profile.raw_values.at("resinPrice") == "50");
        CHECK(profile.raw_values.at("resinUnit") == "/L");
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

    SECTION("a price without a bottle size is the per-litre price of the assumed litre")
    {
        ResinDatasheet datasheet          = full_datasheet();
        datasheet.bottle_volume_ml        = "";
        const ForeignResinProfile profile = datasheet_to_profile(datasheet);
        CHECK(profile.raw_values.at("resinPrice") == "25");
        CHECK(profile.raw_values.count("bottleVolume") == 0);
    }
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
