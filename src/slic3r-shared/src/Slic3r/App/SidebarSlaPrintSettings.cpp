#include "Slic3r/App/SidebarSlaPrintSettings.hpp"

#include "Slic3r/App/IsSlaActive.hpp"
#include "Slic3r/App/SlaPrintSettingsDialog.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/SlaLayerHeight.hpp"

#include <iomanip>
#include <sstream>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

namespace {

std::optional<double> get_number(const Domain::ConfigBox& box, const std::string& key)
{
    if (const Domain::ConfigItem* item = box.items.find(key)) {
        if (item->holds_alternative<double>()) {
            return item->get<double>();
        }
    }
    return std::nullopt;
}

std::string selected_resin_name(const Biz::ProjectInteractor& project_interactor)
{
    const auto& material_presets = project_interactor.preset_interactor().material_presets();
    if (material_presets.size() == 0) {
        return {};
    }
    const Biz::Preset::PresetItemObservableList& resins = material_presets.at(0);
    const size_t selected_index                        = resins.selected_index();
    if (selected_index == Domain::INVALID_ID || selected_index >= resins.items().size()) {
        return {};
    }
    return resins.items().at(selected_index).ui_preset_name();
}

/// The layer height the print will actually be sliced with: the one of the resin when the resin
/// defines one, the one of the supports & raft preset otherwise. Nullopt when neither has one.
std::optional<double> effective_layer_height(
    const Domain::ConfigPackSLA& config_pack,
    const Domain::Preset::HwPrinterConfig& hw_config
)
{
    const auto full_config = std::make_shared<const Domain::FullConfigSLA>(config_pack, hw_config);
    Domain::ConfigView config_view{full_config, {}};
    config_view.finalize();

    const double layer_height = Domain::sla_effective_layer_height(config_view);
    return layer_height > 0. ? std::optional<double>{layer_height} : std::nullopt;
}

} // namespace

SidebarSlaPrintSettings::SidebarSlaPrintSettings(
    Biz::ProjectInteractor& project_interactor,
    SlaPrintSettingsDialog& dialog
) :
    m_project_interactor(project_interactor)
{
    set_object_name("SidebarSlaPrintSettings");
    set_orientation(Orientation::Vertical);
    set_gap(2_fpx);
    set_flex_shrink(0);

    m_button = emplace_back<LayoutButton>(Biz::_u8L("Print settings"), Render::Icon::Cog);
    m_button->set_checkable(true);
    m_button->set_flex_grow(1.f);
    m_button->set_height(1.5_rem);
    m_button->callbacks().action = [this, &dialog]() { dialog.toggle(); };

    dialog.callbacks().opened = [this]() { m_button->set_checked(true); };
    dialog.callbacks().closed = [this]() { m_button->set_checked(false); };

    m_summary = emplace_back<Text>(std::string{});
    m_summary->set_wrap_mode(Text::WrapMode::WrapElide);
    m_summary->set_text_color(m_theme->color_imgui(Platform::Color::Text));

    update_visibility();
    refresh();
}

void SidebarSlaPrintSettings::update_visibility()
{
    set_visible(is_sla_active(m_project_interactor));
}

void SidebarSlaPrintSettings::refresh()
{
    if (!is_sla_active(m_project_interactor)) {
        m_summary->set_text(std::string{});
        return;
    }
    m_summary->set_text(summary_text());
}

std::string SidebarSlaPrintSettings::summary_text() const
{
    if (!is_sla_active(m_project_interactor)) {
        return {};
    }

    const Domain::ConfigContainer& config_container
        = m_project_interactor.selected_config_container();
    const Domain::ConfigPack config_pack = config_container.build_print_config();
    const auto* sla_config = std::get_if<Domain::ConfigPackSLA>(&config_pack);
    if (sla_config == nullptr) {
        return {};
    }

    return SidebarSlaPrintSettingsFormat::format_summary(
        selected_resin_name(m_project_interactor),
        effective_layer_height(*sla_config, config_container.selected_preset().hw_config),
        get_number(sla_config->sla_material_settings, "exposure_time"),
        get_number(sla_config->sla_material_settings, "initial_exposure_time"),
        Biz::_u8L("mm"),
        Biz::_u8L("s")
    );
}

namespace SidebarSlaPrintSettingsFormat {

std::string format_number(double value)
{
    std::ostringstream stream;
    stream << std::setprecision(3) << value;
    return stream.str();
}

std::string format_summary(
    const std::string& resin_name,
    std::optional<double> layer_height,
    std::optional<double> exposure,
    std::optional<double> bottom_exposure,
    const std::string& length_unit,
    const std::string& time_unit
)
{
    const std::string missing = "—";
    const auto format_value   = [&missing](std::optional<double> number, const std::string& unit)
                          -> std::string
    { return number ? format_number(*number) + " " + unit : missing; };

    const std::string exposures = format_value(exposure, time_unit) + " / "
                                + format_value(bottom_exposure, time_unit);

    return (resin_name.empty() ? missing : resin_name) + " · "
         + format_value(layer_height, length_unit) + " · " + exposures;
}

} // namespace SidebarSlaPrintSettingsFormat

} // namespace Slic3r::App
