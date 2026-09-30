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
        R"("value":"8","status":"Exact","note":"A tilt printer has no block of bottom layers, )"
        R"(it fades the exposure over 3 to 20 layers, so the count becomes the transition layer )"
        R"(count and is clamped. A generic MSLA printer keeps the count and prints it with the )"
        R"(bottom_* settings."},)"
        R"({"key":"bottomLayerExposureTime","target_key":"initial_exposure_time",)"
        R"("value":"30","status":"Exact","note":"Both in seconds, no conversion."},)"
        R"({"key":"currProfile","target_key":"","value":"","status":"Converted",)"
        R"("note":"Used as the suggested preset name; never written to the material."},)"
        R"({"key":"layerHeight","target_key":"resin_layer_height","value":"0.05",)"
        R"("status":"Exact","note":"Layer height is a resin setting; 0 would mean )"
        R"(\"use the print preset's layer height\"."},)"
        R"({"key":"lightOffTime","target_key":"delay_before_exposure","value":"1,1",)"
        R"("status":"Approximated","note":"The light-off time is written as a delay before )"
        R"(exposure, the same below and above the area fill. The meaning is not verified yet )"
        R"((M3.1/M3.2)."},)"
        R"({"key":"machineName","target_key":"","value":"","status":"Converted",)"
        R"("note":"A printer hint, used to suggest a matching printer; never written to )"
        R"(the material."},)"
        R"({"key":"normalExposureTime","target_key":"exposure_time","value":"3.5",)"
        R"("status":"Exact","note":"Both in seconds, no conversion."},)"
        R"({"key":"startGcode","target_key":"","value":"","status":"Not applicable",)"
        R"("note":"Foreign G-code is never imported, it belongs to the machine profile."})"
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
    std::string target_key,
    std::string value,
    MappingStatus status
)
{
    return {
        .source_key = std::move(source_key),
        .target_key = std::move(target_key),
        .value      = std::move(value),
        .status     = status,
        .note       = "",
    };
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
        field("startGcode", "", "", MappingStatus::NotApplicable),
        field("layerHeight", "resin_layer_height", "0.05", MappingStatus::Exact),
        field("bottomLayerCount", "bottom_layer_count", "8", MappingStatus::Exact),
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
