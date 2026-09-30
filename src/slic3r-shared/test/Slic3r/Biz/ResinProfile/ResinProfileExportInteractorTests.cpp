#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/ResinProfile/ChituboxCfgReader.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileExportInteractor.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileImportTestFixture.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::Biz::ResinProfile;

namespace Slic3r::Test {

namespace fs = boost::filesystem;

namespace {

/// The keys of an exported file, read the way the reader of that format reads them.
std::map<std::string, std::string> parse_cfg(const std::string& text)
{
    std::map<std::string, std::string> keys;
    std::istringstream               stream{text};
    std::string                      line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        const size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        keys[line.substr(0, colon)] = boost::algorithm::trim_copy(line.substr(colon + 1));
    }
    return keys;
}

std::string value_of(const std::map<std::string, std::string>& keys, const std::string& key)
{
    const auto it = keys.find(key);
    return it == keys.end() ? std::string{} : it->second;
}

ResinProfileExportInteractor exporter_of(ResinImportFixture& fx)
{
    return ResinProfileExportInteractor{fx.project_interactor.preset_interactor()};
}

} // anonymous namespace

TEST_CASE("suggested_cfg_file_name - a preset name as a legal file name", "[resin_profile][export]")
{
    CHECK(suggested_cfg_file_name("Grey resin") == "Grey resin.cfg");
    // A name a Windows file name may not have becomes one it may: the name the picker offers is a
    // name that can be written.
    CHECK(suggested_cfg_file_name("Grey: resin/2") == "Grey_ resin_2.cfg");
    CHECK(suggested_cfg_file_name("  ") == "resin.cfg");
}

TEST_CASE(
    "ResinProfileExportInteractor - the resin of the slot is what an empty name exports",
    "[resin_profile][export]"
)
{
    ResinImportFixture fx;
    // What the interactor says is in the slot, read the same way, so the test does not depend on
    // which resin of the printer the container happens to hold.
    const std::vector<Domain::Preset::EvaluatedMaterialPreset::Preset>& in_slot_list =
        fx.project_interactor.preset_interactor().selected_printer_preset().materials;
    REQUIRE_FALSE(in_slot_list.empty());
    const std::string in_slot = in_slot_list.front().name;

    const ResinProfileExportResult result =
        exporter_of(fx).export_preset(fx.project_interactor.selected_project_id());

    REQUIRE(result.ok);
    CHECK(result.error.empty());
    CHECK(result.preset_name == in_slot);
    CHECK(result.suggested_file_name == in_slot + ".cfg");

    // The file is a resin profile in the format the reader reads, and it is not empty: the mapping
    // table has a row for every key it knows, written or named.
    const std::map<std::string, std::string> keys = parse_cfg(result.exported.text);
    CHECK(value_of(keys, "currProfile") == in_slot);
    CHECK_FALSE(value_of(keys, "normalExposureTime").empty());
    CHECK_FALSE(result.exported.keys.empty());
    CHECK(result.exported.keys.size() > result.exported.skipped.size());
}

TEST_CASE(
    "ResinProfileExportInteractor - a resin preset is found by name and by id",
    "[resin_profile][export]"
)
{
    ResinImportFixture fx;
    const Domain::SelectionId        project_id = fx.project_interactor.selected_project_id();
    const ResinProfileExportInteractor exporter  = exporter_of(fx);

    const Domain::Preset::EvaluatedMaterialPreset::Preset* other = fx.material(named_resin);
    REQUIRE(other != nullptr);

    SECTION("by name")
    {
        const ResinProfileExportResult result = exporter.export_preset(project_id, std::string{named_resin});
        REQUIRE(result.ok);
        CHECK(result.preset_name == std::string{named_resin});
        const std::map<std::string, std::string> keys = parse_cfg(result.exported.text);
        CHECK(value_of(keys, "currProfile") == std::string{named_resin});
        // The price of that system resin and the bottle it comes in: a real preset of this bundle
        // carries both, so the file carries a price per litre as well as the keys of the print.
        CHECK(value_of(keys, "resinPrice") == "30");
        CHECK(value_of(keys, "resinUnit") == "L");
        CHECK(value_of(keys, "bottleVolume") == "1000");
        // The vendor is a resin setting a .cfg names no key for, so it is in the report and not in
        // the file, which is what the button tells the user.
        CHECK_FALSE(result.exported.skipped.empty());
    }

    SECTION("by id, which is what a list row hands over")
    {
        const ResinProfileExportResult result = exporter.export_preset(project_id, other->id);
        REQUIRE(result.ok);
        CHECK(result.preset_name == std::string{named_resin});
    }

    SECTION("a resin this printer does not have is an error that names it")
    {
        const ResinProfileExportResult result =
            exporter.export_preset(project_id, "Some Resin Nobody Sells");
        CHECK_FALSE(result.ok);
        CHECK(result.error.find("Some Resin Nobody Sells") != std::string::npos);
        CHECK(result.exported.text.empty());
        CHECK(result.suggested_file_name.empty());
    }
}

TEST_CASE(
    "ResinProfileExportInteractor - the file that is written reads back as a profile",
    "[resin_profile][export]"
)
{
    ResinImportFixture fx;
    const fs::path folder = fx.scratch.path / "exported";
    boost::system::error_code ec;
    fs::create_directories(folder, ec);
    const fs::path path = folder / "resin.cfg";

    std::string error;
    const ResinProfileExportResult result = exporter_of(fx).export_preset_to_file(
        path,
        fx.project_interactor.selected_project_id(),
        std::string{named_resin},
        0,
        &error
    );

    REQUIRE(result.ok);
    CHECK(error.empty());
    REQUIRE(fs::exists(path));
    // What is on disk is the text the report describes, not a copy of the preset in another format:
    // the reader of that format recognises it and the name comes back as it went in.
    boost::filesystem::ifstream in{path, std::ios::binary};
    const std::string            text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    CHECK(text == result.exported.text);

    const ChituboxCfgReader reader;
    const auto             read = reader.read(path);
    REQUIRE(read.has_value());
    const auto raw = read->raw_values.find("currProfile");
    REQUIRE(raw != read->raw_values.end());
    CHECK(raw->second == std::string{named_resin});
}

TEST_CASE(
    "ResinProfileExportInteractor - a file that cannot be opened is an error, not a crash",
    "[resin_profile][export]"
)
{
    ResinImportFixture fx;
    // A path in a folder that is not there: the picker can only hand back a path in a folder that
    // exists, but the writer still has to say what happened when one does not.
    const fs::path path = fx.scratch.path / "no_such_folder" / "resin.cfg";

    std::string error;
    const ResinProfileExportResult result = exporter_of(fx).export_preset_to_file(
        path,
        fx.project_interactor.selected_project_id(),
        {},
        0,
        &error
    );

    CHECK_FALSE(result.ok);
    CHECK_FALSE(error.empty());
    // The report of what would have been written is still there: the failure is the file, not the
    // mapping.
    CHECK_FALSE(result.exported.text.empty());
    CHECK_FALSE(fs::exists(path));
}

} // namespace Slic3r::Test
