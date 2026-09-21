#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>

#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "libslic3r/SLA/LayerStats.hpp"

using namespace Slic3r;
using Slic3r::Domain::ExPolygon;
using Slic3r::Domain::ExPolygons;
using Slic3r::Domain::Point;
using Slic3r::Biz::Algorithms::Scaling::scaled;
using Catch::Approx;

namespace {

// Create a simple square ExPolygon centered at origin with given size (in mm).
ExPolygon make_square(double size_mm, double center_x = 0.0, double center_y = 0.0)
{
    ExPolygon poly;
    double half = size_mm / 2.0;
    poly.contour.points = {
        Point(scaled(center_x - half), scaled(center_y - half)),
        Point(scaled(center_x + half), scaled(center_y - half)),
        Point(scaled(center_x + half), scaled(center_y + half)),
        Point(scaled(center_x - half), scaled(center_y + half)),
        Point(scaled(center_x - half), scaled(center_y - half))
    };
    return poly;
}

// Create a vector of ExPolygons for multiple layers, each with one square.
std::vector<ExPolygons> make_layers_with_squares(const std::vector<double>& sizes_mm)
{
    std::vector<ExPolygons> layers;
    layers.reserve(sizes_mm.size());
    for (double size : sizes_mm) {
        ExPolygons layer;
        layer.push_back(make_square(size));
        layers.push_back(std::move(layer));
    }
    return layers;
}

// Create layers with an empty layer.
std::vector<ExPolygons> make_layers_with_empty(const std::vector<double>& sizes_mm, size_t empty_layer_idx)
{
    std::vector<ExPolygons> layers;
    layers.reserve(sizes_mm.size() + 1);
    for (size_t i = 0; i <= sizes_mm.size(); ++i) {
        ExPolygons layer;
        if (i != empty_layer_idx && i < sizes_mm.size()) {
            layer.push_back(make_square(sizes_mm[i < empty_layer_idx ? i : i - 1]));
        }
        layers.push_back(std::move(layer));
    }
    return layers;
}

} // namespace

TEST_CASE("LayerStats: layer_areas_mm2 single layer square", "[SLA][LayerStats]")
{
    // 10x10 mm square = 100 mm²
    auto layers = make_layers_with_squares({10.0});
    auto areas = SLA::layer_areas_mm2(layers);
    REQUIRE(areas.size() == 1);
    CHECK(areas[0] == Approx(100.0).margin(0.01));
}

TEST_CASE("LayerStats: layer_areas_mm2 multiple layers", "[SLA][LayerStats]")
{
    // 5x5 = 25 mm², 10x10 = 100 mm², 2x2 = 4 mm²
    auto layers = make_layers_with_squares({5.0, 10.0, 2.0});
    auto areas = SLA::layer_areas_mm2(layers);
    REQUIRE(areas.size() == 3);
    CHECK(areas[0] == Approx(25.0).margin(0.01));
    CHECK(areas[1] == Approx(100.0).margin(0.01));
    CHECK(areas[2] == Approx(4.0).margin(0.01));
}

TEST_CASE("LayerStats: layer_areas_mm2 empty layer gives zero", "[SLA][LayerStats]")
{
    // Layer 1 is empty
    auto layers = make_layers_with_empty({10.0, 5.0}, 1); // sizes: 10, empty, 5
    auto areas = SLA::layer_areas_mm2(layers);
    REQUIRE(areas.size() == 3);
    CHECK(areas[0] == Approx(100.0).margin(0.01));
    CHECK(areas[1] == Approx(0.0).margin(0.01));
    CHECK(areas[2] == Approx(25.0).margin(0.01));
}

TEST_CASE("LayerStats: layer_areas_mm2 multiple polygons per layer summed", "[SLA][LayerStats]")
{
    // Layer with two squares: 5x5 (25 mm²) + 3x3 (9 mm²) = 34 mm²
    std::vector<ExPolygons> layers(1);
    layers[0].push_back(make_square(5.0, -10.0, 0.0));
    layers[0].push_back(make_square(3.0, 10.0, 0.0));
    auto areas = SLA::layer_areas_mm2(layers);
    REQUIRE(areas.size() == 1);
    CHECK(areas[0] == Approx(34.0).margin(0.01));
}

TEST_CASE("LayerStats: peel_force_estimate scales with area", "[SLA][LayerStats]")
{
    std::vector<float> areas = {10.0f, 100.0f, 0.0f, 50.0f};
    auto forces = SLA::peel_force_estimate(areas);
    REQUIRE(forces.size() == 4);
    // force = area * PEEL_FORCE_K (0.1 N/mm²)
    CHECK(forces[0] == Approx(1.0f).margin(0.001f));  // 10 * 0.1
    CHECK(forces[1] == Approx(10.0f).margin(0.001f)); // 100 * 0.1
    CHECK(forces[2] == Approx(0.0f).margin(0.001f));  // 0 * 0.1
    CHECK(forces[3] == Approx(5.0f).margin(0.001f));  // 50 * 0.1
}

TEST_CASE("LayerStats: peel_force_estimate empty input gives empty output", "[SLA][LayerStats]")
{
    std::vector<float> areas;
    auto forces = SLA::peel_force_estimate(areas);
    REQUIRE(forces.empty());
}

TEST_CASE("LayerStats: roundtrip areas to forces", "[SLA][LayerStats]")
{
    auto layers = make_layers_with_squares({4.0, 8.0, 12.0}); // 16, 64, 144 mm²
    auto areas = SLA::layer_areas_mm2(layers);
    auto forces = SLA::peel_force_estimate(areas);
    REQUIRE(areas.size() == 3);
    REQUIRE(forces.size() == 3);
    CHECK(forces[0] == Approx(areas[0] * SLA::PEEL_FORCE_K).margin(0.001f));
    CHECK(forces[1] == Approx(areas[1] * SLA::PEEL_FORCE_K).margin(0.001f));
    CHECK(forces[2] == Approx(areas[2] * SLA::PEEL_FORCE_K).margin(0.001f));
}