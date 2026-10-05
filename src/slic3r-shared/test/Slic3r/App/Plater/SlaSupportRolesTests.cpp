// M7.8.2: what a support takes from the role of its point. The rulebook (R4.1 and R4.3 - R4.6 of
// doc/sla-fork/supports/rulebook.md) fixes the tip class of every role, and this is the one place
// that says which preset of the tool a role is applied with, so the auto support classifier of
// M2.37 and any other caller ask for the same answer. M7.8.5 adds the role of a detailed region
// (R4.9), which takes the same minimum tip a fragile feature takes, and M7.8.3 the role of the anchors
// of a very large object. Nothing here builds a tree: what a role means is what
// tests/sla_print/sla_support_roles_tests.cpp generates the points for.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

#include "Slic3r/App/Plater/SlaSupportPointsSettings.hpp"
#include "Slic3r/App/Plater/SlaSupportRoles.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

using Catch::Approx;
using Slic3r::App::Plater::rulebook_tip_class;
using Slic3r::App::Plater::sla_support_preset;
using Slic3r::App::Plater::sla_support_preset_name;
using Slic3r::Domain::SLA::SupportPoint;

TEST_CASE("Every role of a point takes the tip class the rulebook gives it", "[SlaSupportRoles]")
{
    using Role = SupportPoint::Role;

    // The size table of the rulebook: an anchor is the heavy tip, an island the medium one, a small
    // island and an overhang the light one, a fragile feature the minimum tip, and a detailed region
    // the same minimum tip whatever else it would have been (R4.9).
    CHECK(rulebook_tip_class(Role::Anchor) == "heavy");
    CHECK(rulebook_tip_class(Role::Island) == "medium");
    CHECK(rulebook_tip_class(Role::SmallIsland) == "light");
    CHECK(rulebook_tip_class(Role::Overhang) == "light");
    CHECK(rulebook_tip_class(Role::Fragile) == "mini");
    CHECK(rulebook_tip_class(Role::Detail) == "mini");

    // The anchors of a very large object take the largest tip, which is the fifth preset of the tool
    // (M7.8.3, R4.2 and the size table of the rulebook).
    CHECK(rulebook_tip_class(Role::AnchorLarge) == "xheavy");
    CHECK(sla_support_preset("xheavy").geometry.tip_diameter_mm == Approx(0.6));

    // A point placed by hand, and a project written before the roles existed, carry no role. They
    // take the light tip, which is what such a support got before and what R4.6 asks for anyway.
    CHECK(rulebook_tip_class(Role::Unknown) == "light");
}

TEST_CASE("The tip class of a role is a preset of the tool", "[SlaSupportRoles]")
{
    using Role = SupportPoint::Role;

    // The names this mapping answers with are the ones the preset buttons apply, so the classifier of
    // M2.37 can hand the name straight to them. The fifth one, the class of the anchors of a very
    // large object, is a preset of the tool as well since M7.8.1.
    const std::array<Role, 7> roles{
        Role::Anchor,
        Role::Island,
        Role::SmallIsland,
        Role::Overhang,
        Role::Fragile,
        Role::AnchorLarge,
        Role::Detail
    };
    for (const Role role : roles) {
        const std::string& tip_class = rulebook_tip_class(role);
        INFO("role " << static_cast<int>(role) << " takes " << tip_class);
        const bool is_a_preset = tip_class == "mini"
            || tip_class == "light"
            || tip_class == "medium"
            || tip_class == "heavy"
            || tip_class == "xheavy";
        CHECK(is_a_preset);
    }

    CHECK(sla_support_preset_name(0) == "mini");
    CHECK(sla_support_preset_name(3) == "heavy");
    CHECK(sla_support_preset_name(4) == "xheavy");

    // The mapping and the presets agree on which class is the small one and which the heavy one:
    // a fragile feature and a detailed region are built from the smallest tip and an anchor from the
    // heaviest. What the numbers between them are is the size table of M7.8.1.
    const double mini   = sla_support_preset("mini").geometry.tip_diameter_mm;
    const double light  = sla_support_preset("light").geometry.tip_diameter_mm;
    const double medium = sla_support_preset("medium").geometry.tip_diameter_mm;
    const double heavy  = sla_support_preset("heavy").geometry.tip_diameter_mm;
    const double xheavy = sla_support_preset("xheavy").geometry.tip_diameter_mm;
    CHECK(mini < light);
    CHECK(light < medium);
    CHECK(medium < heavy);
    CHECK(heavy < xheavy);

    // The class of a detailed region is the T0.1 one of that table, since a light tip is what keeps
    // a fine, dense or highly curved surface printable and undamaged (R4.9).
    CHECK(mini == 0.1);
    CHECK(rulebook_tip_class(Role::Detail) == sla_support_preset_name(0));
}

TEST_CASE("The tip classes of the roles are in the order of the enum", "[SlaSupportRoles]")
{
    // The mapping answers with a preset per value of the enumeration, so a role that is added to
    // the point later cannot land outside of the table.
    using Role = SupportPoint::Role;
    const std::array<Role, 8> every_role{
        Role::Unknown,
        Role::Anchor,
        Role::Island,
        Role::SmallIsland,
        Role::Overhang,
        Role::Fragile,
        Role::AnchorLarge,
        Role::Detail
    };

    for (const Role role : every_role)
        CHECK_FALSE(rulebook_tip_class(role).empty());
}