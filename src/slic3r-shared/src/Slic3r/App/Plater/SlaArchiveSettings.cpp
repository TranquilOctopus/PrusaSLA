#include "Slic3r/App/Plater/SlaArchiveSettings.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>

namespace Slic3r::App {

using Biz::ResinProfile::MappedField;
using Biz::ResinProfile::ResinImportResult;

namespace {

/// @brief The prefixes the print-level settings of an archive carry: the supports, the raft and
/// the hollowing. A key with one of them is a value of the print preset, and a resin preset has
/// no key for it, so the notification names it instead of letting it go as applied. Prefixes are
/// used rather than a list of keys so that a support setting added later is named too.
constexpr std::string_view print_level_prefixes[] = {"support_", "raft_", "pad_", "hollow_"};

bool is_print_level(std::string_view key)
{
    for (const std::string_view prefix : print_level_prefixes) {
        if (key.starts_with(prefix))
            return true;
    }
    return false;
}

/// @brief The label of a material value, by the resin preset key the review writes. A key with no
/// label here is named by the key itself rather than left out, so the list never understates what
/// the review writes.
std::string applied_label(std::string_view target_key)
{
    if (target_key == "exposure_time")
        return Biz::_u8L("Exposure");
    if (target_key == "initial_exposure_time")
        return Biz::_u8L("Bottom exposure");
    if (target_key == "resin_layer_height")
        return Biz::_u8L("Layer height");
    if (target_key == "resin_faded_layers")
        return Biz::_u8L("Transition layers");
    if (target_key == "bottom_layer_count")
        return Biz::_u8L("Bottom layers");
    if (target_key == "material_density")
        return Biz::_u8L("Density");
    if (target_key == "bottle_cost")
        return Biz::_u8L("Bottle cost");
    return std::string(target_key);
}

} // namespace

SlaArchiveSettings analyze_archive_settings(const ResinImportResult& result)
{
    SlaArchiveSettings settings;

    for (const MappedField& row : result.mapping.report) {
        // A row with a target key is written into the resin preset, so it is one of the values a
        // review applies. One value can have more than one spelling in the archive; it is listed
        // once, on its first row.
        if (!row.target_key.empty()) {
            const std::string label = applied_label(row.target_key);
            if (std::find(settings.applied.begin(), settings.applied.end(), label)
                == settings.applied.end())
            {
                settings.applied.push_back(label);
            }
            continue;
        }
        // A print-level value that nothing takes. The archive writes these keys exactly as the
        // config spells them, so the key is the name to show.
        if (!is_print_level(row.source_key))
            continue;
        if (settings.not_applied.size() < max_not_applied)
            settings.not_applied.push_back(row.source_key);
        else
            ++settings.not_applied_left_out;
    }

    return settings;
}

std::optional<PopNotification::PopNotificationData> build_archive_settings_notification(
    const SlaArchiveSettings& settings,
    Domain::SelectionId project_id,
    std::function<bool()> on_use_settings
)
{
    if (!settings.has_material())
        return std::nullopt;

    std::string text = Biz::_u8L(
        "This archive carries the material settings of the print it was written for. "
        "Review them and save them as your own resin preset."
    );

    if (!settings.not_applied.empty()) {
        std::string names;
        for (const std::string& name : settings.not_applied) {
            if (!names.empty())
                names += ", ";
            names += name;
        }
        if (settings.not_applied_left_out > 0) {
            names += ", ";
            names +=
                fmt::format(fmt::runtime(Biz::_u8L("and {} more")), settings.not_applied_left_out);
        }
        text += "\n";
        text += fmt::
            format(fmt::runtime(Biz::_u8L("Not applied, these are print settings: {}")), names);
    }

    using namespace PopNotification;
    return PopNotificationData{
        .type    = PopNotificationType::SlaArchiveSettings,
        .level   = PopNotificationLevel::Regular,
        .timeout = std::chrono::seconds{0},
        .layout =
            PopNotificationLayoutHeaderTextButtons{
                .header  = Biz::_u8L("Archive settings"),
                .text    = std::move(text),
                .buttons = {PopNotificationButtonData{
                    // TRN Text of a notification button - review the material settings an
                    // imported SLA archive carries and save them as a resin preset.
                    .text     = Biz::_u8L("Use the settings from this archive"),
                    .callback = std::move(on_use_settings)
                }},
                .icon = Render::Icon::MaterialIconMarker
            },
        .project_id = project_id,
    };
}

} // namespace Slic3r::App
