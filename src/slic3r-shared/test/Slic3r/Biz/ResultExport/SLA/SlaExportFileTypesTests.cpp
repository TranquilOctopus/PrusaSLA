#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/PrintHost/PrintHostJobData.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SlaExportFileTypes.hpp"

#include <algorithm>

using namespace Slic3r::Biz::PrintHost::Sla;

// The SLA export dialog used to offer only .sl1 and .sl1s, and the name came from the filename
// template, which defaults to .gcode.
TEST_CASE("SLA export defaults to the printer's file type", "[export][sla][file-types]")
{
    CHECK(sla_default_export_extension("pm5") == ".pm5");
    CHECK(sla_default_export_extension("goo") == ".goo");
    CHECK(sla_default_export_extension("pwmx") == ".pwmx");
    // Archive formats are matched regardless of case.
    CHECK(sla_default_export_extension("SL1") == ".sl1");
    // A .gcode filename template does not leak into an SLA export.
    CHECK(sla_default_export_extension("pm5", ".gcode") == ".pm5");
    // The template still picks between the extensions of the printer's own format.
    CHECK(sla_default_export_extension("SL1", ".sl1s") == ".sl1s");
    // An unknown archive format has no default.
    CHECK(sla_default_export_extension("nonsense").empty());
}

TEST_CASE("SLA export offers every format, the printer's first", "[export][sla][file-types]")
{
    const auto types = sla_export_file_types("pm5");
    REQUIRE_FALSE(types.empty());
    CHECK(types.front().extension == "pm5");
    CHECK(types.front().matches_sliced_format);

    const auto has = [&types](const std::string& ext) {
        return std::ranges::any_of(types, [&](const auto& type) { return type.extension == ext; });
    };
    CHECK(has("goo"));
    CHECK(has("sl1"));
    CHECK(has("pwmx"));
    // "zip" is only a reading alias for SL1.
    CHECK_FALSE(has("zip"));

    // Only the printer's format can hold the sliced layers.
    for (const auto& type : types)
        CHECK(type.matches_sliced_format == (type.extension == "pm5"));
}

TEST_CASE("SLA export extensions are matched against the sliced format", "[export][sla][file-types]")
{
    CHECK(sla_extension_matches_format(".pm5", "pm5"));
    CHECK(sla_extension_matches_format("PM5", "pm5"));
    CHECK_FALSE(sla_extension_matches_format(".goo", "pm5"));
    // pwmo, pwmx and pwms are one format with one encoder.
    CHECK(sla_extension_matches_format(".pwms", "pwmx"));
    CHECK_FALSE(sla_extension_matches_format(".gcode", "pm5"));
}

TEST_CASE("SLA archive extensions map to an export format", "[export][sla][file-types]")
{
    using Slic3r::Biz::PrintHost::PrintHostExportFormat;
    using Slic3r::Biz::PrintHost::get_export_format_from_extension;
    CHECK(get_export_format_from_extension(".pm5") == PrintHostExportFormat::SlaArchive);
    CHECK(get_export_format_from_extension(".goo") == PrintHostExportFormat::SlaArchive);
    CHECK(get_export_format_from_extension(".sl1") == PrintHostExportFormat::Sl1);
    CHECK(get_export_format_from_extension(".gcode") == PrintHostExportFormat::GCode);
}
