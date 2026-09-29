#pragma once

#include "Slic3r/App/Yoga/Item.hpp"

#include <optional>
#include <string>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class SlaPrintSettingsDialog;

namespace Yoga {
class LayoutButton;
class Text;
} // namespace Yoga

/**
 * @brief SLA only sidebar control replacing the print preset and the resin slot rows.
 *
 * A single "Print settings" button opening SlaPrintSettingsDialog, with a one line summary of
 * the selected presets (resin name, layer height, exposure / bottom exposure) below it.
 */
class SidebarSlaPrintSettings : public Yoga::Item
{
public:
    SidebarSlaPrintSettings(
        Biz::ProjectInteractor& project_interactor,
        SlaPrintSettingsDialog& dialog
    );

    /// The button is shown for SLA printers only.
    void update_visibility();

    /// Recomputes the summary text from the selected resin and supports & raft presets.
    void refresh();

    /// Summary of the selected presets, e.g. "Generic Resin · 0.05 mm · 2.5 s / 30 s".
    std::string summary_text() const;

private:
    Biz::ProjectInteractor& m_project_interactor;
    Yoga::LayoutButton* m_button{nullptr};
    Yoga::Text* m_summary{nullptr};
};

namespace SidebarSlaPrintSettingsFormat {

/// Formats a value with 3 significant digits and no trailing zeros: 0.05 -> "0.05", 2 -> "2".
std::string format_number(double value);

/// Joins the summary parts with a middle dot, a missing part is shown as an em dash.
/// Units are passed in already localized.
std::string format_summary(
    const std::string& resin_name,
    std::optional<double> layer_height,
    std::optional<double> exposure,
    std::optional<double> bottom_exposure,
    const std::string& length_unit,
    const std::string& time_unit
);

} // namespace SidebarSlaPrintSettingsFormat

} // namespace Slic3r::App
