#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Plater/SlaIssueAnalysis.hpp"
#include "Slic3r/Biz/Slicing/SLAResult.hpp"

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
        REQUIRE(result.message.empty());
    }

    SECTION("Single island returns count 1 and correct layer")
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

    SECTION("Non-island issues are ignored")
    {
        std::vector<SlaIssue> issues = { make_cup(3), make_trapped_resin(7), make_cup(1) };
        SlaIssueAnalysis result = analyze_sla_issues_for_notification(issues);

        REQUIRE(result.island_count == 0);
        REQUIRE(result.lowest_layer == 0);
        REQUIRE(result.message.empty());
    }

    SECTION("Mixed issues - only islands counted")
    {
        std::vector<SlaIssue> issues = { make_island(15), make_cup(3), make_island(8), make_trapped_resin(12) };
        SlaIssueAnalysis result = analyze_sla_issues_for_notification(issues);

        REQUIRE(result.island_count == 2);
        REQUIRE(result.lowest_layer == 8);
        REQUIRE(result.message == "2 islands found, first on layer 8. They can fall off during printing.");
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
