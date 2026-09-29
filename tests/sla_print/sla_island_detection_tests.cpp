#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>

#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "libslic3r/SLA/IslandDetection.hpp"

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

// Create a vector of ExPolygons for a single layer containing one square.
std::vector<ExPolygons> make_single_layer_square(double size_mm, double center_x = 0.0, double center_y = 0.0)
{
    std::vector<ExPolygons> layers(1);
    layers[0].push_back(make_square(size_mm, center_x, center_y));
    return layers;
}

// Create two layers with identical squares (no islands expected).
std::vector<ExPolygons> make_stacked_squares(double size_mm)
{
    std::vector<ExPolygons> layers(2);
    layers[0].push_back(make_square(size_mm));
    layers[1].push_back(make_square(size_mm));
    return layers;
}

// Create layers where layer 1 has a square that doesn't exist in layer 0 (island expected).
std::vector<ExPolygons> make_square_appears_in_layer1(double size_mm)
{
    std::vector<ExPolygons> layers(2);
    layers[0] = {}; // Empty first layer
    layers[1].push_back(make_square(size_mm));
    return layers;
}

// Create layers where layer 1 has a square shifted so part overhangs.
std::vector<ExPolygons> make_shifted_square(double size_mm, double shift_mm)
{
    std::vector<ExPolygons> layers(2);
    layers[0].push_back(make_square(size_mm, 0.0, 0.0));
    layers[1].push_back(make_square(size_mm, shift_mm, 0.0));
    return layers;
}

// Create layers with a widening square stack (each layer slightly larger, same center).
// No islands expected since each layer overlaps the previous.
std::vector<ExPolygons> make_widening_stack(int num_layers, double base_size_mm, double growth_per_layer_mm)
{
    std::vector<ExPolygons> layers(num_layers);
    for (int i = 0; i < num_layers; ++i) {
        double size = base_size_mm + i * growth_per_layer_mm;
        layers[i].push_back(make_square(size));
    }
    return layers;
}

// Create layers with a square on layers 0-2 and a separate square starting on layer 3.
std::vector<ExPolygons> make_separate_square_on_layer3(double size_mm)
{
    std::vector<ExPolygons> layers(4);
    // Base square at origin on layers 0-2
    for (int i = 0; i < 3; ++i) {
        layers[i].push_back(make_square(size_mm, 0.0, 0.0));
    }
    // Separate square at (50, 50) starting on layer 3
    layers[3].push_back(make_square(size_mm, 50.0, 50.0));
    return layers;
}

// Create layers with a square on layers 0-2, and on layer 3 the same square plus a larger overlapping square.
std::vector<ExPolygons> make_overlapping_square_on_layer3(double size_mm)
{
    std::vector<ExPolygons> layers(4);
    // Base square at origin on layers 0-2
    for (int i = 0; i < 3; ++i) {
        layers[i].push_back(make_square(size_mm, 0.0, 0.0));
    }
    // On layer 3: same square + larger overlapping square (same center)
    layers[3].push_back(make_square(size_mm, 0.0, 0.0));
    layers[3].push_back(make_square(size_mm * 2.0, 0.0, 0.0));
    return layers;
}

// Create layers with a small speck below min_area.
std::vector<ExPolygons> make_small_speck(double speck_size_mm, double base_size_mm)
{
    std::vector<ExPolygons> layers(2);
    layers[0].push_back(make_square(base_size_mm));
    layers[1].push_back(make_square(base_size_mm));
    // Add a small square on layer 1 that's not on layer 0
    layers[1].push_back(make_square(speck_size_mm, 10.0, 10.0));
    return layers;
}

} // namespace

TEST_CASE("IslandDetection: single layer produces no islands", "[SLA][IslandDetection]")
{
    auto layers = make_single_layer_square(10.0);
    auto hits = SLA::detect_islands(layers, 0.05);
    REQUIRE(hits.empty());
}

TEST_CASE("IslandDetection: identical stacked squares produce no islands", "[SLA][IslandDetection]")
{
    auto layers = make_stacked_squares(10.0);
    auto hits = SLA::detect_islands(layers, 0.05);
    REQUIRE(hits.empty());
}

TEST_CASE("IslandDetection: square appearing only in layer 1 is detected as island", "[SLA][IslandDetection]")
{
    auto layers = make_square_appears_in_layer1(10.0); // 10x10 = 100 mm²
    auto hits = SLA::detect_islands(layers, 0.05);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].layer_index == 1);
    CHECK(hits[0].area_mm2 == Approx(100.0).margin(0.01));
    CHECK(hits[0].centroid.x() == Approx(0.0).margin(0.01));
    CHECK(hits[0].centroid.y() == Approx(0.0).margin(0.01));
}

TEST_CASE("IslandDetection: shifted square with overlap is NOT an island", "[SLA][IslandDetection]")
{
    // 10mm square shifted by 6mm -> 4mm overlap, but still overlaps so NOT an island
    auto layers = make_shifted_square(10.0, 6.0);
    auto hits = SLA::detect_islands(layers, 0.05);
    REQUIRE(hits.empty());
}

TEST_CASE("IslandDetection: shifted square with small overlap is NOT an island", "[SLA][IslandDetection]")
{
    // 10mm square shifted by 1mm -> 9mm overlap, still overlaps so NOT an island
    auto layers = make_shifted_square(10.0, 1.0);
    auto hits = SLA::detect_islands(layers, 20.0);
    REQUIRE(hits.empty());
}

TEST_CASE("IslandDetection: widening stack (cone) produces no islands", "[SLA][IslandDetection]")
{
    // 5 layers, each 1mm larger than previous, same center -> no islands
    auto layers = make_widening_stack(5, 10.0, 1.0);
    auto hits = SLA::detect_islands(layers, 0.05);
    REQUIRE(hits.empty());
}

TEST_CASE("IslandDetection: separate square starting on layer 3 is detected as island", "[SLA][IslandDetection]")
{
    // Base square on layers 0-2, separate square at (50,50) starts on layer 3
    auto layers = make_separate_square_on_layer3(10.0); // 10x10 = 100 mm²
    auto hits = SLA::detect_islands(layers, 0.05);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].layer_index == 3);
    CHECK(hits[0].area_mm2 == Approx(100.0).margin(0.01));
    CHECK(hits[0].centroid.x() == Approx(50.0).margin(0.01));
    CHECK(hits[0].centroid.y() == Approx(50.0).margin(0.01));
}

TEST_CASE("IslandDetection: overlapping larger square on layer 3 produces no islands", "[SLA][IslandDetection]")
{
    // Base square on layers 0-2, layer 3 has same square + larger overlapping square
    auto layers = make_overlapping_square_on_layer3(10.0);
    auto hits = SLA::detect_islands(layers, 0.05);
    REQUIRE(hits.empty());
}

TEST_CASE("IslandDetection: small speck below min_area is filtered", "[SLA][IslandDetection]")
{
    // Base 10x10 square on both layers, plus a 0.1x0.1 = 0.01 mm² speck on layer 1 only
    // min_area = 0.05 mm², so speck should be filtered
    auto layers = make_small_speck(0.1, 10.0);
    auto hits = SLA::detect_islands(layers, 0.05);
    REQUIRE(hits.empty());
}

TEST_CASE("IslandDetection: small speck above min_area is detected", "[SLA][IslandDetection]")
{
    // Base 10x10 square on both layers, plus a 0.3x0.3 = 0.09 mm² speck on layer 1 only
    // min_area = 0.05 mm², so speck should be detected
    auto layers = make_small_speck(0.3, 10.0);
    auto hits = SLA::detect_islands(layers, 0.05);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].layer_index == 1);
    CHECK(hits[0].area_mm2 == Approx(0.09).margin(0.01));
    CHECK(hits[0].centroid.x() == Approx(10.0).margin(0.01));
    CHECK(hits[0].centroid.y() == Approx(10.0).margin(0.01));
}

TEST_CASE("IslandDetection: multiple islands in same layer all reported", "[SLA][IslandDetection]")
{
    std::vector<ExPolygons> layers(2);
    layers[0] = {}; // Empty first layer
    layers[1].push_back(make_square(5.0, -10.0, 0.0)); // 25 mm² at x=-10
    layers[1].push_back(make_square(5.0, 10.0, 0.0));  // 25 mm² at x=10
    layers[1].push_back(make_square(5.0, 0.0, 10.0));  // 25 mm² at y=10

    auto hits = SLA::detect_islands(layers, 0.05);
    REQUIRE(hits.size() == 3);
    // Check all three are reported (order not guaranteed)
    std::vector<double> areas;
    for (const auto& h : hits) areas.push_back(h.area_mm2);
    std::sort(areas.begin(), areas.end());
    CHECK(areas[0] == Approx(25.0).margin(0.01));
    CHECK(areas[1] == Approx(25.0).margin(0.01));
    CHECK(areas[2] == Approx(25.0).margin(0.01));
}