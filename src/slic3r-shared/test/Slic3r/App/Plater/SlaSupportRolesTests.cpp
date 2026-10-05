// M7.8.2: what a support takes from the role of its point. The rulebook (R4.1 and R4.3 - R4.6 of
// doc/sla-fork/supports/rulebook.md) fixes the tip class of every role, and this is the one place
// that says which preset of the tool a role is applied with, so the auto support classifier of
// M2.37 and any other caller ask for the same answer. Nothing here builds a tree: what a role means
// is what tests/sla_print/sla_support_roles_tests.cpp generates the points for, and the anchors a
// very large model gets are M7.8.3. What the automatic placement does with the answer - the tip
// class of a generated point, with the two settings of M2.37 deciding it - is M7.8.8.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>
#include <string>

#include "Slic3r/App/Plater/SlaSupportAutoPresets.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsSettings.hpp"
#include "Slic3r/App/Plater/SlaSupportRoles.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

using Catch::Approx;
using Slic3r::App::Plater::rulebook_tip_class;
using Slic3r::App::Plater::sla_auto_detail_preset_name;
using Slic3r::App::Plater::sla_role_tip_class;
using Slic3r::App::Plater::sla_support_preset;
using Slic3r::App::Plater::sla_support_preset_name;
using Slic3r::Domain::sla::SupportAutoDetailPreset;
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

    // The anchors of a very large object take the largest tip, which is the fifth preset of the tool
    // (M7.8.3, R4.2 and the size table of the rulebook).
    CHECK(rulebook_tip_class(Role::AnchorLarge) == "xheavy");
    CHECK(sla_support_preset("xheavy").tip_diameter_mm == Approx(0.6));

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
    const std::array<Role, 6> roles{
        Role::Anchor,
        Role::Island,
        Role::SmallIsland,
        Role::Overhang,
        Role::Fragile,
        Role::AnchorLarge
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
    // a fragile feature is built from the smallest tip and an anchor from the heaviest. What the
    // numbers between them are is the size table of M7.8.1.
    const double mini   = sla_support_preset("mini").tip_diameter_mm;
    const double light  = sla_support_preset("light").tip_diameter_mm;
    const double medium = sla_support_preset("medium").tip_diameter_mm;
    const double heavy  = sla_support_preset("heavy").tip_diameter_mm;
    const double xheavy = sla_support_preset("xheavy").tip_diameter_mm;
    CHECK(mini < light);
    CHECK(light < medium);
    CHECK(medium < heavy);
    CHECK(heavy < xheavy);
}

TEST_CASE("The tip classes of the roles are in the order of the enum", "[SlaSupportRoles]")
{
    // The mapping answers with a preset per value of the enumeration, so a role that is added to
    // the point later cannot land outside of the table.
    using Role = SupportPoint::Role;
    const std::array<Role, 7> every_role{
        Role::Unknown,
        Role::Anchor,
        Role::Island,
        Role::SmallIsland,
        Role::Overhang,
        Role::Fragile,
        Role::AnchorLarge
    };

    for (const Role role : every_role)
        CHECK_FALSE(rulebook_tip_class(role).empty());
}

// M7.8.8: the roles of M7.8.2 reach the sizes of the supports. Which class a generated point takes
// is decided by its role together with the two settings of the automatic placement of M2.37
// (support_auto_heavy_base and support_auto_detail_preset), and this covers every combination of the
// three, since a point is sized by exactly this one answer.
TEST_CASE(
    "The role of a generated point and the two settings decide its tip class",
    "[SlaSupportRoles]"
)
{
    using Role = SupportPoint::Role;

    /// What a role takes, one row per role of the enum: the class with the heavy base on and the
    /// class with it off. A null cell is the rule of that row - the class the detail setting names,
    /// whatever it is - which is what the two settings decide between for the roles they touch.
    struct RoleRow
    {
        Role role;
        const char* with_heavy_base;
        const char* without_heavy_base;
    };

    const RoleRow rows[]{
        // No role: no class at all here, the band rule of M2.37 is what sizes such a point.
        {Role::Unknown, nullptr, nullptr},
        // R4.1 and R4.2: the lowest point and the anchors around it carry the whole part early in
        // the print, so they take the heavy class (and xheavy on a very large object) while the
        // heavy base is on, and the detail preset when it is off.
        {Role::Anchor, "heavy", nullptr},
        {Role::AnchorLarge, "xheavy", nullptr},
        // R4.3: an island keeps the medium tip unless the detail setting asks for something
        // lighter, which is every choice the key offers (mini, light, medium).
        {Role::Island, nullptr, nullptr},
        {Role::SmallIsland, nullptr, nullptr},
        // R4.6: overhangs and the rest are light by default, so they take the detail preset.
        {Role::Overhang, nullptr, nullptr},
        // R4.4 and R4.5: the minimum tip whatever the settings ask for, since a thick support under
        // a thin feature snaps the feature off when it is removed.
        {Role::Fragile, "mini", "mini"}
    };

    const std::array<SupportAutoDetailPreset, 3> every_detail{
        SupportAutoDetailPreset::Mini,
        SupportAutoDetailPreset::Light,
        SupportAutoDetailPreset::Medium
    };

    for (const SupportAutoDetailPreset detail : every_detail) {
        for (const bool heavy_base : {true, false}) {
            for (const RoleRow& row : rows) {
                INFO(
                    "role "
                    << static_cast<int>(row.role)
                    << ", heavy base "
                    << heavy_base
                    << ", detail "
                    << static_cast<int>(detail)
                );

                const std::optional<std::string> tip_class =
                    sla_role_tip_class(row.role, heavy_base, detail);

                if (row.role == Role::Unknown) {
                    // A point of a project written before the roles existed, or one from a path
                    // that does not classify, has no class to take here.
                    CHECK_FALSE(tip_class.has_value());
                    continue;
                }

                REQUIRE(tip_class.has_value());
                const char* named = heavy_base ? row.with_heavy_base : row.without_heavy_base;
                const std::string expected =
                    named == nullptr ? sla_auto_detail_preset_name(detail) : std::string{named};
                CHECK(*tip_class == expected);

                // Every answer is a class the tool has a button for, so the caller can hand the
                // name straight to apply_sla_support_preset().
                const bool is_a_class = *tip_class == "mini"
                    || *tip_class == "light"
                    || *tip_class == "medium"
                    || *tip_class == "heavy"
                    || *tip_class == "xheavy";
                CHECK(is_a_class);
            }
        }
    }

    // The anchors are the only roles the heavy base is about, and the fragile points are the only
    // ones it cannot touch: with the setting off an anchor is a light support like any other, and a
    // spike keeps the minimum tip whatever the setting says.
    const std::optional<std::string> anchor_off =
        sla_role_tip_class(Role::Anchor, false, SupportAutoDetailPreset::Medium);
    const std::optional<std::string> large_anchor_off =
        sla_role_tip_class(Role::AnchorLarge, false, SupportAutoDetailPreset::Mini);
    const std::optional<std::string> fragile_on =
        sla_role_tip_class(Role::Fragile, true, SupportAutoDetailPreset::Medium);
    REQUIRE(anchor_off.has_value());
    REQUIRE(large_anchor_off.has_value());
    REQUIRE(fragile_on.has_value());
    CHECK(*anchor_off == "medium");
    CHECK(*large_anchor_off == "mini");
    CHECK(*fragile_on == "mini");

    // A role the enumeration of a build that knew more of them does not fall off the table: the
    // answer is a class of the tool like any other.
    const std::optional<std::string> of_an_unknown_value =
        sla_role_tip_class(static_cast<Role>(200), true, SupportAutoDetailPreset::Light);
    REQUIRE(of_an_unknown_value.has_value());
    CHECK(*of_an_unknown_value == "light");
}
