#pragma once

#include "Slic3r/App/MenuItemName.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"

#include <string>

namespace Slic3r::App::Yoga {
class Menu;
class MenuItem;
} // namespace Slic3r::App::Yoga

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {
class MenuItem;
class MenuManager;
class CommandBindingManager;

class MenuBuilder
{
public:
    MenuBuilder(
        MenuManager& menu_manager,
        CommandBindingManager& command_binding_manager,
        const Biz::ProjectInteractor* project_interactor = nullptr
    ) :
        m_menu_manager(menu_manager),
        m_command_binding_manager(command_binding_manager),
        m_project_interactor(project_interactor)
    {}

    // is_sla picks the SLA wording ('build plate') instead of the FFF one ('bed')
    static std::string item_name_translated(
        UniversalMenuItemName menu_item_name,
        bool is_sla = false
    );
    static Render::Icon item_icon(UniversalMenuItemName menu_item_name);
    static std::string icon_name(UniversalMenuItemName menu_item_name);

    void add_menu_items(Yoga::Menu* menu, App::MenuItem* root_menu_item);

    Yoga::MenuItem* add_menu_item(Yoga::Menu* menu, MenuItemName menu_item_name);

    Yoga::MenuItem* add_menu_item(Yoga::Menu* menu, App::MenuItem* menu_item);

    void add_submenu(Yoga::MenuItem* yoga_menu_item, App::MenuItem* menu_item);

private:
    bool is_sla() const;

private:
    MenuManager& m_menu_manager;
    CommandBindingManager& m_command_binding_manager;
    const Biz::ProjectInteractor* m_project_interactor{nullptr};
};

} // namespace Slic3r::App
