#pragma once

#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/Preset/IO/PresetSaver.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
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

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Slic3r::Test {

namespace fs = boost::filesystem;

/// @brief The generic MSLA printer of the app bundle, which the test data bundle does not carry.
constexpr std::string_view generic_msla_printer = "Photon Mono M5";
/// @brief The system resin an imported preset falls back to when the profile names no resin of this
/// printer: the printer offers them ordered by name, and the shared unnamed profiles do not count.
constexpr std::string_view fallback_resin = "Generic Fast Resin";
/// @brief The other system resin of that printer, which a profile can name to pick it instead.
constexpr std::string_view named_resin = "Generic Resin";

/// @brief True for a YYYY-MM-DD string, the date the importer writes into the source note. The note
/// carries the day the import ran on, so the test checks the shape of that day rather than pinning
/// a calendar date, which would start failing the day after it was written.
inline bool is_iso_date(std::string_view text)
{
    if (text.size() != 10 || text[4] != '-' || text[7] != '-')
        return false;
    for (size_t i = 0; i < text.size(); ++i) {
        if (i != 4 && i != 7 && !std::isdigit(static_cast<unsigned char>(text[i])))
            return false;
    }
    return true;
}

/// @brief A Chitubox profile, written by hand the way the slicer exports one.
inline std::string chitubox_cfg(std::string_view profile_name, std::string_view bottom_layers)
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
    /// @brief The scratch tree the test writes into, plus the data dir pointing at it. Declared as
    /// the first member on purpose, so it is destroyed last: the preset bundle the interactor owns
    /// points into this folder and into this data dir, so the folder may only be removed and the
    /// previous data dir restored once the interactor itself is gone. Doing either of them in the
    /// fixture's destructor body tears the interactor down over a deleted preset tree.
    struct ScratchDir
    {
        fs::path path;
        std::string previous_data_dir;

        explicit ScratchDir(fs::path p) : path(std::move(p))
        {
            previous_data_dir = Slic3r::data_dir();
            // The preset bundle cache is written under the data dir, so keep it inside the scratch.
            Slic3r::set_data_dir(path.string());
        }

        ~ScratchDir()
        {
            Slic3r::set_data_dir(previous_data_dir);
            // The non-throwing overload on purpose: a throwing remove_all() out of a destructor
            // would terminate the whole test run instead of failing one test.
            boost::system::error_code ec;
            fs::remove_all(path, ec);
        }

        ScratchDir(const ScratchDir&)            = delete;
        ScratchDir& operator=(const ScratchDir&) = delete;
    };

    Biz::Preset::IO::BundlePaths bundle_paths;
    ScratchDir scratch{Tests::get_datadir() / "resin_import_scratch"};

    Domain::Workbench workbench;
    App::Platform::StdMainThreadDispatcher dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope{dispatcher};
    Tests::JobManagerScope job_manager_scope{dispatcher};
    MockThumbnailImageGenerator thumbnail_image_generator;
    Biz::ProjectInteractor project_interactor{workbench, dispatcher, thumbnail_image_generator};

    ResinImportFixture()
    {
        boost::nowide::nowide_filesystem();

        boost::system::error_code ec;
        fs::remove_all(scratch.path, ec);
        fs::create_directories(scratch.path / "local");
        fs::create_directories(scratch.path / "user");
        fs::create_directories(scratch.path / "config");

        bundle_paths = Biz::Preset::IO::BundlePaths{
            .app_bundle_path       = fs::path{TEST_APP_PRESETS_DIR}.string(),
            .local_bundle_path     = (scratch.path / "local").string(),
            .populate_local_bundle = false,
            .user_bundle_path      = (scratch.path / "user").string(),
            .user_config_path      = (scratch.path / "config").string(),
        };

        std::unique_ptr<Biz::SecretStoreDummy> store_dummy =
            std::make_unique<Biz::SecretStoreDummy>();
        Biz::Platform::PlatformServices::instance().set_secret_store(std::move(store_dummy));

        project_interactor.preset_interactor().set_use_hw_config_short_name(false);
        project_interactor.preset_interactor().load_preset_bundle(bundle_paths);
        project_interactor.new_project();
        select_printer(generic_msla_printer);
    }

    ~ResinImportFixture()
    {
        // Closing the dispatcher is a destructor body job and nothing else: a destructor body runs
        // before the members are destroyed, so this is the last moment at which the interactor and
        // the preset bundle are still alive, and close() runs the queued main-thread work, which
        // several interactors assert must not be left pending. It has to happen while the scratch
        // tree and the data dir still say what they say, because that work resolves paths through
        // them. Removing the tree and restoring the data dir is the ScratchDir member's job, which
        // runs after every other member is gone.
        dispatcher.close();
    }

    ResinImportFixture(const ResinImportFixture&) = delete;
    ResinImportFixture& operator=(const ResinImportFixture&) = delete;

    void select_printer(std::string_view hw_config_name)
    {
        const Biz::Preset::PresetItemObservableList& printers =
            project_interactor.preset_interactor().printer_presets();
        std::optional<std::pair<std::string, std::string>> found; // hw printer config id, printer preset id
        for (size_t i = 0, n = printers.items().size(); i < n && !found.has_value(); ++i) {
            const Biz::Preset::PresetItem& item = printers.items().at(i);
            if (item.hw_printer_config_name == hw_config_name)
                found.emplace(item.hw_printer_config_id, item.id);
        }
        REQUIRE(found.has_value());
        project_interactor.preset_interactor().select_printer_preset(found->first, found->second);
    }

    Biz::ResinProfile::ResinImportTarget target() const
    {
        return Biz::ResinProfile::ResinImportTarget{
            .project_id = project_interactor.selected_project_id(),
            .config_container_id = project_interactor.selected_config_container_id(),
            .material_slot = 0,
        };
    }

    /// @brief Write a profile into the test's own scratch dir, not a system temp dir.
    fs::path write_profile(const std::string& file_name, const std::string& contents)
    {
        const fs::path path = scratch.path / "profiles" / file_name;
        fs::create_directories(path.parent_path());
        boost::nowide::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << contents;
        return path;
    }

    /// @brief A material preset of the selected printer, by its full name.
    const Domain::Preset::EvaluatedMaterialPreset::Preset* material(std::string_view name)
    {
        Biz::Preset::PresetInteractor& presets = project_interactor.preset_interactor();
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

} // namespace Slic3r::Test
