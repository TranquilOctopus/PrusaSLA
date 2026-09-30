#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/ResinImportReport.hpp"

#include <string>
#include <utility>
#include <vector>

using Slic3r::App::MappingBadge;
using Slic3r::App::MappingRow;
using Slic3r::App::MAX_PRESET_NAME_LENGTH;
using Slic3r::Biz::ResinProfile::MappedField;
using Slic3r::Biz::ResinProfile::MappingStatus;
using Slic3r::Biz::ResinProfile::ResinImportResult;

namespace {

MappedField make_field(
    std::string source_key,
    std::string source_value,
    std::string target_key,
    std::string value,
    MappingStatus status
)
{
    MappedField field;
    field.source_key   = std::move(source_key);
    field.source_value = std::move(source_value);
    field.target_key   = std::move(target_key);
    field.value        = std::move(value);
    field.status       = status;
    return field;
}

} // namespace

TEST_CASE("badge_of gives every status a badge", "[resin_import][report]")
{
    SECTION("the five statuses of the mapping table")
    {
        CHECK(Slic3r::App::badge_of(MappingStatus::Exact) == MappingBadge::Exact);
        CHECK(Slic3r::App::badge_of(MappingStatus::Converted) == MappingBadge::Converted);
        CHECK(Slic3r::App::badge_of(MappingStatus::Approximated) == MappingBadge::Approximated);
        CHECK(Slic3r::App::badge_of(MappingStatus::NotApplicable) == MappingBadge::NotApplicable);
        CHECK(Slic3r::App::badge_of(MappingStatus::Unknown) == MappingBadge::Unknown);
    }
}

TEST_CASE("badge_label names every badge", "[resin_import][report]")
{
    // The names of the design table in doc/sla-fork/ROADMAP.md, so the counts under the table and
    // the rows of the table are worded the same way.
    CHECK(Slic3r::App::badge_label(MappingBadge::Exact) == "Exact");
    CHECK(Slic3r::App::badge_label(MappingBadge::Converted) == "Converted");
    CHECK(Slic3r::App::badge_label(MappingBadge::Approximated) == "Approximated");
    CHECK(Slic3r::App::badge_label(MappingBadge::NotApplicable) == "Not applicable");
    CHECK(Slic3r::App::badge_label(MappingBadge::Unknown) == "Unknown");
}

TEST_CASE("build_mapping_rows turns a result into one row per key", "[resin_import][report]")
{
    SECTION("a mapped value, an approximation and a key that only lands in the report")
    {
        ResinImportResult result;
        result.mapping.report = {
            make_field("normalExposureTime", "2.5", "exposure_time", "2.5", MappingStatus::Exact),
            make_field(
                "bottomLayerCount",
                "40",
                "resin_faded_layers",
                "20",
                MappingStatus::Approximated
            ),
            make_field("normalLayerLiftHeight", "5", "", "", MappingStatus::NotApplicable),
            make_field("custom_key_xyz", "42", "", "", MappingStatus::Unknown),
        };

        const std::vector<MappingRow> rows = Slic3r::App::build_mapping_rows(result);

        REQUIRE(rows.size() == 4);
        // The order of the report is kept, so the recognized keys stay above the unknown ones.
        CHECK(rows[0].source_key == "normalExposureTime");
        CHECK(rows[0].target_key == "exposure_time");
        CHECK(rows[0].value == "2.5");
        CHECK(rows[0].badge == MappingBadge::Exact);
        CHECK(rows[0].writes_value());
        CHECK(rows[0].note.empty());

        CHECK(rows[1].badge == MappingBadge::Approximated);
        CHECK(rows[1].writes_value());
        // A value the file had is on the row as well, so a clamp is readable off the row.
        CHECK(rows[1].source_value == "40");
        CHECK(rows[1].value == "20");

        // Nothing is written for the last two, and nothing is lost either: the row is still there
        // with the value it had and the status that says why.
        CHECK_FALSE(rows[2].writes_value());
        CHECK(rows[2].target_key.empty());
        CHECK(rows[2].source_value == "5");
        CHECK(rows[2].badge == MappingBadge::NotApplicable);
        CHECK(rows[3].badge == MappingBadge::Unknown);
        CHECK(rows[3].source_key == "custom_key_xyz");
        CHECK(rows[3].source_value == "42");
    }

    SECTION("the note of a row is carried over")
    {
        ResinImportResult result;
        MappedField clamped = make_field(
            "bottomLayerCount",
            "40",
            "resin_faded_layers",
            "20",
            MappingStatus::Approximated
        );
        clamped.note = "40 layers, clamped to 3-20";
        result.mapping.report.push_back(clamped);

        const std::vector<MappingRow> rows = Slic3r::App::build_mapping_rows(result);
        REQUIRE(rows.size() == 1);
        CHECK(rows[0].note == "40 layers, clamped to 3-20");
    }

    SECTION("a result of a failed import has no rows")
    {
        ResinImportResult result;
        result.ok    = false;
        result.error = "this file is not a resin profile";
        CHECK(Slic3r::App::build_mapping_rows(result).empty());
    }
}

TEST_CASE("source_as and target_as say what a row does", "[resin_import][report]")
{
    SECTION("a value copied as it is, with its unit on both sides")
    {
        ResinImportResult result;
        MappedField exposure =
            make_field("normalExposureTime", "2.5", "exposure_time", "2.5", MappingStatus::Exact);
        exposure.source_unit = "s";
        exposure.target_unit = "s";
        result.mapping.report.push_back(exposure);

        const std::vector<MappingRow> rows = Slic3r::App::build_mapping_rows(result);
        REQUIRE(rows.size() == 1);
        CHECK(Slic3r::App::source_as(rows[0]) == "normalExposureTime = 2.5 s");
        CHECK(Slic3r::App::target_as(rows[0]) == "exposure_time = 2.5 s");
    }

    SECTION("a converted value shows what the file had next to what is written")
    {
        ResinImportResult result;
        MappedField speed = make_field(
            "normalLayerLiftSpeed",
            "150",
            "lift_speed",
            "2.5",
            MappingStatus::Converted
        );
        speed.source_unit = "mm/min";
        speed.target_unit = "mm/s";
        result.mapping.report.push_back(speed);

        const std::vector<MappingRow> rows = Slic3r::App::build_mapping_rows(result);
        REQUIRE(rows.size() == 1);
        CHECK(Slic3r::App::source_as(rows[0]) == "normalLayerLiftSpeed = 150 mm/min");
        CHECK(Slic3r::App::target_as(rows[0]) == "lift_speed = 2.5 mm/s");
    }

    SECTION("a value with no unit of its own is shown as it is")
    {
        ResinImportResult result;
        result.mapping.report = {
            make_field("bottomLayerCount", "8", "bottom_layer_count", "8", MappingStatus::Exact),
        };

        const std::vector<MappingRow> rows = Slic3r::App::build_mapping_rows(result);
        REQUIRE(rows.size() == 1);
        CHECK(Slic3r::App::source_as(rows[0]) == "bottomLayerCount = 8");
        CHECK(Slic3r::App::target_as(rows[0]) == "bottom_layer_count = 8");
    }

    SECTION("a key the file gives no value for is named alone")
    {
        ResinImportResult result;
        result.mapping.report = {
            make_field("startGcode", "", "", "", MappingStatus::NotApplicable)
        };

        const std::vector<MappingRow> rows = Slic3r::App::build_mapping_rows(result);
        REQUIRE(rows.size() == 1);
        CHECK(Slic3r::App::source_as(rows[0]) == "startGcode");
    }

    SECTION("a row that writes nothing has no target side to show")
    {
        ResinImportResult result;
        MappedField lift =
            make_field("normalLayerLiftHeight", "5", "", "", MappingStatus::NotApplicable);
        lift.source_unit = "mm";
        result.mapping.report.push_back(lift);

        const std::vector<MappingRow> rows = Slic3r::App::build_mapping_rows(result);
        REQUIRE(rows.size() == 1);
        CHECK(Slic3r::App::source_as(rows[0]) == "normalLayerLiftHeight = 5 mm");
        CHECK(Slic3r::App::target_as(rows[0]).empty());
    }
}

TEST_CASE("count_badges counts every kind of row", "[resin_import][report]")
{
    SECTION("a report with rows of every kind")
    {
        ResinImportResult result;
        result.mapping.report = {
            make_field("a", "1", "k", "1", MappingStatus::Exact),
            make_field("b", "1", "k", "1", MappingStatus::Exact),
            make_field("c", "1", "k", "1", MappingStatus::Converted),
            make_field("d", "1", "k", "1", MappingStatus::Approximated),
            make_field("e", "1", "", "", MappingStatus::NotApplicable),
            make_field("f", "1", "", "", MappingStatus::NotApplicable),
            make_field("g", "1", "", "", MappingStatus::NotApplicable),
            make_field("h", "1", "", "", MappingStatus::Unknown),
        };

        const Slic3r::App::BadgeCounts counts =
            Slic3r::App::count_badges(Slic3r::App::build_mapping_rows(result));
        CHECK(counts.exact == 2);
        CHECK(counts.converted == 1);
        CHECK(counts.approximated == 1);
        CHECK(counts.not_applicable == 3);
        CHECK(counts.unknown == 1);
        // Nothing is lost between the table and the line under it: the counts add up.
        CHECK(counts.total() == 8);
    }

    SECTION("an empty report")
    {
        const Slic3r::App::BadgeCounts counts = Slic3r::App::count_badges({});
        CHECK(counts.total() == 0);
    }
}

TEST_CASE("summary_line counts the badges that are there", "[resin_import][report]")
{
    SECTION("only the kinds that occur are named")
    {
        std::vector<MappingRow> rows(3);
        rows[0].badge = MappingBadge::Exact;
        rows[1].badge = MappingBadge::Exact;
        rows[2].badge = MappingBadge::NotApplicable;
        CHECK(Slic3r::App::summary_line(rows) == "2 Exact  \xC2\xB7  1 Not applicable");
    }

    SECTION("no rows means no line, so the dialog hides it")
    {
        CHECK(Slic3r::App::summary_line({}).empty());
    }
}

TEST_CASE("source_summary says what the file is", "[resin_import][report]")
{
    SECTION("file, format and resin")
    {
        ResinImportResult result;
        result.file          = "C:/profiles/chitubox.cfg";
        result.source_format = "chitubox-cfg";
        result.resin_name    = "Grey resin";
        CHECK(
            Slic3r::App::source_summary(result)
            == "chitubox.cfg  \xC2\xB7  chitubox-cfg  \xC2\xB7  Grey resin"
        );
    }

    SECTION("a part the file does not have is left out")
    {
        ResinImportResult result;
        result.file          = "resin.cfg";
        result.source_format = "chitubox-cfg";
        CHECK(Slic3r::App::source_summary(result) == "resin.cfg  \xC2\xB7  chitubox-cfg");
    }

    SECTION("a profile that was typed into a form has no file to name")
    {
        ResinImportResult result;
        result.source_format = "datasheet";
        result.resin_name    = "Grey resin";
        result.resin_vendor  = "Anycubic";
        // The format and the resin are all there is to say, and the line says both of them.
        CHECK(
            Slic3r::App::source_summary(result) == "datasheet  \xC2\xB7  Grey resin  \xC2\xB7  Anycubic"
        );
    }

    SECTION("a file that has not been read yet")
    {
        CHECK(Slic3r::App::source_summary(ResinImportResult{}).empty());
    }
}

TEST_CASE("validate_preset_name refuses only what cannot be saved", "[resin_import][report]")
{
    SECTION("a name that is fine")
    {
        CHECK(Slic3r::App::validate_preset_name("Grey resin").empty());
        // The importer replaces the characters a file name may not carry and makes a taken name
        // unique, so the dialog does not refuse either.
        CHECK(Slic3r::App::validate_preset_name("Grey: resin / 2").empty());
    }

    SECTION("a name that is missing")
    {
        CHECK_FALSE(Slic3r::App::validate_preset_name("").empty());
        CHECK_FALSE(Slic3r::App::validate_preset_name("   ").empty());
        CHECK_FALSE(Slic3r::App::validate_preset_name("\t\n").empty());
    }

    SECTION("a name that is too long")
    {
        CHECK(Slic3r::App::validate_preset_name(std::string(MAX_PRESET_NAME_LENGTH, 'a')).empty());
        CHECK_FALSE(
            Slic3r::App::validate_preset_name(std::string(MAX_PRESET_NAME_LENGTH + 1, 'a')).empty()
        );
    }
}
