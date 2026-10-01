#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <string>
#include <vector>

#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "libslic3r/SLA/IslandDetection.hpp"

using namespace Slic3r;
using Catch::Approx;
using Slic3r::Biz::Algorithms::Scaling::scaled;
using Slic3r::Domain::ExPolygon;
using Slic3r::Domain::ExPolygons;
using Slic3r::Domain::ObjectID;
using Slic3r::Domain::Point;

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

// --- Attributing an island to the model it belongs to (M4.8g) ---

// One model object on one layer, as the slicer hands it over for the attribution.
SLA::ObjectLayer make_object(size_t id, const std::string& name, ExPolygons slices)
{
    return SLA::ObjectLayer{ObjectID{id}, name, std::move(slices)};
}

// The merged layer the slicer sees: the polygons of every object of that layer in one layer.
ExPolygons merged_layer(const std::vector<SLA::ObjectLayer>& objects)
{
    ExPolygons all;
    for (const SLA::ObjectLayer& object : objects) {
        for (const ExPolygon& poly : object.slices)
            all.push_back(poly);
    }
    return Slic3r::Biz::Algorithms::ClipperUtils::union_ex(all);
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

TEST_CASE(
    "IslandDetection: the island of a second cube is put on that cube",
    "[SLA][IslandDetection]"
)
{
    // Two cubes on the plate, seen from the top, and a small block floating above the second one
    // with a gap under it. The merged layers are what the island is detected in, the per object
    // layers are what it is named after.
    const size_t base_id  = 7;
    const size_t tower_id = 9;

    std::vector<ExPolygons> layers;
    std::vector<std::vector<SLA::ObjectLayer>> object_layers;

    for (size_t layer = 0; layer < 6; ++layer) {
        std::vector<SLA::ObjectLayer> objects;
        // The first cube stands through all the layers.
        objects.push_back(make_object(base_id, "base cube", ExPolygons{make_square(10.0, 0.0, 0.0)})
        );
        if (layer < 3) {
            // The second cube, on the other side of the plate.
            objects.push_back(
                make_object(tower_id, "tower cube", ExPolygons{make_square(10.0, 30.0, 0.0)})
            );
        } else if (layer == 4) {
            // The floating block above the second cube, the gap is under it.
            objects.push_back(
                make_object(tower_id, "tower cube", ExPolygons{make_square(4.0, 30.0, 0.0)})
            );
        }
        layers.push_back(merged_layer(objects));
        object_layers.push_back(std::move(objects));
    }

    const std::vector<SLA::IslandHit> hits = SLA::detect_islands(layers, 0.05);

    REQUIRE(hits.size() == 1);
    CHECK(hits[0].layer_index == 4);

    const SLA::IslandOwner owner = SLA::attribute_island(hits[0], object_layers[4]);
    CHECK(owner.object_id == ObjectID{tower_id});
    CHECK(owner.name == "tower cube");
}

TEST_CASE(
    "IslandDetection: an island between two blocks belongs to the one it mostly lies on",
    "[SLA][IslandDetection]"
)
{
    // Two blocks floating above two cubes, touching each other, so the merged layer holds one
    // region that both models have a piece of. The left block covers x -5..5, the right one
    // x -3..13, so the right one holds more of the island while the centroid of the merged region
    // (x 4.35) sits in the left one: the answer can come from neither of those two rules. The
    // blocks clear the cubes below by 2 mm, because a block that overlaps them is not floating at
    // all and the layer below says so.
    const size_t left_id  = 3;
    const size_t right_id = 4;

    std::vector<ExPolygons> layers(2);
    std::vector<std::vector<SLA::ObjectLayer>> object_layers(2);

    for (size_t layer = 0; layer < 2; ++layer) {
        if (layer == 0) {
            // The cubes below, the blocks are not there yet.
            object_layers[layer].push_back(
                make_object(left_id, "left cube", ExPolygons{make_square(10.0, 0.0, 0.0)})
            );
            object_layers[layer].push_back(
                make_object(right_id, "right cube", ExPolygons{make_square(10.0, 20.0, 0.0)})
            );
        } else {
            // The blocks, touching each other, so the layer holds a single region of both.
            object_layers[layer].push_back(
                make_object(left_id, "left cube", ExPolygons{make_square(10.0, 0.0, 12.0)})
            );
            object_layers[layer].push_back(
                make_object(right_id, "right cube", ExPolygons{make_square(16.0, 5.0, 15.0)})
            );
        }
        layers[layer] = merged_layer(object_layers[layer]);
    }

    // One region of both models, so there is one island to name and not one per model.
    REQUIRE(layers[1].size() == 1);

    const std::vector<SLA::IslandHit> hits = SLA::detect_islands(layers, 0.05);

    REQUIRE(hits.size() == 1);
    CHECK(hits[0].layer_index == 1);
    CHECK(hits[0].centroid.x() == Approx(4.35).margin(0.05));

    const SLA::IslandOwner owner = SLA::attribute_island(hits[0], object_layers[1]);
    CHECK(owner.object_id == ObjectID{right_id});
    CHECK(owner.name == "right cube");
}

TEST_CASE(
    "IslandDetection: an island over a support is put on the model the support belongs to",
    "[SLA][IslandDetection]"
)
{
    // Only the support of the second model is anywhere near the island, its body is not. Body and
    // supports of a model come as one entry, so both count as that model.
    std::vector<ExPolygons> layers(2);
    std::vector<std::vector<SLA::ObjectLayer>> object_layers(2);

    layers[0]        = ExPolygons{make_square(10.0, 0.0, 0.0)};
    object_layers[0] = {make_object(1, "with support", ExPolygons{make_square(10.0, 0.0, 0.0)})};

    layers[1]        = ExPolygons{make_square(10.0, 0.0, 0.0), make_square(3.0, 30.0, 0.0)};
    object_layers[1] = {make_object(
        1,
        "with support",
        ExPolygons{make_square(10.0, 0.0, 0.0), make_square(3.0, 30.0, 0.0)}
    )};

    const std::vector<SLA::IslandHit> hits = SLA::detect_islands(layers, 0.05);

    REQUIRE(hits.size() == 1);
    CHECK(hits[0].layer_index == 1);

    const SLA::IslandOwner owner = SLA::attribute_island(hits[0], object_layers[1]);
    CHECK(owner.object_id == ObjectID{1});
    CHECK(owner.name == "with support");
}

TEST_CASE("IslandDetection: an island that no model holds stays unnamed", "[SLA][IslandDetection]")
{
    // Nothing is known about the models of the layer, e.g. the island only sits on the pad,
    // which belongs to no model: the issue then carries no name instead of a wrong one.
    std::vector<ExPolygons> layers(2);
    layers[0] = {};
    layers[1].push_back(make_square(10.0, 0.0, 0.0));

    const std::vector<SLA::IslandHit> hits = SLA::detect_islands(layers, 0.05);

    REQUIRE(hits.size() == 1);

    const SLA::IslandOwner owner = SLA::attribute_island(hits[0], {});
    CHECK(owner.object_id.invalid());
    CHECK(owner.name.empty());
}

TEST_CASE("IslandDetection: the region of an island is kept with it", "[SLA][IslandDetection]")
{
    // The attribution needs the island itself and not only its centroid, so the region travels
    // with the hit.
    std::vector<ExPolygons> layers(2);
    layers[0] = {};
    layers[1].push_back(make_square(10.0, 0.0, 0.0));

    const std::vector<SLA::IslandHit> hits = SLA::detect_islands(layers, 0.05);

    REQUIRE(hits.size() == 1);
    const ExPolygon& region = hits[0].region;
    REQUIRE(region.contour.size() == 5);
    const double area_mm2 = Slic3r::Biz::Algorithms::ExPolygon::area(region)
        * Slic3r::Biz::Algorithms::Scaling::SCALING_FACTOR
        * Slic3r::Biz::Algorithms::Scaling::SCALING_FACTOR;
    CHECK(area_mm2 == Approx(100.0).margin(0.01));
}
