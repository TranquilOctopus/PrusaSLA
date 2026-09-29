#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/ResultExport/SlaPreExportCheck.hpp"
#include "libslic3r/SLAResult.hpp"

#include <vector>

using Slic3r::App::SlaPreExportCheck::Problems;
using Slic3r::App::SlaPreExportCheck::format_problems;
using Slic3r::Biz::Slicing::Sla::SlaIssue;

namespace {

SlaIssue make_island(size_t layer)
{
    SlaIssue issue;
    issue.kind     = SlaIssue::Kind::Island;
    issue.layer    = layer;
    issue.position = Slic3r::Domain::Vec3d::Zero();
    issue.note     = "Test island";
    return issue;
}

SlaIssue make_cup(size_t layer)
{
    SlaIssue issue;
    issue.kind     = SlaIssue::Kind::Cup;
    issue.layer    = layer;
    issue.position = Slic3r::Domain::Vec3d::Zero();
    issue.note     = "Test cup";
    return issue;
}

} // namespace

TEST_CASE("SlaPreExportCheck - nothing to report means no checklist", "[SlaPreExportCheck]")
{
    const Problems problems = format_problems({}, {});

    CHECK(problems.empty());
    CHECK(problems.text().empty());
}

TEST_CASE("SlaPreExportCheck - unsupported models are named", "[SlaPreExportCheck]")
{
    const Problems problems = format_problems({ "A", "B" }, {});

    REQUIRE(problems.lines.size() == 1);
    CHECK(problems.lines.front() == "2 models have no supports: A, B");
}

TEST_CASE("SlaPreExportCheck - one unsupported model is singular", "[SlaPreExportCheck]")
{
    const Problems problems = format_problems({ "A" }, {});

    REQUIRE(problems.lines.size() == 1);
    CHECK(problems.lines.front() == "1 model has no supports: A");
}

TEST_CASE("SlaPreExportCheck - long model lists are truncated", "[SlaPreExportCheck]")
{
    const Problems problems = format_problems({ "A", "B", "C", "D", "E", "F", "G" }, {});

    REQUIRE(problems.lines.size() == 1);
    CHECK(problems.lines.front() == "7 models have no supports: A, B, C, D, E, \u2026 and 2 more");
}

TEST_CASE("SlaPreExportCheck - islands are counted with the first layer", "[SlaPreExportCheck]")
{
    const Problems problems = format_problems({}, { make_island(12), make_island(3) });

    REQUIRE(problems.lines.size() == 1);
    CHECK(problems.lines.front() == "2 islands, first on layer 3");
}

TEST_CASE("SlaPreExportCheck - one island is singular", "[SlaPreExportCheck]")
{
    const Problems problems = format_problems({}, { make_island(12) });

    REQUIRE(problems.lines.size() == 1);
    CHECK(problems.lines.front() == "1 island, first on layer 12");
}

TEST_CASE("SlaPreExportCheck - other issue kinds are not reported", "[SlaPreExportCheck]")
{
    const Problems problems = format_problems({}, { make_cup(1), make_cup(2) });

    CHECK(problems.empty());
}

TEST_CASE("SlaPreExportCheck - both problems are listed in one text", "[SlaPreExportCheck]")
{
    const Problems problems = format_problems({ "A" }, { make_island(7) });

    REQUIRE(problems.lines.size() == 2);
    CHECK(problems.text() == "1 model has no supports: A\n1 island, first on layer 7");
}
