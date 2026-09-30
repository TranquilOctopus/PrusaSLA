#include "Slic3r/Biz/Config/ConfigLoad.hpp"
#include <nlohmann/json.hpp>
#include <tl/expected.hpp>
#include "Slic3r/Log.hpp"
#include "Slic3r/Biz/Config/ConfigJson.hpp" // IWYU pragma: keep
#include "Slic3r/Biz/Config/SelectedPresetJson.hpp"
#include "Slic3r/Biz/Format/ProjectFileConstants.hpp"
#include "Slic3r/Domain/SLA/RaftPreset.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>


namespace Slic3r::Biz::Config {

using Domain::ConfigItem;
using Domain::ConfigLocation;
using Domain::ConfigPackFDM;
using Domain::ConfigPackSLA;
using Domain::EnumValueDef;
using Domain::EnumValueDefs;
using Domain::FDMConfigLocation;
using Domain::FilamentSettings;
using Domain::get_location_name;
using Domain::overloaded;
using Domain::PrinterSettings;
using Domain::PrintSettings;
using Domain::ProjectSettings;
using Domain::SLAConfigLocation;
using Domain::SLAMaterialSettings;
using Domain::SLAPrinterSettings;
using Domain::SLAPrintSettings;
using Domain::ToolPrintSettings;
using nlohmann::ordered_json;
using Domain::Vec2d;
using Domain::Percentage;
using Domain::FloatOrPercentage;
using Domain::is_std_vector_v;

namespace {

std::optional<std::string> get_enum_issue(const ordered_json& json_value, const EnumValueDefs& enum_def)
{
    if (!json_value.is_string()) {
        return "Value is not string!";
    }
    const auto serialized_value{json_value.get<std::string>()};

    const auto def_it{std::ranges::find_if(enum_def, [&](const EnumValueDef& def) {
        return def.str_serialized == serialized_value;
    })};

    if (def_it == enum_def.end()) {
        return "Value '" + serialized_value + "' is not possible enum value!";
    }
    return std::nullopt;
}

tl::expected<std::string, std::string> parse_enum(
    const ordered_json& json_value,
    const EnumValueDefs& enum_def
)
{
    if (auto issue{get_enum_issue(json_value, enum_def)}) {
        return tl::unexpected{*issue};
    }

    return json_value.get<std::string>();
}

tl::expected<std::vector<std::string>, std::string> parse_enum_vector(
    const ordered_json& json_value,
    const EnumValueDefs& enum_def
)
{
    if (!json_value.is_array()) {
        return tl::unexpected{"Value is not an array!"};
    }
    for (const auto& value : json_value) {
        if (auto issue{get_enum_issue(value, enum_def)}) {
            return tl::unexpected{*issue};
        }
    }

    return json_value.get<std::vector<std::string>>();
}


tl::expected<void, ItemParsingIssue> fill_item(ConfigItem& item, const ordered_json& json_value)
{
    const auto map_error{[&](const std::string& error) {
        return ItemParsingIssue{.type = ItemParsingIssueType::InvalidFormat, .message = error};
    }};

    return item.visit(overloaded(
        [&](Domain::EnumWrapper& enum_wrapper) {
            return parse_enum(json_value, enum_wrapper.def())
                .map([&](const std::string& value) { enum_wrapper.set_string(value); })
                .map_error(map_error);
        },
        [&](Domain::EnumVectorWrapper& enum_vector_wrapper) {
            return parse_enum_vector(json_value, enum_vector_wrapper.def())
                .map([&](const std::vector<std::string>& value) {
                    enum_vector_wrapper.set_strings(value);
                })
                .map_error(map_error);
        },
        [&](auto& box_value) -> tl::expected<void, ItemParsingIssue> {
            using ValueType = std::remove_cvref_t<decltype(box_value)>;
            return parse<ValueType>(json_value)
                .map([&](const ValueType& value) { box_value = value; })
                .map_error(map_error);
        }
    ));
}

std::size_t get_most_common_size(const ordered_json& json)
{
    std::map<std::size_t, std::size_t> sizes_counts;

    for (const auto& [key, value] : json.items()) {
        if (!value.is_array()) {
            continue;
        }
        sizes_counts[value.size()]++;
    }

    if (sizes_counts.empty()) {
        return 0;
    }

    const auto it{std::ranges::max_element(sizes_counts, [](const auto& pair_a, const auto& pair_b) {
        return pair_a.second < pair_b.second;
    })};
    return it->first;
}

template<typename Settings>
struct BoxesLoadResult
{
    std::vector<Settings> settings;
    std::vector<BoxIssues> issues;
};

template<typename Settings>
BoxesLoadResult<Settings> load_boxes(const ordered_json& json)
{
    ASSERT(json.is_object());

    std::set<std::string> json_keys;
    const std::size_t boxes_count{std::max(std::size_t{1}, get_most_common_size(json))};

    std::vector<ordered_json> json_per_box(boxes_count, ordered_json::object());
    for (const auto& [key, value] : json.items()) {
        if (!value.is_array()) {
            continue;
        }
        for (std::size_t i{}; i < value.size(); ++i) {
            if (i >= json_per_box.size()) {
                continue;
            }
            json_per_box.at(i)[key] = value.at(i);
        }
    }

    BoxesLoadResult<Settings> result;
    for (const ordered_json& box_json : json_per_box) {
        Settings settings;
        const auto issues = load_box(box_json, settings);
        result.settings.push_back(settings);
        result.issues.push_back(issues);
    }
    return result;
}

bool is_object(const std::string& location_name, const ordered_json& json)
{
    return json.contains(location_name) && json[location_name].is_object();
}

// A project written before raft_type named the raft with the two checkboxes raft_type replaced.
// The type those two meant is filled in here, so the project prints the raft it was saved with
// instead of the one raft_type defaults to. The checkboxes keep the values the file gave them,
// and a file that names raft_type keeps that.
void fill_in_legacy_raft_type(SLAPrintSettings& print_settings, const ordered_json& print_json)
{
    if (print_json.contains("raft_type"))
        return;

    // The boxes of a project carry every option of their location, so a missing checkbox reads as
    // the default it had before raft_type, which is what that project would have printed.
    const Domain::ConfigItem* pad_enable         = print_settings.items.find("pad_enable");
    const Domain::ConfigItem* pad_around_object = print_settings.items.find("pad_around_object");
    if (pad_enable == nullptr)
        return;

    print_settings.items.opt("raft_type")
        .set(Domain::SLA::raft_type_of_legacy_pad(
            pad_enable->get<bool>(), pad_around_object != nullptr && pad_around_object->get<bool>()
        ));
}

bool is_empty(const std::vector<BoxIssues>& issues)
{
    for (const BoxIssues& box_issues : issues) {
        if (!box_issues.empty()) {
            return false;
        }
    }
    return true;
}
} // namespace

BoxIssues load_box(const ordered_json& json, Domain::ConfigBox& result)
{
    ASSERT(json.is_object());

    BoxIssues issues;

    std::set<std::string> json_keys;
    for (const auto& [key, _] : json.items()) {
        json_keys.insert(key);
    }

    for (ConfigItem& item : result.items.all_items()) {
        const auto it{json.find(item.name())};
        if (it != json.end()) {
            fill_item(item, *it).or_else([&](const ItemParsingIssue& issue) {
                issues[item.name()] = issue;
            });
            json_keys.erase(item.name());
        } else {
            issues[item.name()] = {ItemParsingIssueType::NotFound};
        }
    }

    Domain::ConfigOverrides& overrides{result.overrides};
    for (ConfigItem& item : overrides.all_items()) {
        const auto it{json.find(item.name())};
        if (it != json.end()) {
            if (!it->is_null()) {
                fill_item(item, *it) //
                    .map([&]() { overrides.enable(item.name()); })
                    .or_else([&](const ItemParsingIssue& issue) { issues[item.name()] = issue; });
            }
            json_keys.erase(item.name());
        }
    }

    for (const std::string& key : json_keys) {
        issues[key] = {ItemParsingIssueType::ExtraKey};
    }

    return issues;
}

tl::expected<LoadResult, GlobalParsingIssue>
load_fdm(const ordered_json& json, const Domain::Preset::HwPrinterConfig& hw_config)
{
    const std::string printer_location_name{get_location_name(FDMConfigLocation::Printer)};
    if (!is_object(printer_location_name, json)) {
        return tl::unexpected{GlobalParsingIssue::InvalidFDMPrinterSettings};
    }
    PrinterSettings printer_settings;
    const auto printer_issues = load_box(json[printer_location_name], printer_settings);

    const std::string tool_location_name{get_location_name(FDMConfigLocation::Tool)};
    if (!is_object(tool_location_name, json)) {
        return tl::unexpected{GlobalParsingIssue::InvalidFDMToolSettings};
    }
    auto tool_load_result{load_boxes<ToolPrintSettings>(json[tool_location_name])};
    if (tool_load_result.settings.empty()) {
        return tl::unexpected{GlobalParsingIssue::InvalidFDMToolSettings};
    }

    const std::string print_location_name{get_location_name(FDMConfigLocation::Print)};
    if (!is_object(print_location_name, json)) {
        return tl::unexpected{GlobalParsingIssue::InvalidFDMPrintSettings};
    }
    PrintSettings print_settings;
    const auto print_issues = load_box(json[print_location_name], print_settings);

    const std::string filament_location_name{get_location_name(FDMConfigLocation::Filament)};
    if (!is_object(filament_location_name, json)) {
        return tl::unexpected{GlobalParsingIssue::InvalidFDMFilamentSettings};
    }
    const auto filament_load_result{load_boxes<FilamentSettings>(json[filament_location_name])};
    if (filament_load_result.settings.empty()) {
        return tl::unexpected{GlobalParsingIssue::InvalidFDMFilamentSettings};
    }
    if (filament_load_result.settings.size() != tool_load_result.settings.size()) {
        // In case of empty (no) tool settings, we can just expand those to hw_config.tool_count
        if (tool_load_result.settings.size() == 1
            && tool_load_result.settings.front().items.all_items().empty()
            && tool_load_result.settings.front().overrides.overridden_items().empty())
        {
            tool_load_result.settings.resize(hw_config.tool_count);
        } else {
            return tl::unexpected{GlobalParsingIssue::FilamentsAndToolsCountIsNotEqual};
        }
    }

    const std::string project_location_name{get_location_name(FDMConfigLocation::Project)};
    if (!is_object(project_location_name, json)) {
        return tl::unexpected{GlobalParsingIssue::InvalidFDMProjectSettings};
    }
    ProjectSettings project_settings;
    const auto project_issues = load_box(json[project_location_name], project_settings);

    ConfigPackFDM config;
    config.printer = printer_settings;
    config.tool = tool_load_result.settings;
    config.print = print_settings;
    config.filament = filament_load_result.settings;
    config.project = project_settings;

    IssuesPerLocation issues;
    if (!printer_issues.empty()) {
        issues.insert({FDMConfigLocation::Printer, printer_issues});
    }
    if (!is_empty(tool_load_result.issues)) {
        issues.insert({FDMConfigLocation::Tool, tool_load_result.issues});
    }
    if (!print_issues.empty()) {
        issues.insert({FDMConfigLocation::Print, print_issues});
    }
    if (!is_empty(filament_load_result.issues)) {
        issues.insert({FDMConfigLocation::Filament, filament_load_result.issues});
    }
    if (!project_issues.empty()) {
        issues.insert({FDMConfigLocation::Project, project_issues});
    }

    return LoadResult{.config = std::move(config), .issues = std::move(issues)};
}

tl::expected<LoadResult, GlobalParsingIssue> load_sla(const ordered_json& json)
{
    const std::string printer_location_name{get_location_name(SLAConfigLocation::Printer)};
    if (!is_object(printer_location_name, json)) {
        return tl::unexpected{GlobalParsingIssue::InvalidSLAPrinterSettings};
    }
    SLAPrinterSettings printer_settings;
    const auto printer_issues = load_box(json[printer_location_name], printer_settings);

    const std::string material_location_name{get_location_name(SLAConfigLocation::Material)};
    if (!is_object(material_location_name, json)) {
        return tl::unexpected{GlobalParsingIssue::InvalidSLAMaterialSettings};
    }
    SLAMaterialSettings material_settings;
    const auto material_issues = load_box(json[material_location_name], material_settings);

    const std::string print_location_name{get_location_name(SLAConfigLocation::Print)};
    if (!is_object(print_location_name, json)) {
        return tl::unexpected{GlobalParsingIssue::InvalidSLAPrintSettings};
    }
    SLAPrintSettings print_settings;
    const auto print_issues = load_box(json[print_location_name], print_settings);
    fill_in_legacy_raft_type(print_settings, json[print_location_name]);

    IssuesPerLocation issues;
    if (!printer_issues.empty()) {
        issues.insert({SLAConfigLocation::Printer, printer_issues});
    }
    if (!material_issues.empty()) {
        issues.insert({SLAConfigLocation::Material, material_issues});
    }
    if (!print_issues.empty()) {
        issues.insert({SLAConfigLocation::Print, print_issues});
    }

    return LoadResult{
        .config =
            ConfigPackSLA{
                .sla_printer_settings = printer_settings,
                .sla_material_settings = material_settings,
                .sla_print_settings = print_settings,
            },
        .issues = std::move(issues)
    };
}

std::optional<std::string> parse_technology_at_location(const ordered_json& json, const std::string& location)
{
    if (!is_object(location, json)) {
        return std::nullopt;
    }
    if (!json[location].contains("printer_technology")) {
        return std::nullopt;
    }
    if (!json[location]["printer_technology"].is_string()) {
        return std::nullopt;
    }
    return json[location]["printer_technology"].get<std::string>();
}

std::optional<std::string> parse_technology(const ordered_json& json) {
    const std::string fdm_printer_location_name{get_location_name(FDMConfigLocation::Printer)};
    const std::string sla_printer_location_name{get_location_name(SLAConfigLocation::Printer)};

    if (const auto technology{parse_technology_at_location(json, fdm_printer_location_name)}) {
        if (technology == "FFF") {
            return technology;
        }
    }

    if (const auto technology{parse_technology_at_location(json, sla_printer_location_name)}) {
        if (technology == "SLA") {
            return technology;
        }
    }

    return std::nullopt;
}

tl::expected<LoadResult, GlobalParsingIssue>
load(const ordered_json& json, const Domain::Preset::HwPrinterConfig& hw_config)
{
    if (!json.is_object()) {
        return tl::unexpected{GlobalParsingIssue::NotAJsonObject};
    }

    if (parse_technology(json) == "FFF") {
        return load_fdm(json, hw_config);
    } else if (parse_technology(json) == "SLA") {
        return load_sla(json);
    } else {
        return tl::unexpected{GlobalParsingIssue::UnableToDeducePrinterTechnology};
    }
}

namespace {

const char* issue_type_name(ItemParsingIssueType type)
{
    switch (type) {
    case ItemParsingIssueType::InvalidFormat: return "the value could not be read";
    case ItemParsingIssueType::NotFound: return "the setting is not in the file";
    case ItemParsingIssueType::ExtraKey: return "this build has no such setting";
    }
    return "the setting could not be read";
}

void log_box_issues(const std::string& location, const BoxIssues& issues)
{
    for (const auto& [key, issue] : issues) {
        // A setting that is not in the file is one the file predates, which every project written
        // before that setting existed is, so it is not worth a line per key. A setting this build
        // does not have, or a value it cannot read, is: the file names something that no longer
        // does what it says, and that must be visible rather than silently dropped.
        if (issue.type == ItemParsingIssueType::NotFound)
            continue;

        SPDLOG_WARN(
            "The configuration of {} names \"{}\", which {}.",
            location,
            key,
            issue.message.empty() ? issue_type_name(issue.type) : issue.message
        );
    }
}

// The issues of a configuration that did load are reported, not thrown away: a project written by
// another version of the app keeps loading, and what it names that this build no longer has is
// said out loud once.
void log_issues(const IssuesPerLocation& issues)
{
    for (const auto& [location, location_issues] : issues) {
        std::visit(
            overloaded{
                [&](const BoxIssues& box_issues) {
                    log_box_issues(get_location_name(location), box_issues);
                },
                [&](const std::vector<BoxIssues>& boxes_issues) {
                    for (size_t i = 0; i < boxes_issues.size(); ++i)
                        log_box_issues(get_location_name(location) + " " + std::to_string(i),
                                       boxes_issues[i]);
                },
            },
            location_issues
        );
    }
}

} // namespace

tl::expected<PresetAndConfig, std::string> load_preset_and_config(
    const ordered_json& project_config_json
)
{
    using Format::ProjectFileConstants::CONFIGURATION;
    using Format::ProjectFileConstants::PRESET_METADATA;

    if (!project_config_json.contains(PRESET_METADATA)
        || !project_config_json.contains(CONFIGURATION))
    {
        return tl::make_unexpected(
            std::string{"The configuration is missing the \"preset\" or \"configuration\" data."}
        );
    }

    tl::expected<Domain::Preset::SelectedPresetMetadata, std::string> preset_metadata =
        load_preset_metadata(project_config_json[PRESET_METADATA]);
    if (!preset_metadata.has_value()) {
        return tl::make_unexpected(preset_metadata.error());
    }

    tl::expected<LoadResult, GlobalParsingIssue> config =
        load(project_config_json[CONFIGURATION], preset_metadata.value().hw_config);
    if (!config.has_value()) {
        return tl::make_unexpected(std::string{"The configuration data could not be parsed."});
    }

    // The config is usable whatever the issues are: the settings that are there are read and the
    // rest keeps its default. Saying the rest out loud is what tells a setting that stopped doing
    // something apart from one that was never set.
    log_issues(config->issues);

    return PresetAndConfig{
        .preset_metadata = std::move(preset_metadata.value()),
        .config_pack     = std::move(config.value().config)
    };
}

} // namespace Slic3r::Biz::Config
