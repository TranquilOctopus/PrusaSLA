#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/ResinProfile/ResinImportReport.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileImportInteractor.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileImportTestFixture.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileMapper.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

using Slic3r::Biz::ResinProfile::MappedField;
using Slic3r::Biz::ResinProfile::MappingStatus;
using Slic3r::Biz::ResinProfile::ResinImportResult;
using Slic3r::Biz::ResinProfile::resin_import_report_json;

namespace Slic3r::Test {

namespace {

/// @brief The report of a dry run of a folder holding one good and one unreadable profile, checked
/// in as one string: a change of the shape or the wording of the report has to be a failing test
/// here rather than a silent change of a file people diff. It is written as chunks, because a raw
/// string literal cannot be continued the way a line this long has to be.
const std::string& expected_folder_report()
{
    static const std::string report =
        R"({"results":[)"
        R"({"file":"a_grey.cfg","ok":true,"error":"","preset_name":"Grey resin",)"
        R"("base_preset":"Generic Fast Resin","mapping":[)"
        R"({"key":"bottomLayerCount","target_key":"bottom_layer_count",)"
        R"("source_value":"8","source_unit":"","value":"8","target_unit":"","status":"Exact",)"
        R"("note":"A tilt printer has no block of )"
        R"(bottom layers, it fades the exposure over 3 to 20 layers, so the count becomes the )"
        R"(transition layer count and is clamped. A generic MSLA printer keeps the count and prints )"
        R"(it with the bottom_* settings."},)"
        R"({"key":"bottomLayerExposureTime","target_key":"initial_exposure_time",)"
        R"("source_value":"30","source_unit":"s","value":"30","target_unit":"s","status":"Exact",)"
        R"("note":"Both in seconds, no )"
        R"(conversion."},)"
        R"({"key":"currProfile","target_key":"","source_value":"Grey resin","source_unit":"",)"
        R"("value":"","target_unit":"","status":"Converted",)"
        R"("note":"Used as the suggested preset name; never written to the )"
        R"(material."},)"
        R"({"key":"layerHeight","target_key":"resin_layer_height","source_value":"0.05",)"
        R"("source_unit":"mm","value":"0.05","target_unit":"mm","status":"Exact",)"
        R"("note":"Layer height is a resin setting; 0 would mean )"
        R"(\"use the print preset's layer height\"."},)"
        R"({"key":"lightOffTime","target_key":"delay_before_exposure","source_value":"1",)"
        R"("source_unit":"s","value":"1,1","target_unit":"s","status":"Approximated",)"
        R"("note":"The light-off time is written as a delay )"
        R"(before exposure, the same below and above the area fill. The meaning is not verified yet )"
        R"((M3.1/M3.2)."},)"
        R"({"key":"machineName","target_key":"","source_value":"Photon Mono M5","source_unit":"",)"
        R"("value":"","target_unit":"","status":"Converted",)"
        R"("note":"A printer hint, used to suggest a matching printer; never )"
        R"(written to the material."},)"
        R"({"key":"normalExposureTime","target_key":"exposure_time","source_value":"3.5",)"
        R"("source_unit":"s","value":"3.5","target_unit":"s","status":"Exact",)"
        R"("note":"Both in seconds, no conversion."},)"
        R"({"key":"startGcode","target_key":"","source_value":"G28","source_unit":"",)"
        R"("value":"","target_unit":"","status":"Not applicable",)"
        R"("note":"Foreign G-code is never imported, it belongs to the )"
        R"(machine profile."})"
        R"(]},)"
        R"({"file":"b_broken.cfg","ok":false,"error":"Unrecognized resin profile format",)"
        R"("preset_name":"","base_preset":"","mapping":[]})"
        R"(]})";
    return report;
}

/// @brief The report of one file the reader does not recognize.
const std::string& expected_failed_report()
{
    static const std::string report =
        R"({"results":[{"file":"broken.cfg","ok":false,"error":"Unrecognized resin profile )"
        R"(format","preset_name":"","base_preset":"","mapping":[]}]})";
    return report;
}

/// @brief One mapping row, so that the test below can list them in an order of its own.
MappedField field(
    std::string source_key,
    std::string source_value,
    std::string source_unit,
    std::string target_key,
    std::string value,
    std::string target_unit,
    MappingStatus status
)
{
    return {
        .source_key   = std::move(source_key),
        .source_value = std::move(source_value),
        .source_unit  = std::move(source_unit),
        .target_key   = std::move(target_key),
        .value        = std::move(value),
        .target_unit  = std::move(target_unit),
        .status       = status,
        .note         = "",
    };
}

/// @brief One mapping row of a key that carries no unit on either side, which is most of them.
MappedField plain_field(
    std::string source_key,
    std::string source_value,
    std::string target_key,
    std::string value,
    MappingStatus status
)
{
    return field(
        std::move(source_key),
        std::move(source_value),
        "",
        std::move(target_key),
        std::move(value),
        "",
        status
    );
}

/// @brief Import the folder of the fixture as a dry run, holding one profile the reader takes and
/// one it does not.
std::vector<ResinImportResult> dry_run_of_two_profiles(ResinImportFixture& fx)
{
    fx.write_profile("a_grey.cfg", chitubox_cfg("Grey resin", "8"));
    fx.write_profile("b_broken.cfg", "this file is not a resin profile at all\n");

    Biz::ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor);
    return interactor.import_folder(fx.scratch.path / "profiles", fx.target(), /*dry_run=*/true);
}

} // namespace

TEST_CASE(
    "Resin import report of a dry run is a fixed document, one entry per file",
    "[resin_profile][import][report]"
)
{
    ResinImportFixture fx;
    const std::vector<ResinImportResult> results = dry_run_of_two_profiles(fx);

    REQUIRE(results.size() == 2);
    CHECK(resin_import_report_json(results) == expected_folder_report());
}

TEST_CASE(
    "Resin import report names the file, not the folder it was read from",
    "[resin_profile][import][report]"
)
{
    ResinImportFixture fx;
    const std::vector<ResinImportResult> results = dry_run_of_two_profiles(fx);

    // The interactor records the path it was given, and that path differs from run to run, so a
    // report carrying it would differ from run to run too.
    const std::string report = resin_import_report_json(results);
    CHECK(report.find(fx.scratch.path.string()) == std::string::npos);
    CHECK(report.find("a_grey.cfg") != std::string::npos);
}

TEST_CASE("Resin import report sorts the mapping rows by key", "[resin_profile][import][report]")
{
    // The mapper emits the rows in the order of the mapping table. Whatever that order is, the
    // report is sorted by the key each row is about, so two reports of the same profiles can be
    // compared line by line.
    ResinImportResult result;
    result.file        = "/some/folder/zebra.cfg";
    result.ok          = true;
    result.preset_name = "Grey resin";
    result.mapping.report = {
        plain_field("startGcode", "G28", "", "", MappingStatus::NotApplicable),
        plain_field("layerHeight", "0.05", "resin_layer_height", "0.05", MappingStatus::Exact),
        plain_field("bottomLayerCount", "8", "bottom_layer_count", "8", MappingStatus::Exact),
    };

    const nlohmann::ordered_json report =
        nlohmann::ordered_json::parse(resin_import_report_json({result}));

    const nlohmann::ordered_json& mapping = report.at("results").at(0).at("mapping");
    std::vector<std::string> keys;
    for (const nlohmann::ordered_json& row : mapping)
        keys.push_back(row.at("key").get<std::string>());

    const std::vector<std::string> sorted{"bottomLayerCount", "layerHeight", "startGcode"};
    REQUIRE(keys.size() == sorted.size());
    CHECK(keys == sorted);
}

TEST_CASE(
    "Resin import report carries the value the file had next to the value that is written",
    "[resin_profile][import][report]"
)
{
    // 150 mm/min becomes 2.5 mm/s, and a script reading the report has to be able to tell the two
    // apart, so the row carries the value of the file under source_value of its own.
    ResinImportResult result;
    result.file           = "grey.cfg";
    result.ok             = true;
    result.preset_name    = "Grey resin";
    result.mapping.report = {
        plain_field("normalLayerLiftSpeed", "150", "lift_speed", "2.5", MappingStatus::Converted),
    };

    const nlohmann::ordered_json report =
        nlohmann::ordered_json::parse(resin_import_report_json({result}));

    const nlohmann::ordered_json& row = report.at("results").at(0).at("mapping").at(0);
    CHECK(row.at("source_value").get<std::string>() == "150");
    CHECK(row.at("value").get<std::string>() == "2.5");
}

TEST_CASE(
    "Resin import report carries the unit of both sides of a row",
    "[resin_profile][import][report]"
)
{
    // The units came with the values in M3.10c, but only the review table showed them: a script had
    // to know the unit of a key from the mapping table. A row carries the unit of the value the file
    // had and the unit of the value that is written to it, so a converted value is readable without
    // the note and without the table.
    ResinImportResult result;
    result.file           = "grey.cfg";
    result.ok             = true;
    result.preset_name    = "Grey resin";
    result.mapping.report = {
        // A converted speed: mm/min in the file, mm/s in the preset.
        field(
            "normalLayerLiftSpeed",
            "150",
            "mm/min",
            "lift_speed",
            "2.5",
            "mm/s",
            MappingStatus::Converted
        ),
        // A copied value: the same unit on both sides.
        field("layerHeight", "0.05", "mm", "resin_layer_height", "0.05", "mm", MappingStatus::Exact),
        // A key that is not a quantity: no unit on either side, and a guess would be worse than
        // nothing, so the row says nothing.
        plain_field("currProfile", "Grey resin", "", "", MappingStatus::Converted),
        // A row that writes nothing still has the unit of the value the file had.
        field("bottleVolume", "500", "ml", "", "", "", MappingStatus::Converted),
    };

    const nlohmann::ordered_json report =
        nlohmann::ordered_json::parse(resin_import_report_json({result}));
    const nlohmann::ordered_json& mapping = report.at("results").at(0).at("mapping");

    REQUIRE(mapping.size() == 4);
    // The rows come back sorted by their key, so the four are at a known place each.
    const nlohmann::ordered_json& bottle  = mapping.at(0);
    const nlohmann::ordered_json& name     = mapping.at(1);
    const nlohmann::ordered_json& height   = mapping.at(2);
    const nlohmann::ordered_json& lift_row = mapping.at(3);

    // A row that writes nothing still carries the unit of the value the file had.
    CHECK(bottle.at("key").get<std::string>() == "bottleVolume");
    CHECK(bottle.at("source_unit").get<std::string>() == "ml");
    CHECK(bottle.at("target_unit").get<std::string>().empty());

    // A key that is not a quantity has no unit on either side, and a guess would be worse than
    // nothing, so the row says nothing.
    CHECK(name.at("key").get<std::string>() == "currProfile");
    CHECK(name.at("source_unit").get<std::string>().empty());
    CHECK(name.at("target_unit").get<std::string>().empty());

    // A copied value: the same unit on both sides.
    CHECK(height.at("key").get<std::string>() == "layerHeight");
    CHECK(height.at("source_unit").get<std::string>() == "mm");
    CHECK(height.at("target_unit").get<std::string>() == "mm");

    // A converted speed: mm/min in the file, mm/s in the preset, without the note.
    CHECK(lift_row.at("key").get<std::string>() == "normalLayerLiftSpeed");
    CHECK(lift_row.at("source_unit").get<std::string>() == "mm/min");
    CHECK(lift_row.at("target_unit").get<std::string>() == "mm/s");
}

TEST_CASE(
    "Resin import report of an empty batch is an empty list",
    "[resin_profile][import][report]"
)
{
    // A folder that holds nothing to import is not an error of its own, but its report still has to
    // be a document that the report of a full one can be read next to.
    CHECK(resin_import_report_json({}) == R"({"results":[]})");
}

TEST_CASE(
    "Resin import report of a file that cannot be read says why and names no preset",
    "[resin_profile][import][report]"
)
{
    ResinImportFixture fx;
    const fs::path profile = fx.write_profile("broken.cfg", "not a resin profile at all\n");

    Biz::ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor);
    const std::vector<ResinImportResult> results{
        interactor.import_file(profile, fx.target(), /*dry_run=*/true)};

    // A file that could not be read still gets its own entry, with the reason, no preset, and no
    // mapping rows, because nothing was mapped.
    REQUIRE_FALSE(results.front().ok);
    CHECK(resin_import_report_json(results) == expected_failed_report());
}

} // namespace Slic3r::Test
