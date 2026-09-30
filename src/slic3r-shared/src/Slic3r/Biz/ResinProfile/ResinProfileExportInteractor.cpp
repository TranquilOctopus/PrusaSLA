#include "Slic3r/Biz/ResinProfile/ResinProfileExportInteractor.hpp"

#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/ResinProfile/ChituboxCfgExport.hpp"
#include "Slic3r/Domain/Preset/SelectedPreset.hpp"

#include <fmt/format.h>

#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace Slic3r::Biz::ResinProfile {

namespace {

// The printer model decides which of the two mapping tables a preset is written with, the same way
// it decides which one the import maps with.
std::string printer_model_of(const Domain::ConfigBox& printer)
{
    const Domain::ConfigItem* item = printer.items.find("printer_model");
    if (item == nullptr || !item->holds_alternative<std::string>())
        return {};
    return item->get<std::string>();
}

} // anonymous namespace

std::string suggested_cfg_file_name(const std::string& preset_name)
{
    // A preset name is a name, and a file name is a file name: the characters Windows will not have
    // in a file name become underscores, the same set the importer turns into underscores when it
    // names a preset after a file.
    constexpr std::string_view illegal = "<>[]:/\\|?*\"@";
    std::string              name     = preset_name;
    for (char& character : name) {
        if (illegal.find(character) != std::string_view::npos || static_cast<unsigned char>(character) < 0x20)
            character = '_';
    }
    name = boost::trim_copy(name);
    // A name of a few hundred characters is a name the dialog shows, not a file name anyone wants,
    // and the file system is the limit that matters here.
    if (name.size() > 100)
        name.resize(100);
    name = boost::trim_copy(name);
    if (name.empty())
        return std::string("resin.") + ResinProfileExportInteractor::FILE_EXTENSION;
    return name + "." + ResinProfileExportInteractor::FILE_EXTENSION;
}

ResinProfileExportResult ResinProfileExportInteractor::export_preset(
    Domain::SelectionId project_id,
    const std::string&   preset_name,
    size_t               material_slot
) const
{
    ResinProfileExportResult result;

    // Only the selected printer has a resin preset to export, and a printer that is not an SLA one
    // has none at all. Both are said here rather than left to a lookup that finds nothing.
    const Domain::Preset::SelectedPreset& selected = m_preset_interactor.selected_printer_preset();
    if (selected.technology() != Domain::PrinterTechnology::SLA) {
        result.error = "The selected printer is not an SLA printer, it has no resin preset.";
        return result;
    }
    if (material_slot >= selected.materials.size()) {
        result.error = "The selected printer has no resin in this slot.";
        return result;
    }

    // What to export: the preset a caller named (by name or by id, which is what the command line
    // passes and what a list row can hand over), or else the resin that is in the slot right now.
    const Domain::Preset::EvaluatedMaterialPreset::Preset* found = nullptr;
    if (preset_name.empty()) {
        found = &selected.materials[material_slot];
    } else {
        for (const auto& entry : m_preset_interactor.get_material_presets(
                 project_id, selected.hw_config.id, selected.printer.id, selected.print.id, material_slot))
        {
            const Domain::Preset::EvaluatedMaterialPreset::Preset& preset = entry.first.get();
            if (preset.name == preset_name || preset.id == preset_name) {
                found = &preset;
                break;
            }
        }
        if (found == nullptr) {
            result.error = fmt::format(
                "The printer \"{}\" has no resin preset named \"{}\".",
                selected.printer.name,
                preset_name
            );
            return result;
        }
    }

    result.preset_name         = found->name;
    result.suggested_file_name = suggested_cfg_file_name(found->name);

    const Domain::ConfigItems& material = found->config_box().items;
    result.exported                    = export_chitubox_cfg_report(
        material,
        export_printer_class(material, printer_model_of(selected.printer.config_box())),
        found->name
    );
    result.ok = true;
    return result;
}

ResinProfileExportResult ResinProfileExportInteractor::export_preset_to_file(
    const boost::filesystem::path& path,
    Domain::SelectionId            project_id,
    const std::string&              preset_name,
    size_t                          material_slot,
    std::string*                    error
) const
{
    ResinProfileExportResult result = export_preset(project_id, preset_name, material_slot);
    if (!result.ok) {
        if (error != nullptr)
            *error = result.error;
        return result;
    }

    // Truncate: a shorter profile must not leave the tail of a longer one behind it.
    boost::nowide::ofstream out{path, std::ios::out | std::ios::trunc};
    if (!out.is_open()) {
        result.ok    = false;
        result.error = fmt::format("Cannot open {} for writing.", path.string());
        if (error != nullptr)
            *error = result.error;
        return result;
    }
    out << result.exported.text;
    out.close();
    if (out.fail()) {
        result.ok    = false;
        result.error = fmt::format("Cannot write {}.", path.string());
        if (error != nullptr)
            *error = result.error;
    }
    return result;
}

} // namespace Slic3r::Biz::ResinProfile
