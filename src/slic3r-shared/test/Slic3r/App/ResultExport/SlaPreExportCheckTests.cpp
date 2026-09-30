#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/ResultExport/SlaPreExportCheck.hpp"
#include "libslic3r/SLAResult.hpp"

#include <string>
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

SlaIssue make_cup(size_t layer, double volume_mm3 = 0., const std::string& object_name = {})
{
    SlaIssue issue;
    issue.kind        = SlaIssue::Kind::Cup;
    issue.layer       = layer;
    issue.object_name = object_name;
    issue.position    = Slic3r::Domain::Vec3d::Zero();
    issue.note        = volume_mm3 > 0. ?
            "cup, 4.20 mm2 opening, layers " + std::to_string(layer) + "-"
                + std::to_string(layer + 2) + ", " + std::to_string(volume_mm3) + " mm3" :
            "Test cup";
    return issue;
}

SlaIssue make_trapped_resin(size_t layer, double volume_mm3 = 0., const std::string& object_name = {})
{
    SlaIssue issue;
    issue.kind        = SlaIssue::Kind::TrappedResin;
    issue.layer       = layer;
    issue.object_name = object_name;
    issue.position    = Slic3r::Domain::Vec3d::Zero();
    issue.note        = volume_mm3 > 0. ?
            "trapped resin, layers " + std::to_string(layer) + "-"
                + std::to_string(layer + 2) + ", " + std::to_string(volume_mm3) + " mm3" :
            "Test trapped resin";
    return issue;
}

SlaIssue make_high_peel_layer(size_t layer, double force_n = 0.)
{
    SlaIssue issue;
    issue.kind     = SlaIssue::Kind::HighPeelForce;
    issue.layer    = layer;
    issue.position = Slic3r::Domain::Vec3d::Zero();
    issue.note     = force_n > 0. ?
            "peel force " + std::to_string(force_n) + " N over 8.00 N" :
            "Test high peel layer";
    return issue;
}

SlaIssue make_other(size_t layer)
{
    SlaIssue issue;
    issue.kind     = SlaIssue::Kind::Other;
    issue.layer    = layer;
    issue.position = Slic3r::Domain::Vec3d::Zero();
    issue.note     = "Test issue of a kind without a meaning yet";
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
    // The raw UTF-8 bytes of the ellipsis, because a "\u2026" in a narrow literal is encoded in the
    // execution code page (this project is not compiled with /utf-8) and would not compare equal.
    CHECK(problems.lines.front() == "7 models have no supports: A, B, C, D, E, " "\xE2\x80\xA6" " and 2 more");
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
    const Problems problems = format_problems({}, { make_other(1), make_other(2) });

    CHECK(problems.empty());
}

TEST_CASE("SlaPreExportCheck - cups are counted with the largest one and the models", "[SlaPreExportCheck]")
{
    const Problems problems = format_problems(
        {},
        { make_cup(4, 8.0, "A"), make_cup(9, 12.5, "B"), make_cup(11, 3.0, "A") });

    REQUIRE(problems.lines.size() == 1);
    CHECK(problems.lines.front() == "3 cups, the largest 12.5 mm" "\xC2\xB3" " on A, B");
}

TEST_CASE("SlaPreExportCheck - one cup is singular and names its model", "[SlaPreExportCheck]")
{
    const Problems problems = format_problems({}, { make_cup(6, 4.25, "A") });

    REQUIRE(problems.lines.size() == 1);
    CHECK(problems.lines.front() == "1 cup, the largest 4.2 mm" "\xC2\xB3" " on A");
}

TEST_CASE("SlaPreExportCheck - a cup the slicer could not name is listed without a model", "[SlaPreExportCheck]")
{
    const Problems problems = format_problems({}, { make_cup(6, 4.25) });

    REQUIRE(problems.lines.size() == 1);
    CHECK(problems.lines.front() == "1 cup, the largest 4.2 mm" "\xC2\xB3");
}

TEST_CASE("SlaPreExportCheck - trapped resin is counted with the largest pocket and the models", "[SlaPreExportCheck]")
{
    const Problems problems =
        format_problems({}, { make_trapped_resin(2, 30.0, "A"), make_trapped_resin(7, 21.0, "B") });

    REQUIRE(problems.lines.size() == 1);
    CHECK(problems.lines.front() == "2 trapped resin pockets, the largest 30.0 mm" "\xC2\xB3" " on A, B");
}

TEST_CASE("SlaPreExportCheck - one pocket of trapped resin is singular", "[SlaPreExportCheck]")
{
    const Problems problems = format_problems({}, { make_trapped_resin(2, 21.0, "A") });

    REQUIRE(problems.lines.size() == 1);
    CHECK(problems.lines.front() == "1 trapped resin pocket, the largest 21.0 mm" "\xC2\xB3" " on A");
}

TEST_CASE("SlaPreExportCheck - a cavity without a volume in its note has no size", "[SlaPreExportCheck]")
{
    const Problems problems =
        format_problems({}, { make_trapped_resin(2, 0., "A"), make_cup(5, 0., "A") });

    REQUIRE(problems.lines.size() == 2);
    CHECK(problems.lines[0] == "1 trapped resin pocket on A");
    CHECK(problems.lines[1] == "1 cup on A");
}

TEST_CASE("SlaPreExportCheck - a long list of cavity models is truncated", "[SlaPreExportCheck]")
{
    const Problems problems = format_problems(
        {},
        { make_cup(1, 2., "A"),
          make_cup(2, 2., "B"),
          make_cup(3, 2., "C"),
          make_cup(4, 2., "D"),
          make_cup(5, 2., "E"),
          make_cup(6, 2., "F"),
          make_cup(7, 2., "G") });

    REQUIRE(problems.lines.size() == 1);
    CHECK(problems.lines.front()
          == "7 cups, the largest 2.0 mm" "\xC2\xB3" " on A, B, C, D, E, " "\xE2\x80\xA6" " and 2 more");
}

TEST_CASE("SlaPreExportCheck - high peel force layers are counted with the worst one", "[SlaPreExportCheck]")
{
    const Problems problems = format_problems(
        {},
        { make_high_peel_layer(9, 18.0), make_high_peel_layer(42, 24.1), make_high_peel_layer(11, 20.5) });

    REQUIRE(problems.lines.size() == 1);
    CHECK(problems.lines.front() == "High peel force on 3 layers with FEP, worst 24.1 N on layer 42");
}

TEST_CASE("SlaPreExportCheck - one high peel force layer is singular", "[SlaPreExportCheck]")
{
    const Problems problems = format_problems({}, { make_high_peel_layer(42, 24.1) });

    REQUIRE(problems.lines.size() == 1);
    CHECK(problems.lines.front() == "High peel force on 1 layer with FEP, worst 24.1 N on layer 42");
}

TEST_CASE("SlaPreExportCheck - the vat film of the slice is named in the peel line", "[SlaPreExportCheck]")
{
    const Problems problems =
        format_problems({}, { make_high_peel_layer(42, 24.1) }, "nFEP");

    REQUIRE(problems.lines.size() == 1);
    CHECK(problems.lines.front() == "High peel force on 1 layer with nFEP, worst 24.1 N on layer 42");
}

TEST_CASE("SlaPreExportCheck - a high peel layer without a force names the first one", "[SlaPreExportCheck]")
{
    const Problems problems = format_problems({}, { make_high_peel_layer(42), make_high_peel_layer(9) });

    REQUIRE(problems.lines.size() == 1);
    CHECK(problems.lines.front() == "High peel force on 2 layers with FEP, the first on layer 9");
}

TEST_CASE("SlaPreExportCheck - the problems are listed in the order a user reads them", "[SlaPreExportCheck]")
{
    const Problems problems = format_problems(
        { "A" },
        { make_high_peel_layer(42, 24.1),
          make_cup(9, 12.5, "B"),
          make_trapped_resin(2, 30.0, "B"),
          make_island(7) });

    REQUIRE(problems.lines.size() == 5);
    CHECK(problems.lines[0] == "1 model has no supports: A");
    CHECK(problems.lines[1] == "1 island, first on layer 7");
    CHECK(problems.lines[2] == "1 trapped resin pocket, the largest 30.0 mm" "\xC2\xB3" " on B");
    CHECK(problems.lines[3] == "1 cup, the largest 12.5 mm" "\xC2\xB3" " on B");
    CHECK(problems.lines[4] == "High peel force on 1 layer with FEP, worst 24.1 N on layer 42");
}

TEST_CASE("SlaPreExportCheck - every problem is listed in one text", "[SlaPreExportCheck]")
{
    const Problems problems = format_problems(
        { "A" },
        { make_island(7), make_trapped_resin(2, 30.0, "A"), make_cup(9, 12.5, "A") });

    REQUIRE(problems.lines.size() == 4);
    CHECK(problems.text()
          == "1 model has no supports: A\n"
             "1 island, first on layer 7\n"
             "1 trapped resin pocket, the largest 30.0 mm" "\xC2\xB3" " on A\n"
             "1 cup, the largest 12.5 mm" "\xC2\xB3" " on A");
}
