// The upload destinations of PhysicalPrinterInteractor: the list the export dialog offers, which of
// them a printer profile may use, and what a plate or printer change does to the selection.
//
// The fixture is built over a local folder and a removable drive. The local folder is the scratch
// tree the data dir points at: the physical printer storage writes {data_dir}/physical_printer
// there and the preset bundle is loaded from there. The removable drive is a second folder
// standing in for a printer's stick. Neither folder is a path the destinations carry - the two
// filesystem destinations are the same kind of entry and the FileSystemExport::prefer_removable
// flag is what tells them apart, which is what the export dialog reads to pick a folder.

#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterInteractor.hpp"
#include "Slic3r/Biz/Preset/IO/BundlePaths.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Slicing/TestUtils.hpp"

#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "Slic3r/Domain/Workbench.hpp"
#include "Slic3r/TestUtils/AppInstanceMessageHandlerScope.hpp"
#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/TestData.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/filesystem.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace {

namespace fs = boost::filesystem;

using Slic3r::Biz::_u8L;
using Slic3r::Biz::PhysicalPrinter::ConnectUpload;
using Slic3r::Biz::PhysicalPrinter::FileSystemExport;
using Slic3r::Biz::PhysicalPrinter::PhysicalPrinterConfig;
using Slic3r::Biz::PhysicalPrinter::PhysicalPrinterInteractor;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::Preset::HwModel;
using Slic3r::Domain::Preset::HwPrinterConfig;

// The SLA printer the shipped community-sla bundle carries, by the name its printer preset is
// filed under. Only its hardware config matters here: it is a printer no FFF upload fits.
constexpr const char* sla_printer_name = "Photon Mono M5";

struct PhysicalPrinterFixture
{
    // Points the data dir at a scratch tree and restores it afterwards. The first member on
    // purpose, so it is the last one destroyed: the interactor's storage and the preset bundle both
    // point into the tree, which may only be removed once they are gone.
    struct ScratchDir
    {
        fs::path path;
        std::string previous_data_dir;

        explicit ScratchDir(fs::path p) : path(std::move(p))
        {
            previous_data_dir = Slic3r::data_dir();
            Slic3r::set_data_dir(path.string());
        }

        ~ScratchDir()
        {
            Slic3r::set_data_dir(previous_data_dir);
            // The non-throwing overload: a throwing remove_all() out of a destructor would take the
            // whole test run with it.
            boost::system::error_code ec;
            fs::remove_all(path, ec);
        }

        ScratchDir(const ScratchDir&)            = delete;
        ScratchDir& operator=(const ScratchDir&) = delete;
    };

    ScratchDir scratch{Tests::get_datadir() / "datadir" / "physical_printer"};
    Slic3r::Domain::Workbench workbench;
    Slic3r::App::Platform::StdMainThreadDispatcher dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope{dispatcher};
    Tests::JobManagerScope job_manager_scope{dispatcher};
    Slic3r::Test::MockThumbnailImageGenerator thumbnail_image_generator;
    Slic3r::Biz::ProjectInteractor
        project_interactor{workbench, dispatcher, thumbnail_image_generator};
    Slic3r::Biz::Preset::IO::BundlePaths bundle_paths;
    Slic3r::Domain::SelectionId first_plate{Slic3r::Domain::INVALID_ID};

    PhysicalPrinterFixture()
    {
        boost::nowide::nowide_filesystem();

        boost::system::error_code ec;
        fs::remove_all(scratch.path, ec);
        // The local folder the drive destinations write into, and the removable drive beside it.
        fs::create_directories(local_folder());
        fs::create_directories(removable_drive());
        // The three folders the preset bundle wants. They live in the scratch tree as well, so the
        // bundle's cache does not end up in the test data folder.
        fs::create_directories(scratch.path / "local");
        fs::create_directories(scratch.path / "user");
        fs::create_directories(scratch.path / "config");

        bundle_paths = Slic3r::Biz::Preset::IO::BundlePaths{
            .app_bundle_path       = fs::path{TEST_APP_PRESETS_DIR}.string(),
            .local_bundle_path     = (scratch.path / "local").string(),
            .populate_local_bundle = false,
            .user_bundle_path      = (scratch.path / "user").string(),
            .user_config_path      = (scratch.path / "config").string(),
        };

        Slic3r::Biz::Preset::PresetInteractor& presets = project_interactor.preset_interactor();
        presets.set_use_hw_config_short_name(false);
        presets.load_preset_bundle(bundle_paths);
        project_interactor.new_project();

        // A new project has no current plate yet, and the plate is what the destination is
        // remembered for, so select the first one the way the app does at startup. No assertion in
        // here: the tests check the state they rely on themselves.
        first_plate = project_interactor.selected_project().config_containers().front()->id().id;
        project_interactor.select_config_container(first_plate);
    }

    ~PhysicalPrinterFixture()
    {
        // Selecting a printer posted main-thread work, which wants draining while the interactor is
        // still alive. A destructor body runs before the members are destroyed.
        dispatcher.dispatch_enqueued();
        dispatcher.close();
    }

    PhysicalPrinterFixture(const PhysicalPrinterFixture&)            = delete;
    PhysicalPrinterFixture& operator=(const PhysicalPrinterFixture&) = delete;

    PhysicalPrinterInteractor& physical_printers()
    {
        return project_interactor.physical_printer_interactor();
    }

    Slic3r::Biz::Preset::PresetInteractor& presets()
    {
        return project_interactor.preset_interactor();
    }

    /// The folder the Local Drive destination writes into.
    fs::path local_folder() const
    {
        return scratch.path / "local_drive";
    }

    /// The folder standing in for the printer's removable drive.
    fs::path removable_drive() const
    {
        return scratch.path / "removable_drive";
    }

    /// The uuid of a filesystem destination, told apart by its prefer_removable flag.
    std::string uuid_of(bool removable)
    {
        const auto& list = physical_printers().observable_list();
        for (size_t i = 0, n = list.size(); i < n; ++i) {
            const auto* fs_export = std::get_if<FileSystemExport>(&list.at(i).payload);
            if (fs_export != nullptr && fs_export->prefer_removable == removable)
                return list.at(i).uuid;
        }
        return {};
    }

    std::string local_drive_uuid()
    {
        return uuid_of(false);
    }

    std::string removable_drive_uuid()
    {
        return uuid_of(true);
    }

    std::string connect_uuid()
    {
        const auto& list = physical_printers().observable_list();
        for (size_t i = 0, n = list.size(); i < n; ++i)
            if (std::holds_alternative<ConnectUpload>(list.at(i).payload))
                return list.at(i).uuid;
        return {};
    }

    /// Adds a host-upload destination for the printer of the current plate, the way the printer
    /// settings dialog does, and returns its uuid. It is selected afterwards.
    std::string add_host_upload(const std::string& name)
    {
        PhysicalPrinterInteractor& printers = physical_printers();
        printers.on_dialog_button_add_new();

        PhysicalPrinterConfig edited = printers.edited_printer();
        edited.name                  = name;
        printers.set_edited_printer(edited);
        printers.save_new_printer();

        return printers.selected_uuid();
    }

    /// Switches the current plate to the SLA printer of the shipped bundle.
    void select_sla_printer()
    {
        const auto& printer_items = presets().printer_presets();
        std::optional<std::pair<std::string, std::string>> found; // hw printer config id, preset id
        for (size_t i = 0, n = printer_items.items().size(); i < n && !found.has_value(); ++i) {
            const auto& item = printer_items.items().at(i);
            if (item.hw_printer_config_name == sla_printer_name)
                found.emplace(item.hw_printer_config_id, item.id);
        }
        REQUIRE(found.has_value());
        presets().select_printer_preset(found->first, found->second);
        dispatcher.dispatch_enqueued();
        REQUIRE(presets().current_printer_config().technology == PrinterTechnology::SLA);
    }
};

TEST_CASE_METHOD(
    PhysicalPrinterFixture,
    "The upload destinations are the local drive, the removable drive and Prusa Connect",
    "[PhysicalPrinter][destination]"
)
{
    PhysicalPrinterInteractor& printers = physical_printers();
    const auto& list                    = printers.observable_list();

    // The fixture's own precondition: a current plate, which is the key a destination is
    // remembered under.
    REQUIRE(first_plate != Slic3r::Domain::INVALID_ID);
    REQUIRE(project_interactor.selected_config_container_id() == first_plate);

    // A profile with no host upload saved holds the three synthetic destinations and nothing else.
    REQUIRE(list.size() == 3);

    REQUIRE(list.at(0).name == _u8L("Local Drive"));
    REQUIRE(std::holds_alternative<FileSystemExport>(list.at(0).payload));
    REQUIRE_FALSE(std::get<FileSystemExport>(list.at(0).payload).prefer_removable);
    REQUIRE(list.at(1).name == _u8L("Removable Drive"));
    REQUIRE(std::holds_alternative<FileSystemExport>(list.at(1).payload));
    REQUIRE(std::get<FileSystemExport>(list.at(1).payload).prefer_removable);
    REQUIRE(list.at(2).name == _u8L("Prusa Connect"));
    REQUIRE(std::holds_alternative<ConnectUpload>(list.at(2).payload));

    REQUIRE(local_drive_uuid() == list.at(0).uuid);
    REQUIRE(removable_drive_uuid() == list.at(1).uuid);
    REQUIRE(connect_uuid() == list.at(2).uuid);

    // The local drive is selected without being asked for.
    REQUIRE(printers.selected_uuid() == local_drive_uuid());
    REQUIRE(printers.is_filesystem_export_selected());
    REQUIRE_FALSE(printers.is_printer_upload_selected());
    REQUIRE_FALSE(printers.is_connect_upload_selected());

    // Both folders the fixture provides are there, and they are not the same one.
    REQUIRE(fs::exists(local_folder()));
    REQUIRE(fs::exists(removable_drive()));
    REQUIRE(local_folder() != removable_drive());
}

TEST_CASE_METHOD(
    PhysicalPrinterFixture,
    "Both drive destinations write a file, and only Prusa Connect needs an account",
    "[PhysicalPrinter][destination]"
)
{
    PhysicalPrinterInteractor& printers = physical_printers();

    // The removable drive is the same kind of destination as the local one: selecting it keeps the
    // export a file write, and prefer_removable is the flag the export dialog reads to pick the
    // drive's folder.
    printers.select_uuid(removable_drive_uuid());
    REQUIRE(printers.selected_uuid() == removable_drive_uuid());
    REQUIRE(printers.is_filesystem_export_selected());
    REQUIRE_FALSE(printers.is_printer_upload_selected());
    REQUIRE_FALSE(printers.is_connect_upload_selected());

    const PhysicalPrinterConfig& selected = printers.selected_physical_printer_data();
    REQUIRE(selected.uuid == removable_drive_uuid());
    REQUIRE(std::holds_alternative<FileSystemExport>(selected.payload));
    REQUIRE(std::get<FileSystemExport>(selected.payload).prefer_removable);

    // Nothing but Connect talks to a service, and a test project has no account.
    REQUIRE(printers.can_be_selected(local_drive_uuid()));
    REQUIRE(printers.can_be_selected(removable_drive_uuid()));
    REQUIRE_FALSE(printers.can_be_selected(connect_uuid()));

    printers.select_uuid(connect_uuid());
    REQUIRE(printers.is_connect_upload_selected());
    REQUIRE_FALSE(printers.is_filesystem_export_selected());

    // select_default() is what every caller falls back to, and it is the local drive.
    printers.select_default();
    REQUIRE(printers.selected_uuid() == local_drive_uuid());
    REQUIRE(printers.is_filesystem_export_selected());
}

TEST_CASE_METHOD(
    PhysicalPrinterFixture,
    "The app's own choice of destination is Connect unless a printer is already picked",
    "[PhysicalPrinter][destination]"
)
{
    PhysicalPrinterInteractor& printers = physical_printers();

    // Nothing is picked but the local drive, so the Connect entry is taken.
    printers.select_connect_upload(true);
    REQUIRE(printers.selected_uuid() == connect_uuid());

    // A picked destination is kept: prefer_physical_printer means a printer the user chose wins
    // over the generic Connect entry.
    printers.select_uuid(removable_drive_uuid());
    printers.select_connect_upload(true);
    REQUIRE(printers.selected_uuid() == removable_drive_uuid());

    // Without the preference the Connect entry is taken either way.
    printers.select_connect_upload(false);
    REQUIRE(printers.selected_uuid() == connect_uuid());
}

TEST_CASE_METHOD(
    PhysicalPrinterFixture,
    "A drive destination fits every printer profile, a host upload only the printer it was added for",
    "[PhysicalPrinter][destination]"
)
{
    PhysicalPrinterInteractor& printers = physical_printers();

    const HwPrinterConfig& fff = presets().current_printer_config();
    REQUIRE(fff.technology == PrinterTechnology::FFF);

    // The same hardware config as an SLA printer, which is what the plate of the last test case is
    // switched to.
    HwPrinterConfig sla = fff;
    sla.technology      = PrinterTechnology::SLA;
    sla.model           = HwModel{"Photon Mono M5", "Photon Mono M5"};
    sla.tools.clear();
    sla.feeders.clear();
    sla.materials.clear();

    // A filesystem destination never talks to a printer, so it fits an FFF and an SLA profile alike,
    // and so does Connect. This is what a destination has to satisfy to survive a printer change.
    REQUIRE(printers.is_printer_compatible(local_drive_uuid(), fff));
    REQUIRE(printers.is_printer_compatible(local_drive_uuid(), sla));
    REQUIRE(printers.is_printer_compatible(removable_drive_uuid(), fff));
    REQUIRE(printers.is_printer_compatible(removable_drive_uuid(), sla));
    REQUIRE(printers.is_printer_compatible(connect_uuid(), fff));
    REQUIRE(printers.is_printer_compatible(connect_uuid(), sla));

    // A host upload is bound to the printer it was added for: the same hardware config fits, an SLA
    // one does not, and neither does another FFF printer.
    const std::string upload = add_host_upload("FFF print host");
    REQUIRE_FALSE(upload.empty());
    REQUIRE(printers.is_printer_compatible(upload, fff));
    REQUIRE_FALSE(printers.is_printer_compatible(upload, sla));

    HwPrinterConfig other_fff = fff;
    other_fff.model           = HwModel{"MK4", "MK4"};
    REQUIRE_FALSE(printers.is_printer_compatible(upload, other_fff));

    // Adding one selected it, and a printer upload is the one destination kind that is not a file
    // write.
    REQUIRE(printers.selected_uuid() == upload);
    REQUIRE(printers.is_printer_upload_selected());
    REQUIRE_FALSE(printers.is_filesystem_export_selected());

    // It is persisted as one JSON file per uuid in the local folder the data dir points at.
    REQUIRE(fs::exists(fs::path(Slic3r::data_dir()) / "physical_printer" / (upload + ".json")));
}

TEST_CASE_METHOD(
    PhysicalPrinterFixture,
    "Every plate remembers its own destination",
    "[PhysicalPrinter][destination]"
)
{
    PhysicalPrinterInteractor& printers = physical_printers();

    printers.select_uuid(removable_drive_uuid());
    REQUIRE(printers.selected_uuid() == removable_drive_uuid());

    // Adding the second plate selects it. The removable drive is still the destination: a
    // filesystem destination is compatible with every printer profile, so nothing resets it and the
    // file does not silently land on the local disk instead of the drive.
    const Slic3r::Domain::SelectionId second_plate = project_interactor.add_config_container();
    REQUIRE(second_plate != first_plate);
    REQUIRE(project_interactor.selected_config_container_id() == second_plate);
    REQUIRE(printers.selected_uuid() == removable_drive_uuid());

    // The second plate remembers its own choice...
    printers.select_default();
    REQUIRE(printers.selected_uuid() == local_drive_uuid());

    // ...and going back and forth between the plates restores each of them.
    project_interactor.select_config_container(first_plate);
    REQUIRE(printers.selected_uuid() == removable_drive_uuid());
    project_interactor.select_config_container(second_plate);
    REQUIRE(printers.selected_uuid() == local_drive_uuid());
}

TEST_CASE_METHOD(
    PhysicalPrinterFixture,
    "A plate whose printer cannot use the picked host upload falls back to the local drive",
    "[PhysicalPrinter][destination]"
)
{
    PhysicalPrinterInteractor& printers = physical_printers();

    // An FFF plate with a host upload picked for it, which is remembered for that plate.
    const std::string upload = add_host_upload("FFF print host");
    REQUIRE(printers.is_printer_upload_selected());
    project_interactor.select_config_container(first_plate);
    REQUIRE(printers.selected_uuid() == upload);

    // Switch that plate to the SLA printer, so the upload no longer fits it.
    select_sla_printer();
    REQUIRE_FALSE(printers.is_printer_compatible(upload, presets().current_printer_config()));

    // The second plate is added on the SLA printer and is selected, which is the notification the
    // app sends whenever a plate becomes the current one. Nothing is remembered for it yet, the
    // host upload cannot use an SLA printer, so the destination falls back to the default instead
    // of uploading a .pm5 to an FFF print host.
    const Slic3r::Domain::SelectionId second_plate = project_interactor.add_config_container();
    REQUIRE(second_plate != first_plate);
    REQUIRE(presets().current_printer_config().technology == PrinterTechnology::SLA);
    REQUIRE(printers.selected_uuid() == local_drive_uuid());
    REQUIRE(printers.is_filesystem_export_selected());

    // Going back to the FFF plate restores the upload it remembers, and the SLA plate restores its
    // own fallback: the memory is per plate, and the compatibility check only runs for a plate that
    // has no destination of its own yet.
    project_interactor.select_config_container(first_plate);
    REQUIRE(printers.selected_uuid() == upload);
    project_interactor.select_config_container(second_plate);
    REQUIRE(printers.selected_uuid() == local_drive_uuid());
}

} // namespace
