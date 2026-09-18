#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/ResultExport/SLA/SlaArchiveFormat.hpp"

#include <algorithm>

TEST_CASE("SlaArchiveFormatRegistry returns SL1 format", "[SlaArchiveFormat]") {
    auto& registry = Slic3r::Biz::PrintHost::Sla::SlaArchiveFormatRegistry::instance();

    // Register formats (idempotent)
    Slic3r::Biz::PrintHost::Sla::register_sla_archive_formats();

    // Test SL1 format
    auto sl1 = registry.get("SL1");
    REQUIRE(sl1 != nullptr);
    REQUIRE(sl1->name() == "SL1");
    REQUIRE(sl1->file_data_type() == Slic3r::Biz::Slicing::Sla::FileDataType::sl1_png);
    auto exts = sl1->extensions();
    REQUIRE(std::find(exts.begin(), exts.end(), "sl1") != exts.end());
    REQUIRE(std::find(exts.begin(), exts.end(), "sl1s") != exts.end());
    REQUIRE(std::find(exts.begin(), exts.end(), "zip") != exts.end());

    // Test SL1_SVG format
    auto sl1_svg = registry.get("SL1_SVG");
    REQUIRE(sl1_svg != nullptr);
    REQUIRE(sl1_svg->name() == "SL1_SVG");
    REQUIRE(sl1_svg->file_data_type() == Slic3r::Biz::Slicing::Sla::FileDataType::sl1_svg);
    exts = sl1_svg->extensions();
    REQUIRE(std::find(exts.begin(), exts.end(), "sl1svg") != exts.end());

    // Test case-insensitive lookup
    auto sl1_lower = registry.get("sl1");
    REQUIRE(sl1_lower != nullptr);
    REQUIRE(sl1_lower->name() == "SL1");

    // Test unknown format returns nullptr
    auto unknown = registry.get("UNKNOWN");
    REQUIRE(unknown == nullptr);

    // Test find by extension
    auto by_ext = registry.find_by_extension("sl1");
    REQUIRE(by_ext != nullptr);
    REQUIRE(by_ext->name() == "SL1");

    auto by_ext_svg = registry.find_by_extension("sl1svg");
    REQUIRE(by_ext_svg != nullptr);
    REQUIRE(by_ext_svg->name() == "SL1_SVG");

    auto by_unknown_ext = registry.find_by_extension("xyz");
    REQUIRE(by_unknown_ext == nullptr);

    // Test names list
    auto names = registry.names();
    REQUIRE(std::find(names.begin(), names.end(), "sl1") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "sl1_svg") != names.end());
}