// M7.8.2: what a support takes from the role of its point. The rulebook (R4.1 and R4.3 - R4.6 of
// doc/sla-fork/supports/rulebook.md) fixes the tip class of every role, and this is the one place
// that says which preset of the tool a role is applied with, so the auto support classifier of
// M2.37 and any other caller ask for the same answer. Nothing here builds a tree: what a role means
// is what tests/sla_print/sla_support_roles_tests.cpp generates the points for.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

#include "Slic3r/App/Plater/SlaSupportPointsSettings.hpp"
#include "Slic3r/App/Plater/SlaSupportRoles.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

using Slic3r::App::Plater::rulebook_tip_class;
using Slic3r::App::Plater::sla_support_preset;
using Slic3r::App::Plater::sla_support_preset_name;
using Slic3r::Domain::SLA::SupportPoint;

TEST_CASE("Every role of a point takes the tip class the rulebook gives it", "[SlaSupportRoles]")
{
    using Role = SupportPoint::Role;

    // The size table of the rulebook: an anchor is the heavy tip, an island the medium one, a small
    // island and an overhang the light one, and a fragile feature the minimum tip.
    CHECK(rulebook_tip_class(Role::Anchor) == "heavy");
    CHECK(rulebook_tip_class(Role::Island) == "medium");
    CHECK(rulebook_tip_class(Role::SmallIsland) == "light");
    CHECK(rulebook_tip_class(Role::Overhang) == "light");
    CHECK(rulebook_tip_class(Role::Fragile) == "mini");

    // A point placed by hand, and a project written before the roles existed, carry no role. They
    // take the light tip, which is what such a support got before and what R4.6 asks for anyway.
    CHECK(rulebook_tip_class(Role::Unknown) == "light");
}

TEST_CASE("The tip class of a role is a preset of the tool", "[SlaSupportRoles]")
{
    using Role = SupportPoint::Role;

    // The names this mapping answers with are the ones the four preset buttons apply, so the
    // classifier of M2.37 can hand the name straight to them.
    const std::array<Role, 5>
        roles{Role::Anchor, Role::Island, Role::SmallIsland, Role::Overhang, Role::Fragile};
    for (const Role role : roles) {
        const std::string& tip_class = rulebook_tip_class(role);
        INFO("role " << static_cast<int>(role) << " takes " << tip_class);
        const bool is_a_preset = tip_class == "mini"
            || tip_class == "light"
            || tip_class == "medium"
            || tip_class == "heavy";
        CHECK(is_a_preset);
    }

    CHECK(sla_support_preset_name(0) == "mini");
    CHECK(sla_support_preset_name(3) == "heavy");

    // The mapping and the presets agree on which class is the small one and which the heavy one:
    // a fragile feature is built from the smallest tip and an anchor from the heaviest. What the
    // numbers between them are is the size table of M7.8.1.
    const double mini   = sla_support_preset("mini").tip_diameter_mm;
    const double light  = sla_support_preset("light").tip_diameter_mm;
    const double medium = sla_support_preset("medium").tip_diameter_mm;
    const double heavy  = sla_support_preset("heavy").tip_diameter_mm;
    CHECK(mini < light);
    CHECK(light < medium);
    CHECK(medium < heavy);
}

TEST_CASE("The tip classes of the roles are in the order of the enum", "[SlaSupportRoles]")
{
    // The mapping answers with a preset per value of the enumeration, so a role that is added to
    // the point later cannot land outside of the table.
    using Role = SupportPoint::Role;
    const std::array<Role, 6> every_role{
        Role::Unknown,
        Role::Anchor,
        Role::Island,
        Role::SmallIsland,
        Role::Overhang,
        Role::Fragile
    };

    for (const Role role : every_role)
        CHECK_FALSE(rulebook_tip_class(role).empty());
}
