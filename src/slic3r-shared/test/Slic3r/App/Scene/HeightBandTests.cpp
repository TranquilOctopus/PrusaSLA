#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Slic3r/App/Scene/HeightBand.hpp"

#include <cmath>
#include <limits>

using Catch::Approx;
using Slic3r::App::Scene::height_band_upper_plane_data;
using Slic3r::App::Scene::HeightBand;

TEST_CASE("HeightBand - make clamps and orders the limits", "[HeightBand]")
{
    SECTION("The full range does not clip anything")
    {
        const HeightBand band = HeightBand::make(0., 200., 200.);
        REQUIRE_FALSE(band.active);
        REQUIRE(band.z_min == Approx(0.));
        REQUIRE(band.z_max == Approx(200.));
    }

    SECTION("A non positive printer height disables the band")
    {
        REQUIRE_FALSE(HeightBand::make(10., 20., 0.).active);
        REQUIRE_FALSE(HeightBand::make(10., 20., -5.).active);
        REQUIRE_FALSE(HeightBand::make(10., 20., std::numeric_limits<double>::quiet_NaN()).active);
    }

    SECTION("Limits are clamped into the print height")
    {
        const HeightBand band = HeightBand::make(-10., 500., 200.);
        REQUIRE(band.active);
        REQUIRE(band.z_min == Approx(0.));
        REQUIRE(band.z_max == Approx(200.));
    }

    SECTION("Swapped limits are ordered")
    {
        const HeightBand band = HeightBand::make(120., 30., 200.);
        REQUIRE(band.active);
        REQUIRE(band.z_min == Approx(30.));
        REQUIRE(band.z_max == Approx(120.));
    }

    SECTION("An upper limit below the print height activates the band")
    {
        const HeightBand band = HeightBand::make(0., 50., 200.);
        REQUIRE(band.active);
        REQUIRE_FALSE(band.contains(60.));
        REQUIRE(band.contains(40.));
    }

    SECTION("A lower limit above the bed activates the band")
    {
        const HeightBand band = HeightBand::make(50., 200., 200.);
        REQUIRE(band.active);
        REQUIRE_FALSE(band.contains(10.));
        REQUIRE(band.contains(60.));
    }

    SECTION("Non finite limits fall back to the full range")
    {
        const HeightBand band = HeightBand::make(
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN(),
            200.
        );
        REQUIRE_FALSE(band.active);
    }
}

TEST_CASE("HeightBand - the upper plane keeps everything below the limit", "[HeightBand]")
{
    SECTION("An inactive band clips nothing")
    {
        const Slic3r::Domain::Vec4f plane = height_band_upper_plane_data(HeightBand{});
        REQUIRE(plane.z() == Approx(1.f));
        REQUIRE(plane.w() > 1e30f);
    }

    // The shader discards fragments with dot(world_position, plane) < 0.
    auto keeps = [](const Slic3r::Domain::Vec4f& plane, float z)
    { return float(z) * plane.z() + plane.w() >= 0.f; };

    SECTION("A band with an upper limit clips above it")
    {
        const Slic3r::Domain::Vec4f plane =
            height_band_upper_plane_data(HeightBand::make(10., 50., 200.));
        REQUIRE(plane.x() == Approx(0.f));
        REQUIRE(plane.y() == Approx(0.f));
        REQUIRE(keeps(plane, 0.f));
        REQUIRE(keeps(plane, 50.f));
        REQUIRE_FALSE(keeps(plane, 50.1f));
        REQUIRE_FALSE(keeps(plane, 200.f));
    }
}
