#include "Slic3r/Biz/ResinProfile/ResinProfileImportInteractor.hpp"

#include "Slic3r/Biz/Preset/NameValidator.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ResinProfile/ChituboxCfgReader.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/Percentage.hpp"
#include "Slic3r/Domain/Preset/PresetTree.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "Slic3r/Domain/TemplateUtils.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/lexical_cast.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <map>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Slic3r::Biz::ResinProfile {

namespace {

namespace Preset = Slic3r::Biz::Preset;

/// @brief The system resin preset an imported preset inherits from.
/// Everything here is a copy: a reference into the preset collections would not survive the save that
/// follows, because saving a user preset reloads the vendor bundle and that replaces every evaluated
/// preset in it, so a second import would read freed memory.
struct BaseMaterial
{
    std::string id;
    std::string name;
    /// Whether the base preset says the machine tilts, empty when it says nothing about it.
    std::optional<std::vector<bool>> use_tilt;
};

std::string trim_copy(std::string_view text)
{
    return boost::trim_copy(std::string(text));
}

std::string lower(std::string text)
{
    return boost::to_lower_copy(std::move(text));
}

bool contains_ignore_case(std::string_view text, std::string_view needle)
{
    return lower(std::string(text)).find(lower(std::string(needle))) != std::string::npos;
}

std::vector<std::string> split(std::string_view text, char separator)
{
    std::vector<std::string> parts;
    std::istringstream stream{std::string(text)};
    std::string part;
    while (std::getline(stream, part, separator))
        parts.push_back(trim_copy(part));
    return parts;
}

const Domain::ConfigItem* find_item(const Domain::ConfigBox& box, const std::string& key)
{
    return box.items.find(key);
}

std::optional<std::string> string_value(const Domain::ConfigBox& box, const std::string& key)
{
    const Domain::ConfigItem* item = find_item(box, key);
    if (!item || !item->holds_alternative<std::string>())
        return std::nullopt;
    return item->get<std::string>();
}

std::optional<std::vector<bool>> bool_vector_value(const Domain::ConfigBox& box, const std::string& key)
{
    const Domain::ConfigItem* item = find_item(box, key);
    if (!item || !item->holds_alternative<std::vector<bool>>())
        return std::nullopt;
    return item->get<std::vector<bool>>();
}

std::string today()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[16]{};
    std::strftime(text, sizeof(text), "%Y-%m-%d", &local);
    return text;
}

std::string source_app_name(const std::string& format_id)
{
    if (format_id.starts_with("chitubox"))
        return "Chitubox";
    if (format_id.starts_with("lychee"))
        return "Lychee";
    // A reader this fork does not know the app of yet still leaves a trace of the format.
    return format_id;
}

/// @brief The resin name the profile carries, which is also what the mapper suggests as the preset
/// name. Read here rather than from the mapping report because the base material has to be picked
/// before the mapping, and the mapping needs the printer class that base decides.
std::string resin_name_hint(const ForeignResinProfile& profile)
{
    const auto current_profile = profile.raw_values.find("currProfile");
    if (current_profile != profile.raw_values.end()) {
        const std::string name = trim_copy(current_profile->second);
        if (!name.empty())
            return name;
    }
    return profile.printer_hint.value_or(std::string{});
}

TargetPrinterClass printer_class_of(
    const std::string& printer_model,
    const std::optional<std::vector<bool>>& base_use_tilt
)
{
    // A resin preset says the machine lifts the build plate by turning use_tilt off. Leaving it on
    // says nothing, because on is also the config default, so the printer model decides: only the
    // SL1 and SL1S tilt. (printer_class_from_config() cannot tell those two apart, a config view
    // always carries use_tilt, default included.)
    if (base_use_tilt && std::ranges::none_of(*base_use_tilt, [](bool uses_tilt) { return uses_tilt; }))
    {
        return TargetPrinterClass::GenericMsla;
    }

    if (printer_model.empty())
        return TargetPrinterClass::Tilt; // this fork is built around the SL1
    return contains_ignore_case(printer_model, "SL1") ? TargetPrinterClass::Tilt : TargetPrinterClass::GenericMsla;
}

bool base_matches(const std::string& resin_name, const Domain::Preset::EvaluatedMaterialPreset::Preset& preset)
{
    if (resin_name.empty())
        return false;
    if (contains_ignore_case(preset.name, resin_name))
        return true;
    const std::optional<std::string> vendor = string_value(preset.config_box(), "material_vendor");
    return vendor && contains_ignore_case(*vendor, resin_name);
}

std::optional<BaseMaterial> pick_base_material(
    const Preset::PresetInteractor& presets,
    Domain::SelectionId project_id,
    const Domain::Preset::SelectedPreset& selected,
    size_t slot,
    const std::string& resin_name
)
{
    const Domain::Preset::EvaluatedMaterialPreset::Preset* first_system = nullptr;
    const Domain::Preset::EvaluatedMaterialPreset::Preset* selected_system = nullptr;
    const Domain::Preset::EvaluatedMaterialPreset::Preset* default_system = nullptr;
    const Domain::Preset::EvaluatedMaterialPreset::Preset* named = nullptr;

    const std::string default_material =
        string_value(selected.print.config_box(), "default_material").value_or(std::string{});
    const std::string& selected_material_id = selected.materials[slot].id;

    // The presets of this printer: the bundle only offers the materials whose conditions match it,
    // so every one of them is compatible.
    for (const auto& entry : presets.get_material_presets(
             project_id, selected.hw_config.id, selected.printer.id, selected.print.id, slot))
    {
        const Domain::Preset::EvaluatedMaterialPreset::Preset& preset = entry.first.get();
        // A user or runtime preset would give the new preset no system preset to inherit from, and
        // the unnamed shared profiles (*common*, *sl1s_fast*, ...) are not a resin of this printer
        // at all, only the values a resin starts from.
        if (preset.origin != Domain::Preset::PresetOrigin::System
            || !Domain::Preset::is_public_name(preset.name))
        {
            continue;
        }
        if (!first_system)
            first_system = &preset;
        if (preset.id == selected_material_id)
            selected_system = &preset;
        if (!named && base_matches(resin_name, preset))
            named = &preset;
        if (!default_system && !default_material.empty() && preset.name == default_material)
            default_system = &preset;
    }

    // The resin of the same name wins; then the printer's own default resin, which is either the
    // one its print preset names or the resin the printer has selected, and finally the first
    // system resin it offers (the list is ordered by name).
    for (const Domain::Preset::EvaluatedMaterialPreset::Preset* preset :
         {named, default_system, selected_system, first_system})
    {
        if (preset)
            return BaseMaterial{
                preset->id, preset->name, bool_vector_value(preset->config_box(), "use_tilt")};
    }
    return std::nullopt;
}

/// @brief Write a value that came from a foreign file into a config item.
/// The mapper produces the text the config itself writes ("2.5", "3,3"), so this is the one place
/// that reads it back. The text is untrusted: what does not fit the option is reported, not asserted.
bool assign_from_text(Domain::ConfigItem& item, const std::string& text, std::string& error)
{
    const std::string value = trim_copy(text);
    return item.visit(Domain::overloaded{
        [&](Domain::EnumWrapper& wrapper) -> bool {
            for (const Domain::EnumValueDef& def : wrapper.def())
                if (def.str_serialized == value) {
                    wrapper.set_string(value); // set_string panics on an unknown name, hence the lookup
                    return true;
                }
            error = "\"" + value + "\" is not one of the allowed options";
            return false;
        },
        [&](Domain::EnumVectorWrapper& wrapper) -> bool {
            const std::vector<std::string> parts = split(value, ',');
            for (const std::string& part : parts) {
                const bool allowed = std::ranges::any_of(
                    wrapper.def(),
                    [&](const Domain::EnumValueDef& def) { return def.str_serialized == part; });
                if (!allowed) {
                    error = "\"" + part + "\" is not one of the allowed options";
                    return false;
                }
            }
            wrapper.set_strings(parts);
            return true;
        },
        [&](auto& target) -> bool {
            using ValueType = std::remove_cvref_t<decltype(target)>;
            if constexpr (std::is_same_v<ValueType, std::string>) {
                target = value;
                return true;
            } else if constexpr (std::is_same_v<ValueType, bool>) {
                const std::string text_lowered = lower(value);
                if (text_lowered == "1" || text_lowered == "true") {
                    target = true;
                    return true;
                }
                if (text_lowered == "0" || text_lowered == "false") {
                    target = false;
                    return true;
                }
                error = "\"" + value + "\" is not a boolean";
                return false;
            } else if constexpr (std::is_same_v<ValueType, int> || std::is_same_v<ValueType, double>) {
                try {
                    target = boost::lexical_cast<ValueType>(value);
                } catch (const boost::bad_lexical_cast&) {
                    error = "\"" + value + "\" is not a number";
                    return false;
                }
                return true;
            } else if constexpr (std::is_same_v<ValueType, Domain::Vec2d>) {
                std::vector<std::string> parts = split(value, ',');
                if (parts.size() != 2)
                    parts = split(value, 'x');
                if (parts.size() != 2) {
                    error = "\"" + value + "\" is not a pair of numbers";
                    return false;
                }
                try {
                    target = Domain::Vec2d{
                        boost::lexical_cast<double>(parts[0]), boost::lexical_cast<double>(parts[1])};
                } catch (const boost::bad_lexical_cast&) {
                    error = "\"" + value + "\" is not a pair of numbers";
                    return false;
                }
                return true;
            } else if constexpr (std::is_same_v<ValueType, std::vector<bool>>) {
                std::vector<bool> values;
                for (const std::string& part : split(value, ',')) {
                    const std::string part_lowered = lower(part);
                    if (part_lowered == "1" || part_lowered == "true")
                        values.push_back(true);
                    else if (part_lowered == "0" || part_lowered == "false")
                        values.push_back(false);
                    else {
                        error = "\"" + part + "\" is not a boolean";
                        return false;
                    }
                }
                target = values;
                return true;
            } else if constexpr (std::is_same_v<ValueType, std::vector<double>>
                                 || std::is_same_v<ValueType, std::vector<int>>) {
                // A setting that has one value per area below/above the fill threshold, written
                // "1,1" the way the config writes it in an .ini file.
                std::vector<typename ValueType::value_type> values;
                for (const std::string& part : split(value, ',')) {
                    try {
                        values.push_back(boost::lexical_cast<typename ValueType::value_type>(part));
                    } catch (const boost::bad_lexical_cast&) {
                        error = "\"" + part + "\" is not a number";
                        return false;
                    }
                }
                target = values;
                return true;
            } else if constexpr (std::is_same_v<ValueType, std::vector<std::string>>) {
                target = split(value, ',');
                return true;
            } else if constexpr (std::is_same_v<ValueType, Domain::Percentage>) {
                std::string number = value;
                if (!number.empty() && number.back() == '%')
                    number.pop_back();
                try {
                    target = Domain::Percentage{boost::lexical_cast<double>(trim_copy(number))};
                } catch (const boost::bad_lexical_cast&) {
                    error = "\"" + value + "\" is not a percentage";
                    return false;
                }
                return true;
            } else if constexpr (std::is_same_v<ValueType, Domain::FloatOrPercentage>) {
                const bool is_percentage = !value.empty() && value.back() == '%';
                const std::string number = trim_copy(is_percentage ? value.substr(0, value.size() - 1) : value);
                try {
                    const double number_value = boost::lexical_cast<double>(number);
                    target = is_percentage ? Domain::FloatOrPercentage{Domain::Percentage{number_value}}
                                            : Domain::FloatOrPercentage{number_value};
                } catch (const boost::bad_lexical_cast&) {
                    error = "\"" + value + "\" is not a number";
                    return false;
                }
                return true;
            } else {
                error = "the importer cannot write a value of this type";
                return false;
            }
        }});
}

bool set_material_value(
    Preset::PresetInteractor& presets,
    size_t slot,
    const std::string& key,
    const std::string& text,
    std::string& error
)
{
    // Checked before writing: set_preset_value() looks the option up and asserts on a missing one,
    // and a mapping must not be able to close the app.
    if (!find_item(presets.selected_printer_preset().materials[slot].config_box(), key)) {
        error = "\"" + key + "\" is not a resin setting of this build";
        return false;
    }

    std::string item_error;
    bool assigned = false;
    presets.set_preset_value(
        Domain::SLAConfigLocation::Material,
        static_cast<int>(slot),
        key,
        [&](Domain::ConfigItem& item) { assigned = assign_from_text(item, text, item_error); });
    if (!assigned)
        error = "\"" + text + "\" cannot be written into " + key + ": " + item_error;
    return assigned;
}

/// @brief A preset name out of untrusted text: the characters a preset name or a file name may not
/// carry are replaced, and the result is short enough for a path on every platform.
std::string sanitize_preset_name(std::string name)
{
    constexpr std::string_view illegal = "<>[]:/\\|?*\"@";
    for (char& character : name) {
        if (illegal.find(character) != std::string_view::npos || static_cast<unsigned char>(character) < 0x20)
            character = '_';
    }
    name = trim_copy(name);
    if (name.size() > 100)
        name.resize(100);
    return trim_copy(name);
}

std::string preset_name(const MappingResult& mapping, const boost::filesystem::path& path)
{
    std::string name = sanitize_preset_name(mapping.suggested_name);
    if (name.empty())
        name = sanitize_preset_name(path.stem().string());
    return name.empty() ? std::string("Imported resin") : name;
}

/// @brief A name no other preset of that kind carries: the wanted one, or the wanted one with the
/// first free " (2)", " (3)", ... appended. Saving under a name that is taken would replace that
/// preset instead of adding one.
/// The NameValidator keeps its own copy of the names, so the answer is already a value by the time
/// this returns and the save that follows cannot invalidate it.
std::string unique_preset_name(
    const Preset::PresetInteractor& presets,
    Domain::Preset::PresetKind kind,
    const std::string& wanted
)
{
    const Preset::NameValidator taken(presets, kind, wanted, false);
    std::string name = wanted;
    for (int suffix = 2; !taken.get_conflict_name(name).empty() && suffix < 1000; ++suffix)
        name = fmt::format("{} ({})", wanted, suffix);
    return name;
}

} // namespace

ResinProfileReaderRegistry ResinProfileImportInteractor::default_registry()
{
    ResinProfileReaderRegistry registry;
    registry.register_reader(std::make_unique<ChituboxCfgReader>());
    return registry;
}

ResinProfileImportInteractor::ResinProfileImportInteractor(
    Biz::ProjectInteractor& project_interactor,
    DateProvider date_provider,
    ResinProfileReaderRegistry registry
)
    : m_project_interactor(project_interactor)
    , m_registry(std::move(registry))
    , m_date_provider(date_provider ? std::move(date_provider) : DateProvider{[] { return today(); } })
{
}

ResinImportResult ResinProfileImportInteractor::import_file(
    const boost::filesystem::path& path,
    const ResinImportTarget& target,
    bool dry_run
)
{
    ResinImportResult result;
    result.file = path.string();

    Biz::ProjectInteractor& project = m_project_interactor;
    Preset::PresetInteractor& presets = project.preset_interactor();

    const Domain::SelectionId project_id =
        target.project_id == Domain::INVALID_ID ? project.selected_project_id() : target.project_id;
    const Domain::SelectionId container_id = target.config_container_id == Domain::INVALID_ID ?
                                                project.selected_config_container_id() :
                                                target.config_container_id;
    if (project_id != project.selected_project_id()
        || container_id != project.selected_config_container_id())
    {
        result.error = "the target printer is not the selected one; select it before importing";
        return result;
    }

    const Domain::Preset::SelectedPreset& selected = presets.selected_printer_preset();
    if (selected.technology() != Domain::PrinterTechnology::SLA) {
        result.error = "the selected printer is not an SLA printer";
        return result;
    }
    if (target.material_slot >= selected.materials.size()) {
        result.error = "the selected printer has no resin slot " + std::to_string(target.material_slot);
        return result;
    }

    // Reading is where untrusted input is bounded: the registry caps the file size, and a reader only
    // parses, it never evaluates what it reads (no G-code is run, ever).
    const tl::expected<ForeignResinProfile, std::string> profile = m_registry.read_file(path);
    if (!profile) {
        result.error = profile.error();
        return result;
    }

    // The base comes before the mapping: it is the system resin of this printer, and its own
    // settings say how the printer separates layers, which is what picks the mapping table. Both
    // the base and the printer model are read out as copies, because everything below this point
    // mutates the preset collections they live in.
    const std::optional<BaseMaterial> base = pick_base_material(
        presets, project_id, selected, target.material_slot, resin_name_hint(*profile));
    if (!base) {
        result.error = "no system resin preset is available for this printer";
        return result;
    }
    const std::string printer_model =
        string_value(selected.printer.config_box(), "printer_model").value_or(std::string{});

    result.mapping = map_resin_profile(*profile, printer_class_of(printer_model, base->use_tilt));
    result.base_preset = base->name;
    result.preset_name =
        unique_preset_name(presets, Domain::Preset::PresetKind::SlaMaterial, preset_name(result.mapping, path));
    if (dry_run) {
        result.ok = true;
        return result;
    }

    // Everything the rest of the import needs is a copy by now (the base id, the values to write,
    // the name to save under), so the mutations below cannot leave this function reading a preset
    // that the save has already replaced. In particular the save reloads the vendor bundle, which
    // is what a second import of the same file runs into.
    // Inherit from the base: select it, write the mapped values on top of it, then save the
    // container's material preset as a new user preset, which is what the material settings dialog
    // does. A failure past this point leaves the container showing the base preset.
    presets.select_material_preset(target.material_slot, base->id, false);

    std::map<std::string, std::string> values = result.mapping.material_values;
    values["material_source_note"] = fmt::format(
        "{} {}, imported {}", source_app_name(profile->source_format), path.filename().string(), m_date_provider());
    for (const auto& [key, text] : values) {
        std::string error;
        if (!set_material_value(presets, target.material_slot, key, text, error)) {
            result.error = error;
            result.preset_name.clear();
            return result;
        }
    }

    presets.save_selected_preset_as(
        Domain::Preset::PresetKind::SlaMaterial, target.material_slot, result.preset_name);
    result.ok = true;
    return result;
}

std::vector<ResinImportResult> ResinProfileImportInteractor::import_folder(
    const boost::filesystem::path& folder,
    const ResinImportTarget& target,
    bool dry_run
)
{
    std::vector<ResinImportResult> results;

    boost::system::error_code ec;
    if (!boost::filesystem::is_directory(folder, ec)) {
        ResinImportResult failed;
        failed.file = folder.string();
        failed.error = "not a folder";
        results.push_back(std::move(failed));
        return results;
    }

    std::vector<boost::filesystem::path> files;
    for (boost::filesystem::directory_iterator entry(folder, ec), end; !ec && entry != end;
         entry.increment(ec))
    {
        if (entry->is_regular_file(ec))
            files.push_back(entry->path());
    }
    std::ranges::sort(files); // the same folder always imports in the same order

    if (files.size() > MAX_BATCH_FILES) {
        ResinImportResult skipped;
        skipped.file = folder.string();
        skipped.error = fmt::format(
            "the folder holds {} files, only the first {} are imported", files.size(), MAX_BATCH_FILES);
        results.push_back(std::move(skipped));
        files.resize(MAX_BATCH_FILES);
    }

    // One result per file, and a file that fails (unreadable, unrecognised, unsavable) only fills
    // in its own result: the batch always goes on.
    for (const boost::filesystem::path& file : files)
        results.push_back(import_file(file, target, dry_run));

    return results;
}

} // namespace Slic3r::Biz::ResinProfile
