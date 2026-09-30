#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/SlaIssueRows.hpp"
#include "libslic3r/SLAResult.hpp"

#include <string>
#include <vector>

using Slic3r::App::build_sla_issue_rows;
using Slic3r::App::sla_issue_area_mm2;
using Slic3r::App::sla_issue_row_text;
using Slic3r::App::sla_issue_volume_mm3;
using Slic3r::App::SlaIssueRow;
using Slic3r::App::SlaIssueRows;
using Slic3r::Biz::Slicing::Sla::SlaIssue;

namespace {

SlaIssue make_issue(SlaIssue::Kind kind, size_t layer, const std::string& note = {})
{
    SlaIssue issue;
    issue.kind     = kind;
    issue.layer    = layer;
    issue.position = Slic3r::Domain::Vec3d::Zero();
    issue.note     = note;
    return issue;
}

SlaIssue make_island(size_t layer, double area = 0.)
{
    if (area <= 0.) {
        return make_issue(SlaIssue::Kind::Island, layer);
    }
    return make_issue(SlaIssue::Kind::Island, layer, "island, " + std::to_string(area) + " mm2");
}

SlaIssue make_cup(size_t layer, double area = 0.)
{
    if (area <= 0.) {
        return make_issue(SlaIssue::Kind::Cup, layer);
    }
    return make_issue(SlaIssue::Kind::Cup, layer, "cup, " + std::to_string(area) + " mm2");
}

SlaIssue make_trapped_resin(size_t layer, double volume = 0.)
{
    if (volume <= 0.) {
        return make_issue(SlaIssue::Kind::TrappedResin, layer);
    }
    return make_issue(
        SlaIssue::Kind::TrappedResin,
        layer,
        "trapped resin, layers " + std::to_string(layer) + "-" + std::to_string(layer + 2) + ", "
            + std::to_string(volume) + " mm3"
    );
}

} // namespace

TEST_CASE("SlaIssueRows - no issues means no list", "[SlaIssueRows]")
{
    const SlaIssueRows rows = build_sla_issue_rows({});

    CHECK(rows.empty());
    CHECK(rows.rows.empty());
    CHECK(rows.total_count == 0);
    CHECK(rows.island_count == 0);
    CHECK(rows.cup_count == 0);
    CHECK(rows.trapped_resin_count == 0);
    CHECK(rows.hidden_count == 0);
    CHECK(rows.more_text().empty());
}

TEST_CASE("SlaIssueRows - islands are listed with their layer and area", "[SlaIssueRows]")
{
    const SlaIssueRows rows = build_sla_issue_rows({make_island(217, 4.2), make_island(5, 1.5)});

    REQUIRE_FALSE(rows.empty());
    CHECK(rows.total_count == 2);
    CHECK(rows.island_count == 2);
    CHECK(rows.cup_count == 0);
    CHECK(rows.hidden_count == 0);
    REQUIRE(rows.rows.size() == 2);

    // Sorted by layer, the 0 based index of the result is shown counted from one.
    CHECK(rows.rows[0].layer == 5);
    CHECK(rows.rows[1].layer == 217);
    CHECK(rows.title() == "Issues (2)");
}

TEST_CASE("SlaIssueRows - a cup is listed next to the islands", "[SlaIssueRows]")
{
    const SlaIssueRows rows = build_sla_issue_rows({make_island(217, 4.2), make_cup(95, 0.8)});

    CHECK(rows.total_count == 2);
    CHECK(rows.island_count == 1);
    CHECK(rows.cup_count == 1);
    REQUIRE(rows.rows.size() == 2);
    CHECK(rows.rows[0].kind == SlaIssue::Kind::Cup);
    CHECK(rows.rows[0].layer == 95);
    CHECK(rows.rows[1].kind == SlaIssue::Kind::Island);
    CHECK(rows.rows[1].layer == 217);
}

TEST_CASE("SlaIssueRows - islands come before cups of the same layer", "[SlaIssueRows]")
{
    const SlaIssueRows rows = build_sla_issue_rows({make_cup(12), make_island(12)});

    REQUIRE(rows.rows.size() == 2);
    CHECK(rows.rows[0].kind == SlaIssue::Kind::Island);
    CHECK(rows.rows[1].kind == SlaIssue::Kind::Cup);
}

TEST_CASE("SlaIssueRows - the biggest piece of a layer comes first", "[SlaIssueRows]")
{
    const SlaIssueRows rows = build_sla_issue_rows({make_island(7, 0.5), make_island(7, 3.25)});

    REQUIRE(rows.rows.size() == 2);
    REQUIRE(rows.rows[0].area_mm2.has_value());
    REQUIRE(rows.rows[1].area_mm2.has_value());
    CHECK(*rows.rows[0].area_mm2 == Catch::Approx(3.25));
    CHECK(*rows.rows[1].area_mm2 == Catch::Approx(0.5));
}

TEST_CASE("SlaIssueRows - a row without a number sorts after one with it", "[SlaIssueRows]")
{
    const SlaIssueRows rows = build_sla_issue_rows({make_island(7), make_island(7, 1.0)});

    REQUIRE(rows.rows.size() == 2);
    CHECK(rows.rows[0].area_mm2.has_value());
    CHECK_FALSE(rows.rows[1].area_mm2.has_value());
}

TEST_CASE("SlaIssueRows - trapped resin is listed with the resin it holds", "[SlaIssueRows]")
{
    const SlaIssueRows rows = build_sla_issue_rows({make_island(3, 1.0), make_trapped_resin(3, 21.0)});

    CHECK(rows.total_count == 2);
    CHECK(rows.island_count == 1);
    CHECK(rows.trapped_resin_count == 1);
    REQUIRE(rows.rows.size() == 2);
    // Same layer, so the island comes first.
    CHECK(rows.rows[0].kind == SlaIssue::Kind::Island);
    CHECK(rows.rows[1].kind == SlaIssue::Kind::TrappedResin);
    // The note of a pocket starts with its layer range, which is not its size.
    CHECK_FALSE(rows.rows[1].area_mm2.has_value());
    REQUIRE(rows.rows[1].volume_mm3.has_value());
    CHECK(*rows.rows[1].volume_mm3 == Catch::Approx(21.0));
}

TEST_CASE("SlaIssueRows - a row carries where the issue was found", "[SlaIssueRows]")
{
    SlaIssue issue      = make_cup(7, 4.0);
    issue.position      = Slic3r::Domain::Vec3d(12.5, -3.25, 8.);

    const SlaIssueRows rows = build_sla_issue_rows({issue});

    REQUIRE(rows.rows.size() == 1);
    CHECK(rows.rows[0].position.x() == Catch::Approx(12.5));
    CHECK(rows.rows[0].position.y() == Catch::Approx(-3.25));
    CHECK(rows.rows[0].position.z() == Catch::Approx(8.0));
}

TEST_CASE("SlaIssueRows - issue kinds without a row yet are not listed", "[SlaIssueRows]")
{
    const SlaIssueRows rows = build_sla_issue_rows({
        make_island(3),
        make_issue(SlaIssue::Kind::Other, 6),
    });

    CHECK(rows.total_count == 1);
    CHECK(rows.island_count == 1);
    REQUIRE(rows.rows.size() == 1);
    CHECK(rows.rows[0].layer == 3);
}

TEST_CASE("SlaIssueRows - the list is capped and counts what it left out", "[SlaIssueRows]")
{
    std::vector<SlaIssue> issues;
    for (size_t layer = 0; layer < 25; ++layer) {
        issues.push_back(make_island(layer, 1.0));
    }

    const SlaIssueRows rows = build_sla_issue_rows(issues, 20);

    CHECK(rows.total_count == 25);
    CHECK(rows.rows.size() == 20);
    CHECK(rows.hidden_count == 5);
    // The first rows of the sorted list are the ones that are kept.
    CHECK(rows.rows.front().layer == 0);
    CHECK(rows.rows.back().layer == 19);
    CHECK(rows.more_text().size() > 4);
    CHECK(rows.more_text().find("5 more") != std::string::npos);
    // The ellipsis is appended outside the translated string, see SlaPreExportCheck.
    CHECK(rows.more_text().find("\xE2\x80\xA6") != std::string::npos);
    CHECK(rows.title() == "Issues (25)");
}

TEST_CASE("SlaIssueRows - a cap of zero lists nothing but still counts", "[SlaIssueRows]")
{
    const SlaIssueRows rows = build_sla_issue_rows({make_island(1), make_island(2)}, 0);

    CHECK(rows.rows.empty());
    CHECK(rows.total_count == 2);
    CHECK(rows.hidden_count == 2);
    CHECK_FALSE(rows.more_text().empty());
}

TEST_CASE("SlaIssueRows - the area is read out of the note of the slicer", "[SlaIssueRows]")
{
    SECTION("island note")
    {
        CHECK(*sla_issue_area_mm2("island, 4.20 mm2") == Catch::Approx(4.2));
    }
    SECTION("a note without a number has no area")
    {
        CHECK_FALSE(sla_issue_area_mm2("island").has_value());
    }
    SECTION("an empty note has no area")
    {
        CHECK_FALSE(sla_issue_area_mm2("").has_value());
    }
    SECTION("a lone dot is not a number")
    {
        CHECK_FALSE(sla_issue_area_mm2("island, . mm2").has_value());
    }
}

TEST_CASE("SlaIssueRows - the volume is read out of the note of the slicer", "[SlaIssueRows]")
{
    SECTION("the note of a pocket of trapped resin")
    {
        CHECK(*sla_issue_volume_mm3("trapped resin, layers 3-5, 21.00 mm3") == Catch::Approx(21.0));
    }
    SECTION("the note of a cup, which reports both")
    {
        CHECK(*sla_issue_volume_mm3("cup, 64.00 mm2 opening, layers 1-3, 192.00 mm3") == Catch::Approx(192.0));
    }
    SECTION("a note without a volume has none")
    {
        CHECK_FALSE(sla_issue_volume_mm3("island, 4.20 mm2").has_value());
    }
    SECTION("an empty note has none")
    {
        CHECK_FALSE(sla_issue_volume_mm3("").has_value());
    }
    SECTION("a unit without a number in front of it is not a volume")
    {
        CHECK_FALSE(sla_issue_volume_mm3("trapped resin, layers 3-5, mm3").has_value());
    }
}

TEST_CASE("SlaIssueRows - the row text names the kind, the layer and the size", "[SlaIssueRows]")
{
    SECTION("island with an area")
    {
        CHECK(
            sla_issue_row_text(SlaIssueRow{SlaIssue::Kind::Island, 217, 4.2})
            == "Island, layer 218  4.2 mm\xC2\xB2"
        );
    }
    SECTION("cup without an area")
    {
        CHECK(
            sla_issue_row_text(SlaIssueRow{SlaIssue::Kind::Cup, 0, std::nullopt}) == "Cup, layer 1"
        );
    }
    SECTION("trapped resin with the volume of resin it holds")
    {
        SlaIssueRow row;
        row.kind       = SlaIssue::Kind::TrappedResin;
        row.layer      = 3;
        row.volume_mm3 = 21.0;
        CHECK(sla_issue_row_text(row) == "Trapped resin, layer 4  21.0 mm\xC2\xB3");
    }
    SECTION("a kind without a name yet")
    {
        CHECK(
            sla_issue_row_text(SlaIssueRow{SlaIssue::Kind::Other, 3, 1.0})
            == "Issue, layer 4  1.0 mm\xC2\xB2"
        );
    }
}
