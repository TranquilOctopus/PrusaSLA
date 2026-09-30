#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <boost/filesystem/operations.hpp>
#include <boost/nowide/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <nlohmann/json.hpp>

#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Config/ConfigLoad.hpp"
#include "Slic3r/Biz/Preset/IO/BundlePaths.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Slicing/TestUtils.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/Preset/EvaluatedPreset.hpp"
#include "Slic3r/Domain/SLA/RaftPreset.hpp"
#include "Slic3r/Domain/SlaLayerHeight.hpp"
#include "Slic3r/TestUtils/AppInstanceMessageHandlerScope.hpp"
#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/TestData.hpp"

// The presets and projects an installed app has were written by earlier builds of it, before this
// fork moved settings around and hid others. A file of that age must still load, and must still
// mean what it meant: the worst outcome is a crash, the second worst a print that quietly is not
// the one the user saved.

namespace Slic3r::Test {

namespace fs = boost::filesystem;

/// The generic MSLA printer of the shipped community-sla bundle. That bundle is what the presets
/// below are written against, so it is the one printer this file needs.
constexpr std::string_view m5_printer = "Photon Mono M5";
/// The vendor of that bundle and the repository folder it sits in. A user preset of this vendor
/// lives in <user presets>/<repo>/<vendor>/, which is where load_bundle looks for it.
constexpr std::string_view community_vendor = "CommunitySLA";
constexpr std::string_view community_repo   = "community-sla";

// The presets below are written the way an earlier build of this app wrote a saved preset: the
// file carries the kind, the variant carries the id, the name and only what the preset changed,
// and what it inherits from the system preset it was made from. What makes them old is the key
// sets they name, not the shape of the file.
//
// pad_enable and pad_around_object are the raft of before raft_type replaced them. Both are
// hidden but still defined, so a preset of that age names them and no raft_type at all.
constexpr std::string_view old_raft_around_object = R"yaml(kind: sla_print
variants:
- id: 11111111-1111-4111-8111-111111111101
  name: Old raft around object
  unconditional_inherits:
  - community_sla_print_005
  values:
    pad_enable: 1
    pad_around_object: 1
    layer_height: 0.05
    faded_layers: 8
)yaml";

constexpr std::string_view old_raft_full_plate = R"yaml(kind: sla_print
variants:
- id: 11111111-1111-4111-8111-111111111102
  name: Old full plate raft
  unconditional_inherits:
  - community_sla_print_005
  values:
    pad_enable: 1
    pad_around_object: 0
)yaml";

constexpr std::string_view old_no_raft = R"yaml(kind: sla_print
variants:
- id: 11111111-1111-4111-8111-111111111103
  name: Old no raft
  unconditional_inherits:
  - community_sla_print_005
  values:
    pad_enable: 0
    pad_around_object: 0
)yaml";

// A preset of this build: it names raft_type and leaves the two checkboxes it inherited from the
// system preset at a value raft_type contradicts. raft_type is the source of truth, so those two
// are what gets ignored.
constexpr std::string_view new_raft_type = R"yaml(kind: sla_print
variants:
- id: 11111111-1111-4111-8111-111111111104
  name: New raft type
  unconditional_inherits:
  - community_sla_print_005
  values:
    pad_enable: 1
    pad_around_object: 1
    raft_type: full
    layer_height: 0.05
    faded_layers: 8
)yaml";

// support_type and support_xy_size are upstream SLA settings this fork does not have, so a preset
// written by another version of the app names them and this build has to load it anyway.
constexpr std::string_view upstream_keys = R"yaml(kind: sla_print
variants:
- id: 11111111-1111-4111-8111-111111111105
  name: Old keys and an upstream key
  unconditional_inherits:
  - community_sla_print_005
  values:
    pad_enable: 1
    pad_around_object: 0
    support_type: tree
    support_xy_size: 1.5
    layer_height: 0.05
)yaml";

// A resin from before the layer settings moved into the resin preset: no resin_layer_height, no
// resin_faded_layers and no initial_layer_height at all, so the print preset keeps them.
// material_source_note is hidden since M1.13d3 but still defined, and still read.
constexpr std::string_view old_resin = R"yaml(kind: sla_material
variants:
- id: 22222222-2222-4222-8222-222222222201
  name: Old resin
  values:
    exposure_time: 3.5
    initial_exposure_time: 35
    material_source_note: a note an old resin preset carried
)yaml";

// A resin of this build: it brings its own layer height, transition layers and first layer height,
// which is what decides them for the whole print.
constexpr std::string_view resin_with_layer_height = R"yaml(kind: sla_material
variants:
- id: 22222222-2222-4222-8222-222222222202
  name: Resin with its own layer height
  values:
    exposure_time: 3.5
    initial_exposure_time: 35
    resin_layer_height: 0.03
    resin_faded_layers: 0
    initial_layer_height: 0.1
)yaml";

/// A project written by another version of the app: two settings this build has never heard of,
/// and a raft named by the two checkboxes raft_type replaced. This is the "configuration" object
/// of a 3MF, which is what the project reader hands to Config::load().
nlohmann::ordered_json old_project_configuration()
{
    return {
        {"sla_printer_settings",
         {{"printer_technology", "SLA"}, {"support_density", 3.5}}},
        {"sla_material_settings", {{"exposure_time", 3.5}, {"sla_hollowing_closing_distance", 0.5}}},
        {"sla_print_settings",
         {{"layer_height", 0.05},
          {"faded_layers", 8},
          {"pad_enable", true},
          {"pad_around_object", true}}},
    };
}

/// The scratch tree the test writes into, plus the data dir pointing at it. Declared as the first
/// member on purpose, so it is destroyed last: the preset bundle the interactor owns points into
/// this folder and into this data dir, so the folder may only be removed and the previous data dir
/// restored once the interactor itself is gone.
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
        // The non-throwing overload on purpose: a throwing remove_all() out of a destructor would
        // terminate the whole test run instead of failing one test.
        boost::system::error_code ec;
        fs::remove_all(path, ec);
    }

    ScratchDir(const ScratchDir&)            = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
};

/// The shipped bundle with a folder of user presets in front of it, holding presets written by
/// earlier builds. Everything asked of the interactor is answered through this bundle, which is
/// how the app reads the presets of an installed copy after an update.
struct OldPresetFixture
{
    Biz::Preset::IO::BundlePaths bundle_paths;
    ScratchDir scratch{Tests::get_datadir() / "old_preset_scratch"};

    Domain::Workbench workbench;
    App::Platform::StdMainThreadDispatcher dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope{dispatcher};
    Tests::JobManagerScope job_manager_scope{dispatcher};
    MockThumbnailImageGenerator thumbnail_image_generator;
    Biz::ProjectInteractor project_interactor{workbench, dispatcher, thumbnail_image_generator};

    OldPresetFixture()
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

        write_user_preset("sla-print-old-raft-around-object.yaml", old_raft_around_object);
        write_user_preset("sla-print-old-full-plate-raft.yaml", old_raft_full_plate);
        write_user_preset("sla-print-old-no-raft.yaml", old_no_raft);
        write_user_preset("sla-print-new-raft-type.yaml", new_raft_type);
        write_user_preset("sla-print-upstream-keys.yaml", upstream_keys);
        write_user_preset("material-old-resin.yaml", old_resin);
        write_user_preset("material-resin-with-layer-height.yaml", resin_with_layer_height);

        project_interactor.preset_interactor().set_use_hw_config_short_name(false);
        project_interactor.preset_interactor().load_preset_bundle(bundle_paths);
        project_interactor.new_project();
    }

    ~OldPresetFixture()
    {
        // Selecting a printer and its presets posts main-thread work, and close() runs what is left
        // while the interactor and the preset bundle are both still alive. Removing the tree and
        // restoring the data dir is the ScratchDir member's job, which runs after every other
        // member is gone.
        dispatcher.close();
    }

    OldPresetFixture(const OldPresetFixture&) = delete;
    OldPresetFixture& operator=(const OldPresetFixture&) = delete;

    void write_user_preset(const std::string& file_name, std::string_view contents)
    {
        // The folder a user preset of that vendor lives in, which is where load_bundle looks.
        const fs::path path = scratch.path / "user" / fs::path{community_repo}
                              / fs::path{community_vendor} / file_name;
        fs::create_directories(path.parent_path());
        boost::nowide::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << contents;
    }

    void select_printer(std::string_view hw_config_name)
    {
        Biz::Preset::PresetInteractor& presets = project_interactor.preset_interactor();
        const Biz::Preset::PresetItemObservableList& printers = presets.printer_presets();
        std::optional<std::pair<std::string, std::string>> found; // hw printer config id, preset id
        for (size_t i = 0, n = printers.items().size(); i < n && !found.has_value(); ++i) {
            const Biz::Preset::PresetItem& item = printers.items().at(i);
            if (item.hw_printer_config_name == hw_config_name)
                found.emplace(item.hw_printer_config_id, item.id);
        }
        REQUIRE(found.has_value());
        presets.select_printer_preset(found->first, found->second);
    }

    void select_print(std::string_view name)
    {
        Biz::Preset::PresetInteractor& presets = project_interactor.preset_interactor();
        const Biz::Preset::PresetItemObservableList& prints = presets.print_presets();
        for (size_t i = 0, n = prints.items().size(); i < n; ++i) {
            if (prints.items().at(i).name == name) {
                presets.select_print_preset(prints.items().at(i).id);
                return;
            }
        }

        FAIL("There is no print preset named " << name << ".");
    }

    void select_resin(std::string_view name)
    {
        Biz::Preset::PresetInteractor& presets = project_interactor.preset_interactor();
        const Domain::Preset::SelectedPreset& selected = presets.selected_printer_preset();
        for (const auto& [preset, is_runtime] : presets.get_material_presets(
                 project_interactor.selected_project_id(),
                 selected.hw_config.id,
                 selected.printer.id,
                 selected.print.id,
                 0
             ))
        {
            if (preset.get().name == name) {
                presets.select_material_preset(0, preset.get().id);
                return;
            }
        }

        FAIL("There is no resin preset named " << name << ".");
    }

    /// The evaluated print preset of the selected printer with the given name, as the bundle
    /// evaluates it. Null when there is no such preset.
    const Domain::ConfigBox* print_config(std::string_view name)
    {
        Biz::Preset::PresetInteractor& presets = project_interactor.preset_interactor();
        const Domain::Preset::SelectedPreset& selected = presets.selected_printer_preset();
        for (const auto& [preset, is_runtime] : presets.get_print_presets(
                 project_interactor.selected_project_id(),
                 selected.hw_config.id,
                 selected.printer.id
             ))
        {
            if (preset.get().name == name)
                return &preset.get().config_box();
        }
        return nullptr;
    }

    /// The evaluated resin preset of the selected printer, print preset and resin slot with the
    /// given name. Null when there is no such preset.
    const Domain::ConfigBox* resin_config(std::string_view name)
    {
        Biz::Preset::PresetInteractor& presets = project_interactor.preset_interactor();
        const Domain::Preset::SelectedPreset& selected = presets.selected_printer_preset();
        for (const auto& [preset, is_runtime] : presets.get_material_presets(
                 project_interactor.selected_project_id(),
                 selected.hw_config.id,
                 selected.printer.id,
                 selected.print.id,
                 0
             ))
        {
            if (preset.get().name == name)
                return &preset.get().config_box();
        }
        return nullptr;
    }

    /// The three boxes the selected printer, print preset and resin add up to, the way the
    /// exporters and the slicer read them.
    Domain::ConfigPackSLA sla_config()
    {
        const Domain::ConfigPack pack
            = project_interactor.selected_config_container().build_print_config();
        const auto* sla = std::get_if<Domain::ConfigPackSLA>(&pack);
        REQUIRE(sla != nullptr);
        return *sla;
    }

    /// The configuration of the selected printer, print preset and resin as one finalized view,
    /// which is what the effective-value helpers take.
    Domain::ConfigView config_view()
    {
        const Domain::Preset::SelectedPreset& selected = project_interactor.preset_interactor()
                                                              .selected_printer_preset();
        const Domain::ConfigPackSLA pack                  = sla_config();
        const auto full_config = std::make_shared<const Domain::FullConfigSLA>(
            pack, selected.hw_config
        );
        Domain::ConfigView view{full_config, {}};
        view.finalize();
        return view;
    }
};

} // namespace Slic3r::Test

// A print preset of before raft_type named the raft with the two checkboxes raft_type replaced.
// They are hidden but still defined, so such a preset loads; what it must not do is end up
// printing the raft raft_type's own default stands for, which is a different shape than the one
// it asked for.
TEST_CASE_METHOD(
    Slic3r::Test::OldPresetFixture,
    "A print preset from before raft_type prints the raft it asked for",
    "[Config][Sla][Raft][Legacy]"
)
{
    using Slic3r::Domain::sla::RaftType;

    select_printer(Slic3r::Test::m5_printer);

    SECTION("a raft around the object is still a raft around the object")
    {
        select_print("Old raft around object");
        const Slic3r::Domain::ConfigBox* box = print_config("Old raft around object");
        REQUIRE(box != nullptr);
        // The checkboxes of that age are still read and still say what they said.
        CHECK(box->items.opt("pad_enable").get<bool>());
        CHECK(box->items.opt("pad_around_object").get<bool>());
        CHECK(box->items.opt("raft_type").get<RaftType>() == RaftType::AroundObject);
    }

    SECTION("a full plate raft is still a full plate raft")
    {
        select_print("Old full plate raft");
        const Slic3r::Domain::ConfigBox* box = print_config("Old full plate raft");
        REQUIRE(box != nullptr);
        CHECK(box->items.opt("pad_enable").get<bool>());
        CHECK_FALSE(box->items.opt("pad_around_object").get<bool>());
        CHECK(box->items.opt("raft_type").get<RaftType>() == RaftType::Full);
    }

    SECTION("a preset that asked for no raft does not get one")
    {
        select_print("Old no raft");
        const Slic3r::Domain::ConfigBox* box = print_config("Old no raft");
        REQUIRE(box != nullptr);
        CHECK_FALSE(box->items.opt("pad_enable").get<bool>());
        CHECK(box->items.opt("raft_type").get<RaftType>() == RaftType::None);
    }

    SECTION("raft_type wins over the checkboxes it replaced")
    {
        select_print("New raft type");
        const Slic3r::Domain::ConfigBox* box = print_config("New raft type");
        REQUIRE(box != nullptr);
        // The preset asks for a full plate raft and leaves the two inherited checkboxes at a raft
        // around the object, which is what raft_type is there to overrule.
        CHECK(box->items.opt("pad_around_object").get<bool>());
        CHECK(box->items.opt("raft_type").get<RaftType>() == RaftType::Full);
    }
}

// The layer height and the transition layer count moved into the resin preset in M1.13c. A resin
// written before the move carries none of them, so the print preset keeps them and the print is
// sliced with the values it was saved with.
TEST_CASE_METHOD(
    Slic3r::Test::OldPresetFixture,
    "A resin from before the layer settings keeps the print preset values",
    "[Config][Sla][LayerHeight][Legacy]"
)
{
    using Slic3r::Domain::sla_effective_faded_layers;
    using Slic3r::Domain::sla_effective_initial_layer_height;
    using Slic3r::Domain::sla_effective_layer_height;

    select_printer(Slic3r::Test::m5_printer);
    select_print("Old raft around object");

    SECTION("a resin without the layer settings falls back to the print preset")
    {
        select_resin("Old resin");
        const Slic3r::Domain::ConfigBox* box = resin_config("Old resin");
        REQUIRE(box != nullptr);
        // None of the three is in the preset, so each option keeps its unset value.
        CHECK(box->items.opt("resin_layer_height").get<double>() == Catch::Approx(0.));
        CHECK(box->items.opt("initial_layer_height").get<double>() == Catch::Approx(0.));

        const Slic3r::Domain::ConfigView view = config_view();
        // The print preset of that age carries layer_height 0.05 and faded_layers 8.
        CHECK(sla_effective_layer_height(view) == Catch::Approx(0.05));
        CHECK(sla_effective_faded_layers(view) == 8);
        // An initial layer height of zero means the first layer is an ordinary one.
        CHECK(sla_effective_initial_layer_height(view) == Catch::Approx(0.05));
    }

    SECTION("a resin that brings its own layer height decides it for the print")
    {
        select_resin("Resin with its own layer height");
        const Slic3r::Domain::ConfigView view = config_view();
        CHECK(sla_effective_layer_height(view) == Catch::Approx(0.03));
        // Zero transition layers is a value, not an unset one, so the print preset cannot win.
        CHECK(sla_effective_faded_layers(view) == 0);
        CHECK(sla_effective_initial_layer_height(view) == Catch::Approx(0.1));
    }
}

// M1.13d3 hid the settings that do nothing for SLA and kept every definition, so a preset of that
// time still names them. Hiding a setting is only safe as long as it keeps loading.
TEST_CASE_METHOD(
    Slic3r::Test::OldPresetFixture,
    "Presets that set the settings this fork hid still load",
    "[Config][Sla][Hidden][Legacy]"
)
{
    select_printer(Slic3r::Test::m5_printer);

    // The print box: the two raft checkboxes, hidden when raft_type replaced them.
    {
        select_print("Old raft around object");
        const Slic3r::Domain::ConfigBox* print = print_config("Old raft around object");
        REQUIRE(print != nullptr);
        CHECK(print->items.opt("pad_enable").get<bool>());
        CHECK(print->items.opt("pad_around_object").get<bool>());
    }

    // The resin box: material_source_note, which nothing reads but a third-party printer needs.
    {
        const Slic3r::Domain::ConfigBox* resin = resin_config("Old resin");
        REQUIRE(resin != nullptr);
        CHECK(resin->items.opt("material_source_note").get<std::string>()
              == "a note an old resin preset carried");
    }

    // The printer box: the four exposure bounds, which no SLA path clamps with. The shipped
    // community printer presets name all four, so a printer of that age needs no user copy.
    {
        // The config of a container is built out of the printer, the print preset and one resin,
        // so a resin has to be selected before the box can be read at all.
        select_resin("Old resin");
        const Slic3r::Domain::ConfigPackSLA config = sla_config();
        const Slic3r::Domain::ConfigItems& printer = config.sla_printer_settings.items;
        CHECK(printer.opt("min_exposure_time").get<double>() == Catch::Approx(1.));
        CHECK(printer.opt("max_exposure_time").get<double>() == Catch::Approx(120.));
        CHECK(printer.opt("min_initial_exposure_time").get<double>() == Catch::Approx(1.));
        CHECK(printer.opt("max_initial_exposure_time").get<double>() == Catch::Approx(300.));
    }
}

// A preset or a project of another version names settings this build does not have. They are
// dropped, not refused: the rest of the file is still what the print is sliced with, and the names
// that did not resolve are reported.
TEST_CASE_METHOD(
    Slic3r::Test::OldPresetFixture,
    "A file that names a setting this build does not have still loads",
    "[Config][Sla][Legacy]"
)
{
    using Slic3r::Biz::Config::BoxIssues;
    using Slic3r::Biz::Config::ItemParsingIssueType;
    using Slic3r::Domain::SLAConfigLocation;

    SECTION("a preset of another version keeps everything it still means")
    {
        select_printer(Slic3r::Test::m5_printer);
        select_print("Old keys and an upstream key");

        const Slic3r::Domain::ConfigBox* box = print_config("Old keys and an upstream key");
        REQUIRE(box != nullptr);
        // support_type and support_xy_size are gone, and no setting was lost with them.
        CHECK(box->find("support_type").item == nullptr);
        CHECK(box->find("support_xy_size").item == nullptr);
        CHECK(box->items.opt("layer_height").get<double>() == Catch::Approx(0.05));
        CHECK(box->items.opt("pad_enable").get<bool>());
    }

    SECTION("a project of another version loads and says what it could not read")
    {
        const Slic3r::Domain::Preset::HwPrinterConfig hw_config{
            .technology = Slic3r::Domain::PrinterTechnology::SLA};

        const auto result = Slic3r::Biz::Config::load(
            Slic3r::Test::old_project_configuration(), hw_config
        );
        REQUIRE(result.has_value());

        const Slic3r::Biz::Config::IssuesPerLocation& issues = result->issues;
        // The two settings this build has never heard of are reported, in the boxes they were in.
        const BoxIssues& printer_keys = std::get<BoxIssues>(issues.at(SLAConfigLocation::Printer));
        CHECK(printer_keys.at("support_density").type == ItemParsingIssueType::ExtraKey);
        const BoxIssues& material_keys = std::get<BoxIssues>(issues.at(SLAConfigLocation::Material));
        CHECK(material_keys.at("sla_hollowing_closing_distance").type
              == ItemParsingIssueType::ExtraKey);

        // What the project did say is what the print is sliced with.
        const Slic3r::Domain::ConfigPackSLA& config = std::get<Slic3r::Domain::ConfigPackSLA>(result->config);
        CHECK(config.sla_print_settings.items.opt("layer_height").get<double>() == Catch::Approx(0.05));
        CHECK(config.sla_print_settings.items.opt("faded_layers").get<int>() == 8);
        CHECK(config.sla_material_settings.items.opt("exposure_time").get<double>() == Catch::Approx(3.5));

        // The raft of that project is the one its two checkboxes asked for, and the checkboxes
        // themselves still say so.
        CHECK(config.sla_print_settings.items.opt("pad_enable").get<bool>());
        CHECK(config.sla_print_settings.items.opt("pad_around_object").get<bool>());
        CHECK(config.sla_print_settings.items.opt("raft_type").get<Slic3r::Domain::sla::RaftType>()
              == Slic3r::Domain::sla::RaftType::AroundObject);
    }
}

// The shipped community-sla bundle is what every user preset of that vendor inherits from, so a
// break in one of its chains would take every printer, resin and print preset of the vendor with
// it. Each of them has to come out of the evaluator whole.
TEST_CASE_METHOD(
    Slic3r::Test::OldPresetFixture,
    "The community-sla bundle resolves for every printer, resin and print",
    "[Config][Sla][Bundle][Legacy]"
)
{
    using Slic3r::Biz::Preset::PresetItem;
    using Slic3r::Biz::Preset::PresetItemObservableList;
    using Slic3r::Biz::Preset::PresetInteractor;
    using Slic3r::Domain::Preset::EvaluatedMaterialPreset;
    using Slic3r::Domain::Preset::EvaluatedPrintPreset;

    PresetInteractor& presets = project_interactor.preset_interactor();
    const PresetItemObservableList& printers = presets.printer_presets();

    // The list of printers is collected before anything is selected, so the walk below cannot be
    // looking at a list that a selection rebuilt.
    std::vector<std::pair<std::string, std::string>> all_printers; // hw printer config id, preset id
    for (size_t i = 0, n = printers.items().size(); i < n; ++i) {
        const PresetItem& item = printers.items().at(i);
        all_printers.emplace_back(item.hw_printer_config_id, item.id);
    }

    size_t community_printers = 0;
    for (const auto& [hw_config_id, printer_preset_id] : all_printers) {
        presets.select_printer_preset(hw_config_id, printer_preset_id);
        if (presets.current_printer_config().vendor_id != Slic3r::Test::community_vendor)
            continue;

        ++community_printers;
        INFO("printer " << presets.current_printer_config().name);

        // The printer preset of that printer, out of the vendor's own printer file.
        const Slic3r::Domain::Preset::SelectedPreset& selected = presets.selected_printer_preset();
        CHECK_FALSE(selected.printer.id.empty());
        CHECK(selected.printer.config_box().items.opt("printer_model").get<std::string>()
              == presets.current_printer_config().model.model);

        // Every print preset of the vendor resolves, its inherits chain and all.
        const auto prints = presets.get_print_presets(
            project_interactor.selected_project_id(),
            selected.hw_config.id,
            selected.printer.id
        );
        size_t print_count = 0;
        for (const auto& [preset, is_runtime] : prints) {
            ++print_count;
            const EvaluatedPrintPreset::Preset& evaluated = preset.get();
            INFO("print preset " << evaluated.name);
            // An id survives the evaluation, and the '*common*' of the bundle came through it.
            CHECK_FALSE(evaluated.id.empty());
            CHECK(evaluated.config_box().items.opt("layer_height").get<double>() > 0.);
            CHECK(evaluated.config_box().items.opt("support_pillar_diameter").get<double>() > 0.);
        }
        CHECK(print_count >= 3);

        // And every resin of the vendor.
        const auto resins = presets.get_material_presets(
            project_interactor.selected_project_id(),
            selected.hw_config.id,
            selected.printer.id,
            selected.print.id,
            0
        );
        size_t resin_count = 0;
        for (const auto& [preset, is_runtime] : resins) {
            ++resin_count;
            const EvaluatedMaterialPreset::Preset& evaluated = preset.get();
            INFO("resin preset " << evaluated.name);
            CHECK_FALSE(evaluated.id.empty());
            CHECK(evaluated.config_box().items.opt("exposure_time").get<double>() > 0.);

            // A resin of the bundle inherits '*common*', which is where its material type and its
            // layer height come from since M1.13c2. A user resin of this file stands on its own,
            // so it has neither, and that is the state it was saved in.
            if (evaluated.origin == Slic3r::Domain::Preset::PresetOrigin::User) {
                CHECK(evaluated.config_box().items.opt("material_type").get<std::string>().empty());
                CHECK(evaluated.config_box().items.opt("resin_layer_height").get<double>()
                      == Catch::Approx(0.));
            } else {
                CHECK(evaluated.config_box().items.opt("material_type").get<std::string>() == "Tough");
                CHECK(evaluated.config_box().items.opt("resin_layer_height").get<double>()
                      == Catch::Approx(0.05));
            }
        }
        CHECK(resin_count >= 2);
    }

    // The vendor of that bundle has printers, or the loop above checked nothing at all.
    CHECK(community_printers >= 6);
}
