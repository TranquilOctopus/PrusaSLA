#pragma once

#include "Slic3r/App/PopNotification/PopNotificationData.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileImportInteractor.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::App {

/**
 * @brief What an imported SLA archive carries, split by what a resin review can take and what
 * nothing takes.
 *
 * An .sl1/.sl1s archive is a sliced print: it holds the profile the layers were rendered for
 * (prusaslicer.ini) next to the settings of the printer it was written for (config.ini). The
 * material values of the first go through the existing resin import path and can be saved as a
 * user resin preset; the supports and the raft of the same file are print-preset settings that
 * no resin mapping takes, so they are named here rather than dropped in silence.
 */
struct SlaArchiveSettings
{
    /// @brief Labels of the material values a review of the archive writes into a resin preset,
    /// one per value, in the order of the mapping report. Empty when the archive names nothing a
    /// resin preset has a key for, and then there is nothing to offer at all.
    std::vector<std::string> applied;
    /// @brief Names of the print-level values the archive carries that no mapping writes, capped
    /// at max_not_applied() with the rest counted rather than listed.
    std::vector<std::string> not_applied;
    /// @brief How many of the print-level values were left out of @ref not_applied by the cap.
    std::size_t not_applied_left_out{0};

    /// @brief Whether there is anything to offer. An archive without a single material value is
    /// imported and then left alone: no notification, no button, no review.
    bool has_material() const
    {
        return !applied.empty();
    }
};

/// @brief How many print-level values are named before the rest are only counted.
inline constexpr std::size_t max_not_applied = 6;

/**
 * @brief Split the settings of an imported archive into what a review takes and what it does not.
 *
 * Pure: it reads the mapping report of a resin import of the archive, which is exactly the work
 * the review dialog does, so what this names and what the review writes cannot drift apart. The
 * result is taken as it comes from the interactor, so @p result.ok is not checked here.
 *
 * @param result A resin import of the archive, dry or not: its mapping report is what is read.
 */
SlaArchiveSettings analyze_archive_settings(const Biz::ResinProfile::ResinImportResult& result);

/**
 * @brief The notification that offers the settings of an imported archive, or nothing when the
 * archive carries none.
 *
 * The button opens the resin import review of @p archive_path, where the user reads what each
 * value becomes and saves them as a user resin preset. Nothing is written here, and the selected
 * printer is never touched, so this only ever asks.
 *
 * Pure: it builds the data and hands it back, and the button is a plain callback, so a test reads
 * the layout back and presses the button without a window.
 *
 * @param settings What analyze_archive_settings() found.
 * @param project_id The project the archive was imported into.
 * @param on_use_settings Called when the button is pressed. The caller opens the review of the
 * archive it analysed, and returns whether the notification should close.
 */
std::optional<PopNotification::PopNotificationData> build_archive_settings_notification(
    const SlaArchiveSettings& settings,
    Domain::SelectionId project_id,
    std::function<bool()> on_use_settings
);

} // namespace Slic3r::App
