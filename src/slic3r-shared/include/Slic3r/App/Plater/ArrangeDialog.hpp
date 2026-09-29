#pragma once

#include "Slic3r/App/Plater/GizmoWindow.hpp"
#include "Slic3r/Biz/Arrange/Settings.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

namespace Slic3r::App::Yoga {
class SliderWithInput;
class ToggleButton;
class Slider;
class SegmentedControl;
class Separator;
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Plater {

class PivotPicker;

enum class ArrangeTaskStatus
{
    Idle,
    Running
};

class ArrangeDialog : public GizmoWindow
{
public:
    enum ArrangeMode {
        PrinterGroup,
        SelectedBeds
    };

    using OnArrange      = std::function<void(ArrangeMode)>;
    using OnCancel       = std::function<void()>;

    ArrangeDialog(
        OnArrange on_arrange,
        OnCancel on_cancel,
        const Biz::Arrange::Settings& settings,
        const Biz::ProjectInteractor& project_interactor
    );

    // Re-reads the printer technology and updates the 'bed' / 'build plate' wording
    void reload_labels();

    void update_segments_visibility();

    void set_bed_segments(const std::optional<Domain::BedSegments>& bed_segments);

    void set_auxiliary_travel_anchor(const std::optional<Domain::Vec2d>& auxiliary_travel_anchor);

    void update_status(const ArrangeTaskStatus status);

    Biz::Arrange::Settings get_settings() const;

private:
    bool is_sla() const;
    Yoga::ItemPtr build_help();

private:
    OnArrange m_on_arrange;
    OnCancel m_on_cancel;
    const Biz::ProjectInteractor* m_project_interactor{nullptr};
    ArrangeTaskStatus m_status{ArrangeTaskStatus::Idle};
    Yoga::SegmentedControl* m_mode{nullptr};
    Yoga::SliderWithInput* m_offset_slider{nullptr};
    Yoga::SliderWithInput* m_bed_offset_slider{nullptr};
    Yoga::Text* m_bed_spacing_label{nullptr};
    Yoga::Text* m_all_beds_text{nullptr};
    Yoga::Text* m_single_bed_text{nullptr};
    Yoga::ComboBox* m_geometry_handling{nullptr};
    Yoga::ToggleButton* m_enable_rotations_toggle{nullptr};
    PivotPicker* m_pivot_picker{nullptr};
    Yoga::Item* m_bed_segments_section{nullptr};
    Yoga::Separator* m_bed_segments_separator{nullptr};
    Yoga::LayoutButton* m_arrange_button{nullptr};
    std::optional<Domain::BedSegments> m_bed_segments;
    std::optional<Domain::Vec2d> m_auxiliary_travel_anchor;

    std::string m_arrange_all_label{Biz::_u8L("Arrange all")};
    std::string m_arrange_beds_label{Biz::_u8L("Arrange beds")};
};

} // namespace Slic3r::App::Plater
