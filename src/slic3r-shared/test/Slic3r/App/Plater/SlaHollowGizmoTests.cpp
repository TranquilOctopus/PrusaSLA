#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Plater/SlaHollowGizmo.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <optional>

using Slic3r::Domain::Vec3d;

namespace Slic3r::App::Plater {

struct SlaHollowConfig
{
    bool enabled = false;
    double min_thickness = 3.0;
    double quality = 0.5;
    double closing_distance = 2.0;

    bool operator==(const SlaHollowConfig& other) const
    {
        return enabled == other.enabled
            && min_thickness == other.min_thickness
            && quality == other.quality
            && closing_distance == other.closing_distance;
    }
};

inline SlaHollowConfig clamp_config(const SlaHollowConfig& config)
{
    SlaHollowConfig result = config;
    result.min_thickness = std::clamp(result.min_thickness, 1.0, 10.0);
    result.quality = std::clamp(result.quality, 0.0, 1.0);
    result.closing_distance = std::clamp(result.closing_distance, 0.0, 10.0);
    return result;
}

inline bool config_requires_reslice(const SlaHollowConfig& old_config, const SlaHollowConfig& new_config)
{
    return old_config.enabled != new_config.enabled
        || old_config.min_thickness != new_config.min_thickness
        || old_config.quality != new_config.quality
        || old_config.closing_distance != new_config.closing_distance;
}

} // namespace Slic3r::App::Plater

TEST_CASE("SlaHollowGizmo - config clamping", "[SlaHollowGizmo][config]")
{
    using Slic3r::App::Plater::SlaHollowConfig;
    using Slic3r::App::Plater::clamp_config;

    SECTION("Values within bounds remain unchanged")
    {
        SlaHollowConfig config{true, 3.0, 0.5, 2.0};
        auto clamped = clamp_config(config);
        REQUIRE(clamped == config);
    }

    SECTION("Min thickness below minimum is clamped to 1.0")
    {
        SlaHollowConfig config{true, 0.5, 0.5, 2.0};
        auto clamped = clamp_config(config);
        REQUIRE(clamped.min_thickness == 1.0);
        REQUIRE(clamped.enabled == true);
        REQUIRE(clamped.quality == 0.5);
        REQUIRE(clamped.closing_distance == 2.0);
    }

    SECTION("Min thickness above maximum is clamped to 10.0")
    {
        SlaHollowConfig config{true, 15.0, 0.5, 2.0};
        auto clamped = clamp_config(config);
        REQUIRE(clamped.min_thickness == 10.0);
    }

    SECTION("Quality below 0 is clamped to 0")
    {
        SlaHollowConfig config{true, 3.0, -0.1, 2.0};
        auto clamped = clamp_config(config);
        REQUIRE(clamped.quality == 0.0);
    }

    SECTION("Quality above 1 is clamped to 1")
    {
        SlaHollowConfig config{true, 3.0, 1.5, 2.0};
        auto clamped = clamp_config(config);
        REQUIRE(clamped.quality == 1.0);
    }

    SECTION("Closing distance below 0 is clamped to 0")
    {
        SlaHollowConfig config{true, 3.0, 0.5, -1.0};
        auto clamped = clamp_config(config);
        REQUIRE(clamped.closing_distance == 0.0);
    }

    SECTION("Closing distance above 10 is clamped to 10")
    {
        SlaHollowConfig config{true, 3.0, 0.5, 15.0};
        auto clamped = clamp_config(config);
        REQUIRE(clamped.closing_distance == 10.0);
    }

    SECTION("All values clamped simultaneously")
    {
        SlaHollowConfig config{true, -5.0, 2.0, 20.0};
        auto clamped = clamp_config(config);
        REQUIRE(clamped.min_thickness == 1.0);
        REQUIRE(clamped.quality == 1.0);
        REQUIRE(clamped.closing_distance == 10.0);
    }

    SECTION("Disabled config still clamps values")
    {
        SlaHollowConfig config{false, 0.0, -1.0, 100.0};
        auto clamped = clamp_config(config);
        REQUIRE(clamped.enabled == false);
        REQUIRE(clamped.min_thickness == 1.0);
        REQUIRE(clamped.quality == 0.0);
        REQUIRE(clamped.closing_distance == 10.0);
    }
}

TEST_CASE("SlaHollowGizmo - reslice detection", "[SlaHollowGizmo][reslice]")
{
    using Slic3r::App::Plater::SlaHollowConfig;
    using Slic3r::App::Plater::config_requires_reslice;

    SECTION("Same config requires no reslice")
    {
        SlaHollowConfig old_config{true, 3.0, 0.5, 2.0};
        SlaHollowConfig new_config{true, 3.0, 0.5, 2.0};
        REQUIRE(config_requires_reslice(old_config, new_config) == false);
    }

    SECTION("Enable change requires reslice")
    {
        SlaHollowConfig old_config{false, 3.0, 0.5, 2.0};
        SlaHollowConfig new_config{true, 3.0, 0.5, 2.0};
        REQUIRE(config_requires_reslice(old_config, new_config) == true);
    }

    SECTION("Min thickness change requires reslice")
    {
        SlaHollowConfig old_config{true, 3.0, 0.5, 2.0};
        SlaHollowConfig new_config{true, 4.0, 0.5, 2.0};
        REQUIRE(config_requires_reslice(old_config, new_config) == true);
    }

    SECTION("Quality change requires reslice")
    {
        SlaHollowConfig old_config{true, 3.0, 0.5, 2.0};
        SlaHollowConfig new_config{true, 3.0, 0.7, 2.0};
        REQUIRE(config_requires_reslice(old_config, new_config) == true);
    }

    SECTION("Closing distance change requires reslice")
    {
        SlaHollowConfig old_config{true, 3.0, 0.5, 2.0};
        SlaHollowConfig new_config{true, 3.0, 0.5, 3.0};
        REQUIRE(config_requires_reslice(old_config, new_config) == true);
    }

    SECTION("Multiple changes require reslice")
    {
        SlaHollowConfig old_config{false, 2.0, 0.3, 1.0};
        SlaHollowConfig new_config{true, 4.0, 0.8, 5.0};
        REQUIRE(config_requires_reslice(old_config, new_config) == true);
    }

    SECTION("Disabled to disabled with same params requires no reslice")
    {
        SlaHollowConfig old_config{false, 3.0, 0.5, 2.0};
        SlaHollowConfig new_config{false, 3.0, 0.5, 2.0};
        REQUIRE(config_requires_reslice(old_config, new_config) == false);
    }
}