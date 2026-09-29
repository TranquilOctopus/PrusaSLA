#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Preset/IO/PresetSaver.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileImportInteractor.hpp"
#include "Slic3r/Biz/SecretStoreDummy.hpp"
#include "Slic3r/Biz/Slicing/TestUtils.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/TestUtils/AppInstanceMessageHandlerScope.hpp"
#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/TestData.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/nowide/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <fmt/format.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace Slic3r::Biz;

namespace Slic3r::Test {

namespace fs = boost::filesystem;

/// @brief The generic MSLA printer of the app bundle, which the test data bundle does not carry.
constexpr std::string_view generic_msla_printer = "Photon Mono M5";
/// @brief The system resin an imported preset falls back to when the profile names no resin of this
/// printer: the printer offers them ordered by name, and the shared unnamed profiles do not count.
constexpr std::string_view fallback_resin = "Generic Fast Resin";
/// @brief The other system resin of that printer, which a profile can name to pick it instead.
constexpr std::string_view named_resin = "Generic Resin";

/// @brief A Chitubox profile, written by hand the way the slicer exports one.
std::string chitubox_cfg(std::string_view profile_name, std::string_view bottom_layers)
{
    return fmt::format("currProfile: {}\n"
                       "machineName: Photon Mono M5\n"
                       "layerHeight: 0.05\n"
                       "normalExposureTime: 3.5\n"
                       "bottomLayerExposureTime: 30\n"
                       "bottomLayerCount: {}\n"
                       "lightOffTime: 1\n"
                       "startGcode: G28\n",
                       profile_name, bottom_layers);
}

struct ResinImportFixture
{
    Domain::Workbench workbench;
    App::Platform::StdMainThreadDispatcher dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope{dispatcher};
    Tests::JobManagerScope job_manager_scope{dispatcher};
    MockThumbnailImageGenerator thumbnail_image_generator;
    Biz::ProjectInteractor project_interactor{workbench, dispatcher, thumbnail_image_generator};

    /// Everything the test writes, inside the test tree and removed again.
    fs::path scratch;
    std::string previous_data_dir;
    Preset::IO::BundlePaths bundle_paths;

    ResinImportFixture()
    {
        boost::nowide::nowide_filesystem();

        scratch = Tests::get_datadir() / "resin_import_scratch";
        fs::remove_all(scratch);
        fs::create_directories(scratch / "local");
        fs::create_directories(scratch / "user");
        fs::create_directories(scratch / "config");

        bundle_paths = Preset::IO::BundlePaths{
            .app_bundle_path = fs::path{TEST_APP_PRESETS_DIR}.string(),
            .local_bundle_path = (scratch / "local").string(),
            .populate_local_bundle = false,
            .user_bundle_path = (scratch / "user").string(),
            .user_config_path = (scratch / "config").string(),
        };

        std::unique_ptr<SecretStoreDummy> store_dummy = std::make_unique<SecretStoreDummy>();
        Platform::PlatformServices::instance().set_secret_store(std::move(store_dummy));

        // The preset bundle cache is written under the data dir, so keep it inside the scratch.
        previous_data_dir = Slic3r::data_dir();
        Slic3r::set_data_dir(scratch.string());

        project_interactor.preset_interactor().set_use_hw_config_short_name(false);
        project_interactor.preset_interactor().load_preset_bundle(bundle_paths);
        project_interactor.new_project();
        select_printer(generic_msla_printer);
    }

    ~ResinImportFixture()
    {
        Slic3r::set_data_dir(previous_data_dir);
        fs::remove_all(scratch);
        dispatcher.close();
    }

    ResinImportFixture(const ResinImportFixture&) = delete;
    ResinImportFixture& operator=(const ResinImportFixture&) = delete;

    void select_printer(std::string_view hw_config_name)
    {
        const Preset::PresetItemObservableList& printers =
            project_interactor.preset_interactor().printer_presets();
        std::optional<std::pair<std::string, std::string>> found; // hw printer config id, printer preset id
        for (size_t i = 0, n = printers.items().size(); i < n && !found.has_value(); ++i) {
            const Preset::PresetItem& item = printers.items().at(i);
            if (item.hw_printer_config_name == hw_config_name)
                found.emplace(item.hw_printer_config_id, item.id);
        }
        REQUIRE(found.has_value());
        project_interactor.preset_interactor().select_printer_preset(found->first, found->second);
    }

    ResinProfile::ResinImportTarget target() const
    {
        return ResinProfile::ResinImportTarget{
            .project_id = project_interactor.selected_project_id(),
            .config_container_id = project_interactor.selected_config_container_id(),
            .material_slot = 0,
        };
    }

    /// @brief Write a profile into the test's own scratch dir, not a system temp dir.
    fs::path write_profile(const std::string& file_name, const std::string& contents)
    {
        const fs::path path = scratch / "profiles" / file_name;
        fs::create_directories(path.parent_path());
        boost::nowide::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << contents;
        return path;
    }

    /// @brief A material preset of the selected printer, by its full name.
    const Domain::Preset::EvaluatedMaterialPreset::Preset* material(std::string_view name)
    {
        Preset::PresetInteractor& presets = project_interactor.preset_interactor();
        const Domain::Preset::SelectedPreset& selected = presets.selected_printer_preset();
        for (const auto& entry : presets.get_material_presets(
                 project_interactor.selected_project_id(),
                 selected.hw_config.id,
                 selected.printer.id,
                 selected.print.id,
                 0))
        {
            if (entry.first.get().name == name)
                return &entry.first.get();
        }
        return nullptr;
    }
};

TEST_CASE(
    "ResinProfileImportInteractor imports a Chitubox profile into a user resin preset",
    "[resin_profile][import]"
)
{
    ResinImportFixture fx;
    const fs::path profile = fx.write_profile("photon.cfg", chitubox_cfg("Grey resin", "8"));

    ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor, [] { return "2026-09-29"; });
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
    CHECK(note->get<std::string>() == "Chitubox photon.cfg, imported 2026-09-29");

    // The new preset inherits from the base, so the printer specific settings stay sensible.
    const auto inherits = imported->features.find(Preset::IO::FEATURE_BASED_ID);
    REQUIRE(inherits != imported->features.end());
    CHECK(std::get<std::string>(*inherits) == base_id);
}

TEST_CASE(
    "ResinProfileImportInteractor names a second import of the same profile differently",
    "[resin_profile][import]"
)
{
    ResinImportFixture fx;
    const fs::path profile = fx.write_profile("grey.cfg", chitubox_cfg("Grey resin", "8"));

    ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor, [] { return "2026-09-29"; });
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

    ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor, [] { return "2026-09-29"; });
    const ResinProfile::ResinImportResult result = interactor.import_file(profile, fx.target(), /*dry_run=*/true);

    REQUIRE(result.ok);
    CHECK(result.base_preset == named_resin);
}

TEST_CASE("ResinProfileImportInteractor writes nothing on a dry run", "[resin_profile][import]")
{
    ResinImportFixture fx;
    const fs::path profile = fx.write_profile("grey.cfg", chitubox_cfg("Grey resin", "8"));

    ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor, [] { return "2026-09-29"; });
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
    const fs::path folder = fx.scratch / "profiles";
    fx.write_profile("a_grey.cfg", chitubox_cfg("Grey resin", "8"));
    fx.write_profile("b_broken.cfg", "this file is not a resin profile at all\n");

    ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor, [] { return "2026-09-29"; });
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

    ResinProfile::ResinProfileImportInteractor interactor(fx.project_interactor, [] { return "2026-09-29"; });
    ResinProfile::ResinImportTarget target = fx.target();
    target.config_container_id = target.config_container_id + 1;

    const ResinProfile::ResinImportResult result = interactor.import_file(profile, target);
    CHECK_FALSE(result.ok);
    CHECK(result.error.find("not the selected one") != std::string::npos);
}

} // namespace Slic3r::Test
