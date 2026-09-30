#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "libslic3r/SLA/CavityDetection.hpp"

using namespace Slic3r;
using Catch::Approx;
using Slic3r::Biz::Algorithms::Scaling::scaled;
using Slic3r::Domain::ExPolygon;
using Slic3r::Domain::ExPolygons;
using Slic3r::Domain::Point;
using Slic3r::Domain::Polygon;

namespace {

// A rectangle ExPolygon centered at the given point, given in mm.
ExPolygon make_rect(double size_x_mm, double size_y_mm, double cx = 0.0, double cy = 0.0)
{
    const double hx = size_x_mm / 2.0;
    const double hy = size_y_mm / 2.0;
    ExPolygon poly;
    poly.contour.points = {
        Point(scaled(cx - hx), scaled(cy - hy)),
        Point(scaled(cx + hx), scaled(cy - hy)),
        Point(scaled(cx + hx), scaled(cy + hy)),
        Point(scaled(cx - hx), scaled(cy + hy)),
        Point(scaled(cx - hx), scaled(cy - hy))
    };
    return poly;
}

// A square ExPolygon centered at the given point, given in mm.
ExPolygon make_square(double size_mm, double cx = 0.0, double cy = 0.0)
{
    return make_rect(size_mm, size_mm, cx, cy);
}

// A rectangle wound the other way round than a contour: holes wind clockwise.
Polygon make_rect_hole(double size_x_mm, double size_y_mm, double cx = 0.0, double cy = 0.0)
{
    ExPolygon rect = make_rect(size_x_mm, size_y_mm, cx, cy);
    std::reverse(rect.contour.points.begin(), rect.contour.points.end());
    return std::move(rect.contour);
}

Polygon make_square_hole(double size_mm, double cx = 0.0, double cy = 0.0)
{
    return make_rect_hole(size_mm, size_mm, cx, cy);
}

// A square ring: solid between the outer and the inner square, the inner one a hole.
ExPolygon make_ring(double outer_mm, double inner_mm, double cx = 0.0, double cy = 0.0)
{
    ExPolygon poly = make_square(outer_mm, cx, cy);
    poly.holes.push_back(make_square_hole(inner_mm, cx, cy));
    return poly;
}

// A circle approximated by a polygon, clockwise for a hole.
ExPolygon make_circle(double radius_mm, double cx = 0.0, double cy = 0.0, bool clockwise = false)
{
    constexpr int segments = 64;
    Polygon poly;
    poly.points.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        const double angle = 2.0 * M_PI * i / segments;
        poly.points.push_back(Point(
            scaled(cx + radius_mm * std::cos(angle)),
            scaled(cy + radius_mm * std::sin(angle))
        ));
    }
    if (clockwise)
        std::reverse(poly.points.begin(), poly.points.end());
    return ExPolygon(std::move(poly));
}

// A square with a circular hole in the middle, which is what a drain hole or a vent hole looks
// like in the layers it goes through.
ExPolygon make_square_with_round_hole(double size_mm,
                                      double hole_radius_mm,
                                      double cx = 0.0,
                                      double cy = 0.0)
{
    ExPolygon poly = make_square(size_mm, cx, cy);
    poly.holes.push_back(make_circle(hole_radius_mm, cx, cy, true).contour);
    return poly;
}

std::vector<float> uniform_thicknesses(size_t layer_count, float thickness_mm = 1.f)
{
    return std::vector<float>(layer_count, thickness_mm);
}

// An upside down cup, the textbook suction cup: a roof over an open pocket, the whole thing held
// off the plate by two uprights (the way supports hold an overhang) so that the pocket opens onto
// the resin. The pocket is the 8x8 mm hole of the wall layers 1 to 3.
constexpr size_t cup_first_layer = 1;
constexpr size_t cup_last_layer  = 3;
constexpr double cup_opening_mm2 = 64.0; // the 8x8 mm pocket, nothing under it
constexpr double cup_volume_mm3  = 192.0; // 64 mm2 of pocket times 3 layers of 1 mm

std::vector<ExPolygons> make_cup(double cx = 0.0, double cy = 0.0, double vent_radius_mm = 0.0)
{
    std::vector<ExPolygons> layers(cup_last_layer + 3);
    // The uprights under the left and the right wall of the cup, the layer it starts on.
    layers[0].push_back(make_rect(2.0, 12.0, cx + 5.0, cy));
    layers[0].push_back(make_rect(2.0, 12.0, cx - 5.0, cy));
    for (size_t i = cup_first_layer; i <= cup_last_layer; ++i)
        layers[i].push_back(make_ring(12.0, 8.0, cx, cy)); // the walls, the pocket is the hole
    for (size_t i = cup_last_layer + 1; i < layers.size(); ++i) {
        if (vent_radius_mm > 0.0)
            layers[i].push_back(make_square_with_round_hole(12.0, vent_radius_mm, cx, cy));
        else
            layers[i].push_back(make_square(12.0, cx, cy)); // the roof
    }
    return layers;
}

// A cup like make_cup() with a post standing in the middle of the pocket, so the pocket is a ring
// around it and not a square: the solid of the layer standing inside the void has to be taken off.
std::vector<ExPolygons> make_cup_with_post_in_the_pocket(double cx = 0.0, double cy = 0.0)
{
    std::vector<ExPolygons> layers = make_cup(cx, cy);
    for (size_t i = 0; i <= cup_last_layer; ++i)
        layers[i].push_back(make_square(4.0, cx, cy));
    return layers;
}

// A cup like make_cup() but small: its pocket is pocket_mm square, so its opening decides whether
// it is worth reporting. A 0.9 mm pocket is 0.81 mm2, under the 1 mm2 threshold, a 1.4 mm pocket
// is 1.96 mm2, over it.
std::vector<ExPolygons> make_small_cup(double pocket_mm)
{
    std::vector<ExPolygons> layers(6);
    for (size_t i = 0; i < 3; ++i)
        layers[i].push_back(make_ring(4.0, pocket_mm));
    for (size_t i = 3; i < layers.size(); ++i)
        layers[i].push_back(make_square(4.0));
    return layers;
}

// A hollow cube: 10x10 mm overall, a 6x6 mm cavity from layer 2 to layer 7, closed at both ends.
constexpr size_t shell_wall_layers   = 2;
constexpr size_t shell_cavity_layers = 6;
constexpr double shell_cavity_mm3    = 216.0; // 36 mm2 of cavity times 6 layers of 1 mm

std::vector<ExPolygons> make_hollow_cube(bool with_drain = false)
{
    const size_t top_wall = shell_wall_layers + shell_cavity_layers;
    std::vector<ExPolygons> layers(top_wall + shell_wall_layers);
    for (size_t i = 0; i < shell_wall_layers; ++i) {
        // The bottom wall, with a drain hole through it when there is one.
        if (with_drain)
            layers[i].push_back(make_square_with_round_hole(10.0, 0.5));
        else
            layers[i].push_back(make_square(10.0));
    }
    for (size_t i = shell_wall_layers; i < shell_wall_layers + shell_cavity_layers; ++i)
        layers[i].push_back(make_ring(10.0, 6.0));
    for (size_t i = shell_wall_layers + shell_cavity_layers; i < layers.size(); ++i)
        layers[i].push_back(make_square(10.0)); // the top wall
    return layers;
}

} // namespace

TEST_CASE("CavityDetection: an upside down cup is a cup", "[SLA][CavityDetection]")
{
    const std::vector<ExPolygons> layers = make_cup();

    const SLA::CavityAnalysis result =
        SLA::detect_cavities(layers, uniform_thicknesses(layers.size()));

    REQUIRE(result.cups.size() == 1);
    const SLA::CupHit& cup = result.cups.front();
    CHECK(cup.first_layer == cup_first_layer);
    CHECK(cup.last_layer == cup_last_layer);
    CHECK(cup.opening_area_mm2 == Approx(cup_opening_mm2).margin(0.01));
    CHECK(cup.volume_mm3 == Approx(cup_volume_mm3).margin(0.01));
    CHECK(cup.opening_centroid.x() == Approx(0.0).margin(0.01));
    CHECK(cup.opening_centroid.y() == Approx(0.0).margin(0.01));
    CHECK(result.trapped_resin.empty());
}

TEST_CASE("CavityDetection: a cup with a vent hole in the roof is not a cup",
          "[SLA][CavityDetection]")
{
    // A 1 mm vent hole through the roof, in the middle of the pocket.
    const std::vector<ExPolygons> layers = make_cup(0.0, 0.0, 0.5);

    const SLA::CavityAnalysis result =
        SLA::detect_cavities(layers, uniform_thicknesses(layers.size()));

    // The vent reaches the outside, so the pocket is neither sealed against the film nor trapped.
    CHECK(result.cups.empty());
    CHECK(result.trapped_resin.empty());
}

TEST_CASE("CavityDetection: a hollow cube without a drain hole holds trapped resin",
          "[SLA][CavityDetection]")
{
    const std::vector<ExPolygons> layers = make_hollow_cube();

    const SLA::CavityAnalysis result =
        SLA::detect_cavities(layers, uniform_thicknesses(layers.size()));

    // The cavity is closed at both ends, so it is not a cup that seals against the film but resin
    // that never gets out.
    CHECK(result.cups.empty());
    REQUIRE(result.trapped_resin.size() == 1);
    const SLA::TrappedResinHit& trapped = result.trapped_resin.front();
    CHECK(trapped.first_layer == shell_wall_layers);
    CHECK(trapped.last_layer == shell_wall_layers + shell_cavity_layers - 1);
    CHECK(trapped.volume_mm3 == Approx(shell_cavity_mm3).margin(0.01));
    CHECK(trapped.centroid.x() == Approx(0.0).margin(0.01));
    CHECK(trapped.centroid.y() == Approx(0.0).margin(0.01));
}

TEST_CASE("CavityDetection: a hollow cube with a drain hole holds no trapped resin",
          "[SLA][CavityDetection]")
{
    const std::vector<ExPolygons> layers = make_hollow_cube(true);

    const SLA::CavityAnalysis result =
        SLA::detect_cavities(layers, uniform_thicknesses(layers.size()));

    // The drain hole connects the cavity to the vat. It is also narrower than the 1 mm2 threshold,
    // so the cavity is not reported as a cup either.
    CHECK(result.trapped_resin.empty());
    CHECK(result.cups.empty());
}

TEST_CASE("CavityDetection: a solid cube has no cups and no trapped resin",
          "[SLA][CavityDetection]")
{
    std::vector<ExPolygons> layers(10);
    for (size_t i = 0; i < layers.size(); ++i)
        layers[i].push_back(make_square(10.0));

    const SLA::CavityAnalysis result =
        SLA::detect_cavities(layers, uniform_thicknesses(layers.size()));

    CHECK(result.cups.empty());
    CHECK(result.trapped_resin.empty());
}

TEST_CASE("CavityDetection: three separate cups give three cups", "[SLA][CavityDetection]")
{
    const std::vector<ExPolygons> left   = make_cup(-40.0, 0.0);
    const std::vector<ExPolygons> middle = make_cup(0.0, 0.0);
    const std::vector<ExPolygons> right  = make_cup(40.0, 0.0);

    std::vector<ExPolygons> layers(left.size());
    for (size_t i = 0; i < layers.size(); ++i) {
        layers[i] = left[i];
        layers[i].insert(layers[i].end(), middle[i].begin(), middle[i].end());
        layers[i].insert(layers[i].end(), right[i].begin(), right[i].end());
    }

    const SLA::CavityAnalysis result =
        SLA::detect_cavities(layers, uniform_thicknesses(layers.size()));

    REQUIRE(result.cups.size() == 3);
    CHECK(result.trapped_resin.empty());
    std::vector<double> centres;
    for (const SLA::CupHit& cup : result.cups) {
        CHECK(cup.first_layer == cup_first_layer);
        CHECK(cup.last_layer == cup_last_layer);
        CHECK(cup.opening_area_mm2 == Approx(cup_opening_mm2).margin(0.01));
        centres.push_back(cup.opening_centroid.x());
    }
    std::sort(centres.begin(), centres.end());
    CHECK(centres[0] == Approx(-40.0).margin(0.01));
    CHECK(centres[1] == Approx(0.0).margin(0.01));
    CHECK(centres[2] == Approx(40.0).margin(0.01));
}

TEST_CASE("CavityDetection: a cup with an opening under 1 mm2 is ignored", "[SLA][CavityDetection]")
{
    const std::vector<ExPolygons> tiny = make_small_cup(0.9);

    const SLA::CavityAnalysis with_default =
        SLA::detect_cavities(tiny, uniform_thicknesses(tiny.size()));
    CHECK(with_default.cups.empty());

    // With the threshold lowered it is a cup like any other.
    SLA::CavityDetectionOptions opts;
    opts.min_cup_opening_mm2 = 0.5;
    const SLA::CavityAnalysis with_low_threshold =
        SLA::detect_cavities(tiny, uniform_thicknesses(tiny.size()), opts);
    REQUIRE(with_low_threshold.cups.size() == 1);
    CHECK(with_low_threshold.cups.front().opening_area_mm2 == Approx(0.81).margin(0.01));

    // A 1.4 mm pocket is 1.96 mm2, over the threshold, so it is reported as it is.
    const std::vector<ExPolygons> small = make_small_cup(1.4);
    const SLA::CavityAnalysis of_small =
        SLA::detect_cavities(small, uniform_thicknesses(small.size()));
    REQUIRE(of_small.cups.size() == 1);
    CHECK(of_small.cups.front().opening_area_mm2 == Approx(1.96).margin(0.01));
}

TEST_CASE("CavityDetection: the layer thickness sets the volume", "[SLA][CavityDetection]")
{
    const std::vector<ExPolygons> layers = make_cup();

    const SLA::CavityAnalysis result =
        SLA::detect_cavities(layers, uniform_thicknesses(layers.size(), 0.05f));

    REQUIRE(result.cups.size() == 1);
    // 64 mm2 of pocket times 3 layers of 0.05 mm. The opening does not depend on the thickness.
    CHECK(result.cups.front().volume_mm3 == Approx(9.6).margin(0.01));
    CHECK(result.cups.front().opening_area_mm2 == Approx(cup_opening_mm2).margin(0.01));
}

TEST_CASE("CavityDetection: a post inside the pocket is not counted as pocket",
          "[SLA][CavityDetection]")
{
    const std::vector<ExPolygons> layers = make_cup_with_post_in_the_pocket();

    const SLA::CavityAnalysis result =
        SLA::detect_cavities(layers, uniform_thicknesses(layers.size()));

    // The pocket is the ring around the 4x4 mm post: 64 - 16 = 48 mm2, not the full 8x8 mm hole.
    REQUIRE(result.cups.size() == 1);
    CHECK(result.cups.front().opening_area_mm2 == Approx(48.0).margin(0.01));
    CHECK(result.cups.front().volume_mm3 == Approx(144.0).margin(0.01));
}

TEST_CASE("CavityDetection: a pocket that vents above is reported as neither",
          "[SLA][CavityDetection]")
{
    // Walls with no roof: the hole of the wall layers opens into the outside above them.
    std::vector<ExPolygons> layers(3);
    for (size_t i = 0; i < layers.size(); ++i)
        layers[i].push_back(make_ring(10.0, 6.0));

    const SLA::CavityAnalysis result =
        SLA::detect_cavities(layers, uniform_thicknesses(layers.size()));

    CHECK(result.cups.empty());
    CHECK(result.trapped_resin.empty());
}

TEST_CASE("CavityDetection: two cavities that merge into one are counted once",
          "[SLA][CavityDetection]")
{
    // Two cavities side by side under one roof: they are two holes in the first cavity layer and
    // one hole in the second, where they have joined.
    std::vector<ExPolygons> layers(4);
    layers[0].push_back(make_square(20.0));
    ExPolygon split = make_square(20.0);
    split.holes.push_back(make_square_hole(4.0, -5.0, 0.0));
    split.holes.push_back(make_square_hole(4.0, 5.0, 0.0));
    layers[1].push_back(std::move(split));
    ExPolygon joined = make_square(20.0);
    joined.holes.push_back(make_rect_hole(14.0, 4.0));
    layers[2].push_back(std::move(joined));
    layers[3].push_back(make_square(20.0)); // the roof closes both

    const SLA::CavityAnalysis result =
        SLA::detect_cavities(layers, uniform_thicknesses(layers.size()));

    REQUIRE(result.trapped_resin.size() == 1);
    CHECK(result.trapped_resin.front().first_layer == 1);
    CHECK(result.trapped_resin.front().last_layer == 2);
    // 16 + 16 mm2 of the two cavities and the 56 mm2 they are one on the layer above.
    CHECK(result.trapped_resin.front().volume_mm3 == Approx(88.0).margin(0.01));
}

TEST_CASE("CavityDetection: a cavity that widens upward is one cavity", "[SLA][CavityDetection]")
{
    // A stepped cavity: the walls step inwards going up, so the void grows layer by layer. It has
    // to be counted once, with the area of every layer.
    std::vector<ExPolygons> layers(6);
    layers[0].push_back(make_square(20.0));
    layers[1].push_back(make_ring(20.0, 10.0)); // a 10x10 mm cavity
    layers[2].push_back(make_ring(20.0, 10.0));
    layers[3].push_back(make_ring(20.0, 14.0)); // the walls step in, the cavity widens
    layers[4].push_back(make_ring(20.0, 14.0));
    layers[5].push_back(make_square(20.0)); // the roof

    const SLA::CavityAnalysis result =
        SLA::detect_cavities(layers, uniform_thicknesses(layers.size()));

    REQUIRE(result.trapped_resin.size() == 1);
    CHECK(result.trapped_resin.front().first_layer == 1);
    CHECK(result.trapped_resin.front().last_layer == 4);
    CHECK(result.trapped_resin.front().volume_mm3 == Approx(2 * 100.0 + 2 * 196.0).margin(0.01));
}

TEST_CASE("CavityDetection: no layers and no thicknesses are handled", "[SLA][CavityDetection]")
{
    const SLA::CavityAnalysis no_layers = SLA::detect_cavities({}, {});
    CHECK(no_layers.cups.empty());
    CHECK(no_layers.trapped_resin.empty());

    // The layers without their thicknesses are still walked, only the volumes come out zero.
    const std::vector<ExPolygons> layers   = make_cup();
    const SLA::CavityAnalysis no_thickness = SLA::detect_cavities(layers, {});
    REQUIRE(no_thickness.cups.size() == 1);
    CHECK(no_thickness.cups.front().volume_mm3 == Approx(0.0).margin(0.01));
    CHECK(no_thickness.cups.front().opening_area_mm2 == Approx(cup_opening_mm2).margin(0.01));
}
