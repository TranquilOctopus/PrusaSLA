#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/ResinProfile/ResinProfileMapper.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"

#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::Biz::ResinProfile;

namespace {

/// A profile written by hand from a key/value list, the way a reader would hand it over.
ForeignResinProfile profile_of(std::initializer_list<std::pair<const char*, const char*>> values)
{
    ForeignResinProfile profile;
    profile.source_format = "chitubox-cfg";
    for (const auto& [key, value] : values)
        profile.raw_values[key] = value;
    return profile;
}

const MappedField* find_row(const MappingResult& result, const std::string& source_key)
{
    for (const MappedField& row : result.report)
        if (row.source_key == source_key)
            return &row;
    return nullptr;
}

/// The report row of one source key. The key has to be there: a source key that never reaches the
/// report is the one thing the importer may not do.
const MappedField& row_of(const MappingResult& result, const std::string& source_key)
{
    const MappedField* row = find_row(result, source_key);
    REQUIRE(row != nullptr);
    return *row;
}

/// A printer settings view for a target printer. use_tilt is what a tilt material preset carries;
/// a generic MSLA printer has none, and then the model name is all there is.
Domain::ConfigView printer_view(std::optional<bool> use_tilt, const std::string& printer_model)
{
    Domain::ConfigPackSLA pack;
    if (use_tilt)
        pack.sla_material_settings.items.opt("use_tilt")
            .set(std::vector<bool>{*use_tilt, *use_tilt});
    if (!printer_model.empty())
        pack.sla_printer_settings.items.opt("printer_model").set(printer_model);
    auto full_config = std::make_shared<const Domain::FullConfigSLA>(
        pack,
        Domain::Preset::HwPrinterConfig{.technology = Domain::PrinterTechnology::SLA}
    );
    Domain::ConfigView view{std::move(full_config), {}};
    // Until finalize() runs, values() is empty and every lookup misses.
    view.finalize();
    return view;
}

Domain::ConfigView default_printer_view()
{
    auto full_config =
        std::make_shared<const Domain::FullConfigSLA>(Domain::FullConfigSLA::defaults());
    Domain::ConfigView view{std::move(full_config), {}};
    view.finalize();
    return view;
}

/// One row of the mapping table of doc/sla-fork/ROADMAP.md, for one printer class.
/// An empty target_key or written value means the row writes nothing.
struct ExpectedRow
{
    const char* source_key;
    const char* source_value;
    TargetPrinterClass printer_class;
    const char* target_key;
    const char* written_value;
    MappingStatus status;
};

const std::vector<ExpectedRow>& expected_rows()
{
    static const std::vector<ExpectedRow> rows{
        // normalExposureTime -> exposure_time
        {"normalExposureTime",
         "2.5",
         TargetPrinterClass::Tilt,
         "exposure_time",
         "2.5",
         MappingStatus::Exact},
        {"normalExposureTime",
         "2.5",
         TargetPrinterClass::GenericMsla,
         "exposure_time",
         "2.5",
         MappingStatus::Exact},

        // bottomLayerExposureTime | bottomLayExposureTime -> initial_exposure_time
        {"bottomLayerExposureTime",
         "30",
         TargetPrinterClass::Tilt,
         "initial_exposure_time",
         "30",
         MappingStatus::Exact},
        {"bottomLayExposureTime",
         "30",
         TargetPrinterClass::GenericMsla,
         "initial_exposure_time",
         "30",
         MappingStatus::Exact},

        // A transition layer count is a resin setting on both printer classes.
        {"transitionLayers",
         "8",
         TargetPrinterClass::Tilt,
         "resin_faded_layers",
         "8",
         MappingStatus::Exact},
        {"transitionLayers",
         "8",
         TargetPrinterClass::GenericMsla,
         "resin_faded_layers",
         "8",
         MappingStatus::Exact},

        // The bottom block: clamped into a fade on a tilt printer, a count on a generic MSLA one.
        {"bottomLayerCount",
         "6",
         TargetPrinterClass::Tilt,
         "resin_faded_layers",
         "6",
         MappingStatus::Approximated},
        {"bottomLayCount",
         "6",
         TargetPrinterClass::GenericMsla,
         "bottom_layer_count",
         "6",
         MappingStatus::Exact},

        // layerHeight is a resin setting now, not a preset variant condition.
        {"layerHeight",
         "0.05",
         TargetPrinterClass::Tilt,
         "resin_layer_height",
         "0.05",
         MappingStatus::Exact},
        {"layerHeight",
         "0.05",
         TargetPrinterClass::GenericMsla,
         "resin_layer_height",
         "0.05",
         MappingStatus::Exact},

        {"resinDensity",
         "1.12",
         TargetPrinterClass::Tilt,
         "material_density",
         "1.12",
         MappingStatus::Exact},
        {"resinDensity",
         "1.12",
         TargetPrinterClass::GenericMsla,
         "material_density",
         "1.12",
         MappingStatus::Exact},

        // Light-off time -> delay_before_exposure, the same below and above the area fill.
        {"lightOffTime",
         "3",
         TargetPrinterClass::Tilt,
         "delay_before_exposure",
         "3,3",
         MappingStatus::Approximated},
        {"bottomLightOffTime",
         "3.5",
         TargetPrinterClass::GenericMsla,
         "delay_before_exposure",
         "3.5,3.5",
         MappingStatus::Approximated},

        // The reset times around the lift: a delay after the exposure on a tilt printer.
        {"resetTimeBeforeLift",
         "1.5",
         TargetPrinterClass::Tilt,
         "delay_after_exposure",
         "1.5,1.5",
         MappingStatus::Approximated},
        {"resetTimeBeforeLift",
         "1.5",
         TargetPrinterClass::GenericMsla,
         "wait_before_lift",
         "1.5",
         MappingStatus::Exact},
        {"resetTimeAfterLift",
         "0.5",
         TargetPrinterClass::Tilt,
         "",
         "",
         MappingStatus::NotApplicable},
        {"resetTimeAfterLift",
         "0.5",
         TargetPrinterClass::GenericMsla,
         "wait_after_lift",
         "0.5",
         MappingStatus::Exact},

        // Layer separation by lift: a generic MSLA printer only, speeds in mm/min -> mm/s.
        {"normalLayerLiftHeight",
         "5",
         TargetPrinterClass::Tilt,
         "",
         "",
         MappingStatus::NotApplicable},
        {"normalLayerLiftHeight",
         "5",
         TargetPrinterClass::GenericMsla,
         "lift_height",
         "5",
         MappingStatus::Exact},
        {"normalLayerLiftHeight2",
         "3",
         TargetPrinterClass::GenericMsla,
         "lift_height_2",
         "3",
         MappingStatus::Exact},
        {"bottomLayerLiftHeight",
         "5",
         TargetPrinterClass::GenericMsla,
         "bottom_lift_height",
         "5",
         MappingStatus::Exact},
        {"bottomLayerLiftHeight2",
         "3",
         TargetPrinterClass::GenericMsla,
         "bottom_lift_height_2",
         "3",
         MappingStatus::Exact},
        {"normalLayerLiftSpeed",
         "150",
         TargetPrinterClass::Tilt,
         "",
         "",
         MappingStatus::NotApplicable},
        {"normalLayerLiftSpeed",
         "150",
         TargetPrinterClass::GenericMsla,
         "lift_speed",
         "2.5",
         MappingStatus::Converted},
        {"normalLayerLiftSpeed2",
         "180",
         TargetPrinterClass::GenericMsla,
         "lift_speed_2",
         "3",
         MappingStatus::Converted},
        {"bottomLayerLiftSpeed",
         "60",
         TargetPrinterClass::GenericMsla,
         "bottom_lift_speed",
         "1",
         MappingStatus::Converted},
        {"bottomLayerLiftSpeed2",
         "180",
         TargetPrinterClass::GenericMsla,
         "bottom_lift_speed_2",
         "3",
         MappingStatus::Converted},
        {"normalDropSpeed", "300", TargetPrinterClass::Tilt, "", "", MappingStatus::NotApplicable},
        {"normalDropSpeed",
         "300",
         TargetPrinterClass::GenericMsla,
         "retract_speed",
         "5",
         MappingStatus::Converted},
        {"normalDropSpeed2",
         "300",
         TargetPrinterClass::GenericMsla,
         "retract_speed_2",
         "5",
         MappingStatus::Converted},

        // The short key spellings of the two-stage lift (M3.4's fixture) map like the long ones.
        {"liftHeight",
         "5",
         TargetPrinterClass::GenericMsla,
         "lift_height",
         "5",
         MappingStatus::Exact},
        {"liftHeight2",
         "3",
         TargetPrinterClass::GenericMsla,
         "lift_height_2",
         "3",
         MappingStatus::Exact},
        {"bottomLiftHeight",
         "5",
         TargetPrinterClass::GenericMsla,
         "bottom_lift_height",
         "5",
         MappingStatus::Exact},
        {"bottomLiftSpeed",
         "60",
         TargetPrinterClass::GenericMsla,
         "bottom_lift_speed",
         "1",
         MappingStatus::Converted},

        // LED PWM: a generic MSLA printer only.
        {"normalLightIntensityPWM",
         "255",
         TargetPrinterClass::Tilt,
         "",
         "",
         MappingStatus::NotApplicable},
        {"normalLightIntensityPWM",
         "255",
         TargetPrinterClass::GenericMsla,
         "light_pwm",
         "255",
         MappingStatus::Exact},
        {"bottomLightIntensityPWM",
         "128",
         TargetPrinterClass::GenericMsla,
         "bottom_light_pwm",
         "128",
         MappingStatus::Exact},
        {"bottomLightIntensityPWM",
         "128",
         TargetPrinterClass::Tilt,
         "",
         "",
         MappingStatus::NotApplicable},

        // Preview settings, never a material setting.
        {"bAntiAliasing", "1", TargetPrinterClass::Tilt, "", "", MappingStatus::NotApplicable},
        {"antiAliasLevel",
         "3",
         TargetPrinterClass::GenericMsla,
         "",
         "",
         MappingStatus::NotApplicable},
        {"bImageBlur", "1", TargetPrinterClass::GenericMsla, "", "", MappingStatus::NotApplicable},
        {"minGreyLevel",
         "0",
         TargetPrinterClass::GenericMsla,
         "",
         "",
         MappingStatus::NotApplicable},
        {"maxGreyLevel",
         "15",
         TargetPrinterClass::GenericMsla,
         "",
         "",
         MappingStatus::NotApplicable},

        // Printer hints and the profile name: used to suggest, never written to the material.
        {"resolutionX", "4056", TargetPrinterClass::Tilt, "", "", MappingStatus::Converted},
        {"resolutionY", "4056", TargetPrinterClass::GenericMsla, "", "", MappingStatus::Converted},
        {"machineWidth", "218", TargetPrinterClass::GenericMsla, "", "", MappingStatus::Converted},
        {"machineDepth", "123", TargetPrinterClass::GenericMsla, "", "", MappingStatus::Converted},
        {"machineHeight", "250", TargetPrinterClass::GenericMsla, "", "", MappingStatus::Converted},
        {"projectType", "MSLA", TargetPrinterClass::GenericMsla, "", "", MappingStatus::Converted},
        {"machineType",
         "Photon Mono",
         TargetPrinterClass::GenericMsla,
         "",
         "",
         MappingStatus::Converted},
        {"currProfile",
         "Siraya Tech Tough",
         TargetPrinterClass::Tilt,
         "",
         "",
         MappingStatus::Converted},
        {"machineName",
         "Photon Mono X",
         TargetPrinterClass::GenericMsla,
         "",
         "",
         MappingStatus::Converted},

        // Never imported.
        {"startGcode", "G28", TargetPrinterClass::Tilt, "", "", MappingStatus::NotApplicable},
        {"layerGcode",
         "G1 Z5",
         TargetPrinterClass::GenericMsla,
         "",
         "",
         MappingStatus::NotApplicable},
        {"endGcode", "G28", TargetPrinterClass::GenericMsla, "", "", MappingStatus::NotApplicable},
        {"displayCorrectX", "0.1", TargetPrinterClass::Tilt, "", "", MappingStatus::NotApplicable},
        {"buildAreaOffsetX",
         "-0.02",
         TargetPrinterClass::GenericMsla,
         "",
         "",
         MappingStatus::NotApplicable},
    };
    return rows;
}

} // namespace

TEST_CASE(
    "ResinProfileMapper - one row per ROADMAP mapping table row and printer class",
    "[resin_profile][mapper]"
)
{
    for (const ExpectedRow& expected : expected_rows()) {
        INFO("source " << expected.source_key << " for " << to_string(expected.printer_class));
        CAPTURE(
            expected.source_key,
            expected.source_value,
            expected.target_key,
            expected.written_value
        );

        const MappingResult result = map_resin_profile(
            profile_of({{expected.source_key, expected.source_value}}),
            expected.printer_class
        );
        const MappedField& row = row_of(result, expected.source_key);

        CHECK(row.status == expected.status);
        CHECK(row.target_key == expected.target_key);
        CHECK(row.value == expected.written_value);
        CHECK_FALSE(row.note.empty());

        if (expected.target_key[0] == '\0')
            CHECK(result.material_values.find(expected.target_key) == result.material_values.end());
        else
            CHECK(result.material_values.at(expected.target_key) == expected.written_value);
    }
}

TEST_CASE(
    "ResinProfileMapper - a bottom layer count is clamped into the fade range of a tilt printer",
    "[resin_profile][mapper]"
)
{
    struct Clamp
    {
        const char* source_value;
        const char* expected;
        bool clamped;
    };

    static const std::vector<Clamp> clamps{
        {"0", "3", true},
        {"1", "3", true},
        {"2", "3", true},
        {"3", "3", false},
        {"10", "10", false},
        {"20", "20", false},
        {"21", "20", true},
        {"50", "20", true}
    };

    for (const Clamp& clamp : clamps) {
        INFO("bottom layer count " << clamp.source_value);
        const MappingResult result = map_resin_profile(
            profile_of({{"bottomLayerCount", clamp.source_value}}),
            TargetPrinterClass::Tilt
        );

        CHECK(result.material_values.at("resin_faded_layers") == clamp.expected);
        const MappedField& row = row_of(result, "bottomLayerCount");
        CHECK(row.status == MappingStatus::Approximated);
        // A count that had to be clamped says so, one that was already in range does not.
        CHECK((row.note.find("Clamped from") != std::string::npos) == clamp.clamped);
    }
}

TEST_CASE(
    "ResinProfileMapper - a bottom layer count is not clamped for a generic MSLA printer",
    "[resin_profile][mapper]"
)
{
    const MappingResult result = map_resin_profile(
        profile_of({{"bottomLayerCount", "50"}}),
        TargetPrinterClass::GenericMsla
    );

    CHECK(result.material_values.at("bottom_layer_count") == "50");
    CHECK(result.material_values.count("resin_faded_layers") == 0);
    CHECK(row_of(result, "bottomLayerCount").status == MappingStatus::Exact);
}

TEST_CASE(
    "ResinProfileMapper - a transition layer count wins over the bottom layer count on a tilt printer",
    "[resin_profile][mapper]"
)
{
    const MappingResult result = map_resin_profile(
        profile_of({{"bottomLayerCount", "50"}, {"transitionLayers", "8"}}),
        TargetPrinterClass::Tilt
    );

    // Both rules want resin_faded_layers. The transition count is the direct equivalent, so it
    // keeps the key and the bottom count is reported as not written.
    CHECK(result.material_values.at("resin_faded_layers") == "8");
    const MappedField& bottom = row_of(result, "bottomLayerCount");
    CHECK(bottom.status == MappingStatus::Approximated);
    CHECK(bottom.value.empty());
    CHECK(bottom.note.find("already carries another value") != std::string::npos);
}

TEST_CASE("ResinProfileMapper - mm/min speeds become mm/s", "[resin_profile][mapper]")
{
    const MappingResult result = map_resin_profile(
        profile_of({{"normalLayerLiftSpeed", "100"}, {"normalDropSpeed", "60"}}),
        TargetPrinterClass::GenericMsla
    );

    // 100 mm/min is 1.66667 mm/s to the six digits the config writes.
    CHECK(result.material_values.at("lift_speed") == "1.66667");
    CHECK(result.material_values.at("retract_speed") == "1");
    // The unit is not verified yet, so every converted speed has to say so.
    CHECK(row_of(result, "normalLayerLiftSpeed").note.find("not verified") != std::string::npos);
    CHECK(row_of(result, "normalDropSpeed").note.find("not verified") != std::string::npos);
}

TEST_CASE(
    "ResinProfileMapper - a price per litre becomes the cost of one bottle",
    "[resin_profile][mapper]"
)
{
    SECTION("a 1 litre bottle is assumed when the profile does not say")
    {
        const MappingResult result = map_resin_profile(
            profile_of({{"resinPrice", "25"}, {"resinUnit", "L"}}),
            TargetPrinterClass::Tilt
        );

        CHECK(result.material_values.at("bottle_cost") == "25");
        const MappedField& row = row_of(result, "resinPrice");
        CHECK(row.status == MappingStatus::Converted);
        // The currency is not converted, and the reader of the report has to be told so.
        CHECK(row.note.find("currency is not converted") != std::string::npos);
        CHECK(row.note.find("1000") != std::string::npos);
    }

    SECTION("a smaller bottle is priced down")
    {
        const MappingResult result = map_resin_profile(
            profile_of({{"resinPrice", "25"}, {"resinUnit", "per litre"}, {"bottleVolume", "500"}}),
            TargetPrinterClass::GenericMsla
        );

        CHECK(result.material_values.at("bottle_cost") == "12.5");
    }

    SECTION("a price that is not per litre is not written")
    {
        const MappingResult result = map_resin_profile(
            profile_of({{"resinPrice", "25"}, {"resinUnit", "kg"}}),
            TargetPrinterClass::Tilt
        );

        CHECK(result.material_values.count("bottle_cost") == 0);
        const MappedField& row = row_of(result, "resinPrice");
        CHECK(row.status == MappingStatus::Converted);
        CHECK(row.note.find("Nothing written") != std::string::npos);
    }

    SECTION("a price without a unit is not written")
    {
        const MappingResult result =
            map_resin_profile(profile_of({{"resinPrice", "25"}}), TargetPrinterClass::Tilt);

        CHECK(result.material_values.count("bottle_cost") == 0);
        CHECK(row_of(result, "resinPrice").note.find("per litre") != std::string::npos);
    }
}

TEST_CASE(
    "ResinProfileMapper - a key the table does not know is reported and not written",
    "[resin_profile][mapper]"
)
{
    const MappingResult result = map_resin_profile(
        profile_of({{"normalExposureTime", "2.5"}, {"someFutureChituboxKey", "42"}}),
        TargetPrinterClass::Tilt
    );

    const MappedField& row = row_of(result, "someFutureChituboxKey");
    CHECK(row.status == MappingStatus::Unknown);
    CHECK(row.target_key.empty());
    CHECK(row.value.empty());
    // The value is kept in the report, so nothing is lost silently.
    CHECK(row.note.find("42") != std::string::npos);
    CHECK(result.material_values.count("someFutureChituboxKey") == 0);
}

TEST_CASE("ResinProfileMapper - every source key ends up in the report", "[resin_profile][mapper]")
{
    const ForeignResinProfile profile = profile_of({
        {"normalExposureTime", "2.5"},
        {"bottomLayerExposureTime", "30"},
        {"bottomLayerCount", "6"},
        {"layerHeight", "0.05"},
        {"resinDensity", "1.12"},
        {"resinPrice", "25"},
        {"resinUnit", "L"},
        {"lightOffTime", "3"},
        {"resetTimeBeforeLift", "1"},
        {"resetTimeAfterLift", "1"},
        {"normalLayerLiftHeight", "5"},
        {"normalLayerLiftSpeed", "150"},
        {"normalDropSpeed", "300"},
        {"normalLightIntensityPWM", "255"},
        {"bAntiAliasing", "1"},
        {"machineName", "Photon Mono"},
        {"currProfile", "Grey resin"},
        {"startGcode", "G28"},
        {"displayCorrectY", "0.1"},
        {"aKeyNobodyKnows", "1"},
    });

    for (const TargetPrinterClass printer_class :
         {TargetPrinterClass::Tilt, TargetPrinterClass::GenericMsla})
    {
        INFO("printer class " << to_string(printer_class));
        const MappingResult result = map_resin_profile(profile, printer_class);

        // One row per source key, and no key reported twice.
        std::map<std::string, int> reported;
        for (const MappedField& row : result.report)
            ++reported[row.source_key];
        for (const auto& entry : profile.raw_values) {
            CAPTURE(entry.first);
            CHECK(reported[entry.first] == 1);
        }
        CHECK(reported.size() == profile.raw_values.size());
    }
}

TEST_CASE(
    "ResinProfileMapper - the same setting under two spellings is used once and both are reported",
    "[resin_profile][mapper]"
)
{
    const MappingResult result = map_resin_profile(
        profile_of({{"bottomLayCount", "4"}, {"bottomLayerCount", "9"}}),
        TargetPrinterClass::Tilt
    );

    // The spelling of the mapping table wins.
    CHECK(result.material_values.at("resin_faded_layers") == "9");
    const MappedField& loser = row_of(result, "bottomLayCount");
    CHECK(loser.value.empty());
    CHECK(loser.note.find("bottomLayerCount") != std::string::npos);
}

TEST_CASE(
    "ResinProfileMapper - a value that is not a number is reported, not written",
    "[resin_profile][mapper]"
)
{
    const MappingResult result = map_resin_profile(
        profile_of({{"normalExposureTime", "two point five"}, {"layerHeight", "0.05"}}),
        TargetPrinterClass::Tilt
    );

    CHECK(result.material_values.count("exposure_time") == 0);
    CHECK(result.material_values.at("resin_layer_height") == "0.05");
    const MappedField& row = row_of(result, "normalExposureTime");
    CHECK(row.value.empty());
    CHECK(row.note.find("two point five") != std::string::npos);
    CHECK(row.note.find("Nothing written") != std::string::npos);
}

TEST_CASE(
    "ResinProfileMapper - the profile name is suggested, the printer hint is the fallback",
    "[resin_profile][mapper]"
)
{
    SECTION("the name of the profile wins")
    {
        const ForeignResinProfile profile =
            profile_of({{"currProfile", "Grey resin"}, {"machineName", "Photon Mono"}});
        profile.printer_hint = "Photon Mono";

        const MappingResult result = map_resin_profile(profile, TargetPrinterClass::Tilt);

        CHECK(result.suggested_name == "Grey resin");
        // Suggested, never written: the interactor decides the preset name (M3.7).
        CHECK(result.material_values.count("name") == 0);
    }

    SECTION("the printer hint is used when the profile does not name itself")
    {
        ForeignResinProfile profile = profile_of({{"machineName", "Photon Mono"}});
        profile.printer_hint        = "Photon Mono";

        CHECK(map_resin_profile(profile, TargetPrinterClass::Tilt).suggested_name == "Photon Mono");
    }

    SECTION("no name at all")
    {
        const MappingResult result = map_resin_profile(
            profile_of({{"normalExposureTime", "2.5"}}),
            TargetPrinterClass::Tilt
        );

        CHECK(result.suggested_name.empty());
    }
}

TEST_CASE(
    "ResinProfileMapper - only keys the SLA config knows are written",
    "[resin_profile][mapper]"
)
{
    const ForeignResinProfile profile = profile_of({
        {"normalExposureTime", "2.5"},
        {"bottomLayerExposureTime", "30"},
        {"transitionLayers", "8"},
        {"bottomLayerCount", "6"},
        {"layerHeight", "0.05"},
        {"resinDensity", "1.12"},
        {"resinPrice", "25"},
        {"resinUnit", "L"},
        {"lightOffTime", "3"},
        {"resetTimeBeforeLift", "1"},
        {"resetTimeAfterLift", "1"},
        {"normalLayerLiftHeight", "5"},
        {"normalLayerLiftHeight2", "3"},
        {"bottomLayerLiftHeight", "5"},
        {"bottomLayerLiftHeight2", "3"},
        {"normalLayerLiftSpeed", "150"},
        {"normalLayerLiftSpeed2", "180"},
        {"bottomLayerLiftSpeed", "60"},
        {"bottomLayerLiftSpeed2", "180"},
        {"normalDropSpeed", "300"},
        {"normalDropSpeed2", "300"},
        {"normalLightIntensityPWM", "255"},
        {"bottomLightIntensityPWM", "128"},
    });

    for (const TargetPrinterClass printer_class :
         {TargetPrinterClass::Tilt, TargetPrinterClass::GenericMsla})
    {
        INFO("printer class " << to_string(printer_class));
        const MappingResult result = map_resin_profile(profile, printer_class);
        REQUIRE_FALSE(result.material_values.empty());

        for (const auto& [key, value] : result.material_values) {
            CAPTURE(key);
            const Domain::ConfigItemDef* def = nullptr;
            for (const Domain::ConfigItemDef& candidate : Domain::get_defs_sla().defs())
                if (candidate.name == key)
                    def = &candidate;

            REQUIRE(def != nullptr);
            // A resin preset value has to be a material option, not a printer or print one.
            CHECK(Domain::get_location_name(def->location) == "sla_material_settings");
            CHECK(value.find_first_not_of("0123456789.,-+eE ") == std::string::npos);
        }
    }
}

TEST_CASE(
    "printer_class_from_config - use_tilt decides, then the printer model",
    "[resin_profile][mapper]"
)
{
    SECTION("a printer that tilts")
    {
        CHECK(printer_class_from_config(printer_view(true, "")) == TargetPrinterClass::Tilt);
    }

    SECTION("a printer that lifts has no use_tilt, its model decides")
    {
        CHECK(
            printer_class_from_config(printer_view(std::nullopt, "Anycubic Photon Mono X 4K"))
            == TargetPrinterClass::GenericMsla
        );
        CHECK(
            printer_class_from_config(printer_view(std::nullopt, "Elegoo Mars 2"))
            == TargetPrinterClass::GenericMsla
        );
    }

    SECTION("an SL1 is a tilt printer even without a use_tilt")
    {
        CHECK(
            printer_class_from_config(printer_view(std::nullopt, "Prusa SL1"))
            == TargetPrinterClass::Tilt
        );
        CHECK(
            printer_class_from_config(printer_view(std::nullopt, "prusa sl1s"))
            == TargetPrinterClass::Tilt
        );
    }

    SECTION("use_tilt wins over the model name")
    {
        CHECK(
            printer_class_from_config(printer_view(true, "Elegoo Mars 2"))
            == TargetPrinterClass::Tilt
        );
        CHECK(
            printer_class_from_config(printer_view(false, "Prusa SL1"))
            == TargetPrinterClass::GenericMsla
        );
    }

    SECTION("the SLA defaults of this fork tilt")
    {
        CHECK(printer_class_from_config(default_printer_view()) == TargetPrinterClass::Tilt);
    }
}
