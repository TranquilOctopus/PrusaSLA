#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/Preset/IO/PresetSaver.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileImportInteractor.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileImportTestFixture.hpp"

#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace Slic3r::Biz;

namespace Slic3r::Test {

TEST_CASE(
    "ResinProfileImportInteractor imports a Chitubox profile into a user resin preset",
    "[resin_profile][import]"
)
{
    ResinImportFixture fx;
    const fs::path profile = fx.write_profile("photon.cfg", chitubox_cfg("Grey resin", "8"));

    ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor);
    const ResinProfile::ResinImportResult result = interactor.import_file(profile, fx.target());

    REQUIRE(result.ok);
    CHECK(result.error.empty());
    CHECK(result.preset_name == "Grey resin");
    CHECK(result.base_preset == fallback_resin);
    // What the file says about itself, for the source summary of the import dialog and for the
    // report the CLI writes.
    CHECK(result.source_format == "chitubox-cfg");
    CHECK(result.resin_name == "Grey resin");
    // A Chitubox profile names no vendor, so the field is empty rather than made up.
    CHECK(result.resin_vendor.empty());
    CHECK_FALSE(result.base_preset_id.empty());
    // A Z-lift printer keeps the bottom layer count instead of fading the exposure over it.
    CHECK(result.mapping.material_values.at("exposure_time") == "3.5");
    CHECK(result.mapping.material_values.at("bottom_layer_count") == "8");
    CHECK(result.mapping.material_values.count("resin_faded_layers") == 0);
    CHECK(result.mapping.material_values.count("lift_height") == 0);
    // Nothing is lost silently: the keys that are only reported stay in the report.
    CHECK(result.mapping.report.size() >= 8);

    const Domain::Preset::EvaluatedMaterialPreset::Preset* base = fx.material(fallback_resin);
    REQUIRE(base != nullptr);
    const std::string base_id = base->id;

    // The preset went through the normal save path, so it comes back with the bundle.
    fx.project_interactor.preset_interactor().load_preset_bundle(fx.bundle_paths);
    fx.select_printer(generic_msla_printer);

    const Domain::Preset::EvaluatedMaterialPreset::Preset* imported = fx.material(result.preset_name);
    REQUIRE(imported != nullptr);
    CHECK(imported->origin == Domain::Preset::PresetOrigin::User);

    const Domain::ConfigItem* exposure = imported->config_box().items.find("exposure_time");
    REQUIRE(exposure != nullptr);
    CHECK(exposure->get<double>() == 3.5);

    const Domain::ConfigItem* bottom_layers = imported->config_box().items.find("bottom_layer_count");
    REQUIRE(bottom_layers != nullptr);
    CHECK(bottom_layers->get<int>() == 8);

    const Domain::ConfigItem* note = imported->config_box().items.find("material_source_note");
    REQUIRE(note != nullptr);
    // The note names the file and the day it was imported, so the date is only checked for its
    // shape: the test does not know which day it runs on.
    constexpr std::string_view note_prefix = "Chitubox photon.cfg, imported ";
    const std::string source_note = note->get<std::string>();
    REQUIRE(source_note.starts_with(note_prefix));
    CHECK(is_iso_date(source_note.substr(note_prefix.size())));

    // The new preset inherits from the base, so the printer specific settings stay sensible.
    const auto inherits = imported->features.find(Preset::IO::FEATURE_BASED_ID);
    REQUIRE(inherits != imported->features.end());
    CHECK(std::get<std::string>(inherits->second) == base_id);
}

TEST_CASE(
    "ResinProfileImportInteractor names a second import of the same profile differently",
    "[resin_profile][import]"
)
{
    ResinImportFixture fx;
    const fs::path profile = fx.write_profile("grey.cfg", chitubox_cfg("Grey resin", "8"));

    ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor);
    const ResinProfile::ResinImportResult first = interactor.import_file(profile, fx.target());
    const ResinProfile::ResinImportResult second = interactor.import_file(profile, fx.target());

    REQUIRE(first.ok);
    REQUIRE(second.ok);
    CHECK(first.preset_name == "Grey resin");
    // Saving under a name that is taken would replace that preset instead of adding one.
    CHECK(second.preset_name == "Grey resin (2)");

    // Both saves reloaded the vendor presets, which freed the config boxes the print tool settings
    // read, so the extruder candidate updates the two imports posted are delivered against the
    // re-resolved ones. In the app this happens on the next idle, after the import has returned.
    fx.drain_main_thread_queue();
}

TEST_CASE(
    "ResinProfileImportInteractor picks the resin of the same name as the base",
    "[resin_profile][import]"
)
{
    ResinImportFixture fx;
    // The printer also ships this resin, so the name of the profile picks it over the fallback.
    const fs::path profile = fx.write_profile("generic.cfg", chitubox_cfg("Generic Resin", "8"));

    ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor);
    const ResinProfile::ResinImportResult result = interactor.import_file(profile, fx.target(), /*dry_run=*/true);

    REQUIRE(result.ok);
    CHECK(result.base_preset == named_resin);
}

TEST_CASE("ResinProfileImportInteractor writes nothing on a dry run", "[resin_profile][import]")
{
    ResinImportFixture fx;
    const fs::path profile = fx.write_profile("grey.cfg", chitubox_cfg("Grey resin", "8"));

    ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor);
    const ResinProfile::ResinImportResult result = interactor.import_file(profile, fx.target(), /*dry_run=*/true);

    // What the import would do is reported, name collisions included.
    REQUIRE(result.ok);
    CHECK(result.preset_name == "Grey resin");
    CHECK(result.base_preset == fallback_resin);
    CHECK_FALSE(result.mapping.material_values.empty());
    // Nothing was saved and the printer still shows its own resin.
    CHECK(fx.material("Grey resin") == nullptr);
    CHECK(fx.material(fallback_resin) != nullptr);
}

TEST_CASE(
    "ResinProfileImportInteractor imports onto the base the caller asks for",
    "[resin_profile][import]"
)
{
    ResinImportFixture fx;
    // The profile names no resin of this printer, so on its own the base would be the fallback one.
    const fs::path profile = fx.write_profile("grey.cfg", chitubox_cfg("Grey resin", "8"));
    const Domain::Preset::EvaluatedMaterialPreset::Preset* base = fx.material(named_resin);
    REQUIRE(base != nullptr);
    const std::string base_id = base->id;

    ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor);
    const ResinProfile::ResinImportResult result =
        interactor.import_file(profile, fx.target(), /*dry_run=*/true, base_id);

    REQUIRE(result.ok);
    CHECK(result.base_preset == named_resin);
    CHECK(result.base_preset_id == base_id);
}

TEST_CASE(
    "ResinProfileImportInteractor falls back to its own base choice for an unknown base id",
    "[resin_profile][import]"
)
{
    ResinImportFixture fx;
    const fs::path profile = fx.write_profile("grey.cfg", chitubox_cfg("Grey resin", "8"));

    ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor);
    const ResinProfile::ResinImportResult result =
        interactor.import_file(profile, fx.target(), /*dry_run=*/true, "no_preset_with_this_id");

    // A base that is not a system resin of this printer is ignored, not refused: the dialog can pass
    // an id a bundle reload has dropped without breaking the import.
    REQUIRE(result.ok);
    CHECK(result.base_preset == fallback_resin);
}

TEST_CASE("ResinProfileImportInteractor saves under the name the caller passes", "[resin_profile][import]")
{
    ResinImportFixture fx;
    const fs::path profile = fx.write_profile("grey.cfg", chitubox_cfg("Grey resin", "8"));

    ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor);
    const ResinProfile::ResinImportResult result =
        interactor.import_file(profile, fx.target(), /*dry_run=*/false, {}, "My grey resin");

    REQUIRE(result.ok);
    CHECK(result.preset_name == "My grey resin");
    CHECK(fx.material("My grey resin") != nullptr);
    // The name the profile carries no longer names the preset.
    CHECK(fx.material("Grey resin") == nullptr);
}

TEST_CASE(
    "ResinProfileImportInteractor keeps a caller name unique and cuts off what a file name cannot carry",
    "[resin_profile][import]"
)
{
    ResinImportFixture fx;
    const fs::path profile = fx.write_profile("grey.cfg", chitubox_cfg("Grey resin", "8"));

    ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor);
    const ResinProfile::ResinImportResult first =
        interactor.import_file(profile, fx.target(), /*dry_run=*/false, {}, "Grey/resin");
    const ResinProfile::ResinImportResult second =
        interactor.import_file(profile, fx.target(), /*dry_run=*/false, {}, "Grey/resin");

    // The name is a file name, so the slash is replaced, and a name that is taken still adds a
    // preset instead of replacing the first one.
    REQUIRE(first.ok);
    REQUIRE(second.ok);
    CHECK(first.preset_name == "Grey_resin");
    CHECK(second.preset_name == "Grey_resin (2)");
}

TEST_CASE(
    "ResinProfileImportInteractor offers the system resins of the printer as bases",
    "[resin_profile][import]"
)
{
    ResinImportFixture fx;

    const std::vector<std::pair<std::string, std::string>> resins =
        ResinProfile::system_resin_presets(
            fx.project_interactor.preset_interactor(), fx.project_interactor.selected_project_id(), 0
        );

    // The printer's own resins are offered, every one of them with an id the import takes back.
    CHECK(resins.size() >= 2);
    const auto offers = [&resins](const std::string& name) {
        return std::ranges::any_of(resins, [&name](const std::pair<std::string, std::string>& resin) {
            return resin.second == name;
        });
    };
    CHECK(offers(fallback_resin));
    CHECK(offers(named_resin));
    for (const std::pair<std::string, std::string>& resin : resins) {
        CHECK_FALSE(resin.first.empty());
    }

    // The ids are the ones the import takes back: a dry run with the id of a listed resin picks it.
    const auto named = std::ranges::find_if(resins, [](const std::pair<std::string, std::string>& resin) {
        return resin.second == named_resin;
    });
    REQUIRE(named != resins.end());
    const fs::path profile = fx.write_profile("grey.cfg", chitubox_cfg("Grey resin", "8"));
    ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor);
    const ResinProfile::ResinImportResult result =
        interactor.import_file(profile, fx.target(), /*dry_run=*/true, named->first);
    REQUIRE(result.ok);
    CHECK(result.base_preset_id == named->first);
}

TEST_CASE("ResinProfileImportInteractor imports a folder, one result per file", "[resin_profile][import]")
{
    ResinImportFixture fx;
    const fs::path folder = fx.scratch.path / "profiles";
    fx.write_profile("a_grey.cfg", chitubox_cfg("Grey resin", "8"));
    fx.write_profile("b_broken.cfg", "this file is not a resin profile at all\n");

    ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor);
    const std::vector<ResinProfile::ResinImportResult> results = interactor.import_folder(folder, fx.target());

    // The good file imports, the unreadable one only fills in its own result.
    REQUIRE(results.size() == 2);
    CHECK(results[0].ok);
    CHECK(results[0].preset_name == "Grey resin");
    CHECK_FALSE(results[1].ok);
    CHECK_FALSE(results[1].error.empty());
    CHECK(fx.material("Grey resin") != nullptr);
}

TEST_CASE("ResinProfileImportInteractor reports a target that is not selected", "[resin_profile][import]")
{
    ResinImportFixture fx;
    const fs::path profile = fx.write_profile("grey.cfg", chitubox_cfg("Grey resin", "8"));

    ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor);
    ResinProfile::ResinImportTarget target = fx.target();
    target.config_container_id = target.config_container_id + 1;

    const ResinProfile::ResinImportResult result = interactor.import_file(profile, target);
    CHECK_FALSE(result.ok);
    CHECK(result.error.find("not the selected one") != std::string::npos);
}

} // namespace Slic3r::Test
