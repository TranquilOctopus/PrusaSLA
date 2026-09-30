#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/Preset/IO/PresetSaver.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileImportInteractor.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileImportTestFixture.hpp"

#include <optional>
#include <string>
#include <string_view>
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
