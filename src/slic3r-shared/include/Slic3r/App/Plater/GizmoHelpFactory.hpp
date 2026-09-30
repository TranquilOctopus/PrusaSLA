#pragma once

#include "Slic3r/App/Render/ImguiTypes.hpp"
#include "Slic3r/App/Yoga/Namespace.hpp"

#include <vector>
#include <variant>

namespace Slic3r::App::Yoga {
class Item;
class Icon;
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Plater {

class GizmoHelpFactory
{
public:
    struct HelpIcon
    {
        Render::Icon icon{Render::Icon::None};
        Yoga::Unit min_width{25.f};
        Yoga::Unit min_height{25.f};
    };

    /**
     * Help icon can either be an Icon, or KeyIcon constructed from std::string
     */
    using HelpItem = std::variant<HelpIcon, std::string>;

    /**
     * Initialize Help with container fot help items
     */
    void init(Yoga::Item* container);

    /**
     * @brief Adds a help row: its icons and the label of the action they do.
     *
     * @param icons  The mouse buttons or key icons of the row, in the order they are shown.
     * @param title  The label of the action.
     * @return The label of the row, so that a tool whose wording depends on the printer technology
     * can give it another text later (see PaintOnSupportsDialog::set_technology).
     */
    Yoga::Text* add_item(const std::vector<HelpItem>& icons, const std::string& title);

private:
    Yoga::Item* m_container{nullptr};
};

} // namespace Slic3r::App::Plater
