#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Plater/SlaIssueAnalysis.hpp"
#include "libslic3r/SLAResult.hpp"

#include <vector>

using Slic3r::Biz::Slicing::Sla::SlaIssue;

namespace {

SlaIssue make_island(size_t layer, Slic3r::Domain::ObjectID object_id = {}) {
    SlaIssue issue;
    issue.kind = SlaIssue::Kind::Island;
    issue.layer = layer;
    issue.object_id = object_id;
    issue.position = Slic3r::Domain::Vec3d::Zero();
    issue.note = "Test island";
    return issue;
}

SlaIssue make_cup(size_t layer) {
    SlaIssue issue;
    issue.kind = SlaIssue::Kind::Cup;
    issue.layer = layer;
    issue.object_id = {};
    issue.position = Slic3r::Domain::Vec3d::Zero();
    issue.note = "Test cup";
    return issue;
}

SlaIssue make_trapped_resin(size_t layer) {
    SlaIssue issue;
    issue.kind = SlaIssue::Kind::TrappedResin;
    issue.layer = layer;
    issue.object_id = {};
    issue.position = Slic3r::Domain::Vec3d::Zero();
    issue.note = "Test trapped resin";
    return issue;
}

} // namespace

TEST_CASE("SlaIssueAnalysis - analyze_sla_issues_for_notification", "[SlaIssueAnalysis]")
{
    using Slic3r::App::Plater::analyze_sla_issues_for_notification;
    using Slic3r::App::Plater::SlaIssueAnalysis;

    SECTION("Empty issues returns zero count and empty message")
    {
        std::vector<SlaIssue> issues;
        SlaIssueAnalysis result = analyze_sla_issues_for_notification(issues);

        REQUIRE(result.island_count == 0);
        REQUIRE(result.lowest_layer == 0);
        REQUIRE(result.cup_count == 0);
        REQUIRE(result.trapped_resin_count == 0);
        REQUIRE(result.message.empty());
        REQUIRE(result.empty());
    }

    SECTION("A single island returns count 1 and correct layer")
    {
        std::vector<SlaIssue> issues = { make_island(42) };
        SlaIssueAnalysis result = analyze_sla_issues_for_notification(issues);

        REQUIRE(result.island_count == 1);
        REQUIRE(result.lowest_layer == 42);
        REQUIRE(result.message == "1 island found, first on layer 42. They can fall off during printing.");
    }

    SECTION("Multiple islands returns correct count and lowest layer")
    {
        std::vector<SlaIssue> issues = { make_island(10), make_island(5), make_island(20) };
        SlaIssueAnalysis result = analyze_sla_issues_for_notification(issues);

        REQUIRE(result.island_count == 3);
        REQUIRE(result.lowest_layer == 5);
        REQUIRE(result.message == "3 islands found, first on layer 5. They can fall off during printing.");
    }

    SECTION("Cups are reported with their count and their lowest layer")
    {
        std::vector<SlaIssue> issues = { make_cup(30), make_cup(12) };
        SlaIssueAnalysis result = analyze_sla_issues_for_notification(issues);

        REQUIRE(result.island_count == 0);
        REQUIRE(result.cup_count == 2);
        REQUIRE(result.cup_lowest_layer == 12);
        REQUIRE(result.message == "2 cups found, first on layer 12. They can hold a vacuum against the film on every peel.");
    }

    SECTION("Trapped resin is reported with its count and its lowest layer")
    {
        std::vector<SlaIssue> issues = { make_trapped_resin(9), make_trapped_resin(4) };
        SlaIssueAnalysis result = analyze_sla_issues_for_notification(issues);

        REQUIRE(result.trapped_resin_count == 2);
        REQUIRE(result.trapped_resin_lowest_layer == 4);
        REQUIRE(result.message == "2 trapped resin pockets found, first on layer 4. The resin in it cannot drain.");
    }

    SECTION("Every kind found is named, the most harmful first")
    {
        std::vector<SlaIssue> issues = {
            make_trapped_resin(12), make_cup(3), make_island(15), make_island(8)
        };
        SlaIssueAnalysis result = analyze_sla_issues_for_notification(issues);

        REQUIRE(result.island_count == 2);
        REQUIRE(result.cup_count == 1);
        REQUIRE(result.trapped_resin_count == 1);
        REQUIRE(
            result.message
            == "2 islands found, first on layer 8. They can fall off during printing. "
               "1 cup found, first on layer 3. They can hold a vacuum against the film on every peel. "
               "1 trapped resin pocket found, first on layer 12. The resin in it cannot drain."
        );
    }

    SECTION("Kinds the notification has no name for are ignored")
    {
        SlaIssue unknown;
        unknown.kind  = SlaIssue::Kind::Other;
        unknown.layer = 5;
        std::vector<SlaIssue> issues = { unknown };

        SlaIssueAnalysis result = analyze_sla_issues_for_notification(issues);

        REQUIRE(result.island_count == 0);
        REQUIRE(result.cup_count == 0);
        REQUIRE(result.trapped_resin_count == 0);
        REQUIRE(result.message.empty());
        REQUIRE(result.empty());
    }

    SECTION("Islands on layer 0")
    {
        std::vector<SlaIssue> issues = { make_island(0), make_island(5) };
        SlaIssueAnalysis result = analyze_sla_issues_for_notification(issues);

        REQUIRE(result.island_count == 2);
        REQUIRE(result.lowest_layer == 0);
        REQUIRE(result.message == "2 islands found, first on layer 0. They can fall off during printing.");
    }

    SECTION("Single island uses singular 'island'")
    {
        std::vector<SlaIssue> issues = { make_island(100) };
        SlaIssueAnalysis result = analyze_sla_issues_for_notification(issues);

        REQUIRE(result.message.find("1 island found") != std::string::npos);
        REQUIRE(result.message.find("islands found") == std::string::npos);
    }

    SECTION("Multiple islands uses plural 'islands'")
    {
        std::vector<SlaIssue> issues = { make_island(10), make_island(20) };
        SlaIssueAnalysis result = analyze_sla_issues_for_notification(issues);

        REQUIRE(result.message.find("2 islands found") != std::string::npos);
        REQUIRE(result.message.find("1 island found") == std::string::npos);
    }
}
