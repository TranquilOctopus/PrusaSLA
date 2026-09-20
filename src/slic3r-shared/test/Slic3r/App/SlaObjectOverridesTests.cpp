#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Domain/ConfigDef.hpp"
#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"

using namespace Slic3r::Domain;

TEST_CASE("SLA per-object override keys exist and have correct properties", "[sla][overrides]")
{
    ConfigDefsSLA defs;
    defs.init();

    SECTION("supports_enable")
    {
        const ConfigItemDef* def = defs.sla_print_settings.def("supports_enable");
        REQUIRE(def != nullptr);
        REQUIRE(def->overrides_in.contains(Object));
        REQUIRE(def->category == ConfigItemDef::Category::Print_Supports);
        REQUIRE(def->gui_type == ConfigItemDef::GUIType::checkbox);
        REQUIRE(def->type_id == typeid(bool));
    }

    SECTION("support_points_density_relative")
    {
        const ConfigItemDef* def = defs.sla_print_settings.def("support_points_density_relative");
        REQUIRE(def != nullptr);
        REQUIRE(def->overrides_in.contains(Object));
        REQUIRE(def->overrides_in.contains(Material));
        REQUIRE(def->category == ConfigItemDef::Category::Print_Supports);
        REQUIRE(def->gui_type == ConfigItemDef::GUIType::spinbox);
        REQUIRE(def->type_id == typeid(int));
    }

    SECTION("hollowing_enable")
    {
        const ConfigItemDef* def = defs.sla_print_settings.def("hollowing_enable");
        REQUIRE(def != nullptr);
        REQUIRE(def->overrides_in.contains(Object));
        REQUIRE(def->category == ConfigItemDef::Category::Print_Hollowing);
        REQUIRE(def->gui_type == ConfigItemDef::GUIType::checkbox);
        REQUIRE(def->type_id == typeid(bool));
    }

    SECTION("hollowing_min_thickness")
    {
        const ConfigItemDef* def = defs.sla_print_settings.def("hollowing_min_thickness");
        REQUIRE(def != nullptr);
        REQUIRE(def->overrides_in.contains(Object));
        REQUIRE(def->category == ConfigItemDef::Category::Print_Hollowing);
        REQUIRE(def->gui_type == ConfigItemDef::GUIType::textfield);
        REQUIRE(def->type_id == typeid(double));
    }

    SECTION("hollowing_quality")
    {
        const ConfigItemDef* def = defs.sla_print_settings.def("hollowing_quality");
        REQUIRE(def != nullptr);
        REQUIRE(def->overrides_in.contains(Object));
        REQUIRE(def->category == ConfigItemDef::Category::Print_Hollowing);
        REQUIRE(def->gui_type == ConfigItemDef::GUIType::textfield);
        REQUIRE(def->type_id == typeid(double));
    }

    SECTION("hollowing_closing_distance")
    {
        const ConfigItemDef* def = defs.sla_print_settings.def("hollowing_closing_distance");
        REQUIRE(def != nullptr);
        REQUIRE(def->overrides_in.contains(Object));
        REQUIRE(def->category == ConfigItemDef::Category::Print_Hollowing);
        REQUIRE(def->gui_type == ConfigItemDef::GUIType::textfield);
        REQUIRE(def->type_id == typeid(double));
    }
}

TEST_CASE("SLA per-object override categories are Print_Supports, Print_Hollowing, Print_Pad", "[sla][overrides]")
{
    ConfigDefsSLA defs;
    defs.init();

    std::set<ConfigItemDef::Category> sla_override_categories;
    for (const ConfigItemDef* def : defs.sla_print_settings.defs()) {
        if (def->overrides_in.contains(Object)) {
            sla_override_categories.insert(def->category);
        }
    }

    REQUIRE(sla_override_categories.contains(ConfigItemDef::Category::Print_Supports));
    REQUIRE(sla_override_categories.contains(ConfigItemDef::Category::Print_Hollowing));
    REQUIRE(sla_override_categories.contains(ConfigItemDef::Category::Print_Pad));
}

TEST_CASE("SLA object settings box has overrides for required keys", "[sla][overrides]")
{
    SLAPrintSettings sla_print_settings;
    sla_print_settings.init();

    REQUIRE(sla_print_settings.overrides.has("supports_enable"));
    REQUIRE(sla_print_settings.overrides.has("support_points_density_relative"));
    REQUIRE(sla_print_settings.overrides.has("hollowing_enable"));
    REQUIRE(sla_print_settings.overrides.has("hollowing_min_thickness"));
    REQUIRE(sla_print_settings.overrides.has("hollowing_quality"));
    REQUIRE(sla_print_settings.overrides.has("hollowing_closing_distance"));
}