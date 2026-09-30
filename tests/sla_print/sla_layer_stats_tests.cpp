#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <cmath>

#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "libslic3r/SLA/CavityDetection.hpp"
#include "libslic3r/SLA/LayerStats.hpp"

using namespace Slic3r;
using Slic3r::Domain::ExPolygon;
using Slic3r::Domain::ExPolygons;
using Slic3r::Domain::Point;
using Slic3r::Domain::Polygon;
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
        if (i != empty_layer_idx) {
            const size_t size_idx = i < empty_layer_idx ? i : i - 1;
            if (size_idx < sizes_mm.size())
                layer.push_back(make_square(sizes_mm[size_idx]));
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

TEST_CASE("LayerStats: layer_perimeters_mm counts the contour and the holes", "[SLA][LayerStats]")
{
    // A 10x10 mm square with a 2x2 mm hole in it: 4*10 mm of contour plus 4*2 mm of hole.
    ExPolygon with_hole = make_square(10.0);
    Polygon hole;
    hole.points = {
        Point(scaled(-1.0), scaled(-1.0)),
        Point(scaled(-1.0), scaled(1.0)),
        Point(scaled(1.0), scaled(1.0)),
        Point(scaled(1.0), scaled(-1.0)),
        Point(scaled(-1.0), scaled(-1.0))
    };
    with_hole.holes.push_back(hole);

    const std::vector<ExPolygons> layers{{with_hole}};
    auto perimeters = SLA::layer_perimeters_mm(layers);
    REQUIRE(perimeters.size() == 1);
    CHECK(perimeters[0] == Approx(48.0).margin(0.01));

    // The same square without the hole is only the contour.
    const std::vector<ExPolygons> plain{{make_square(10.0)}};
    CHECK(SLA::layer_perimeters_mm(plain)[0] == Approx(40.0).margin(0.01));
}

TEST_CASE("LayerStats: layer_perimeters_mm sums every polygon of the layer", "[SLA][LayerStats]")
{
    std::vector<ExPolygons> layers(1);
    layers[0].push_back(make_square(5.0, -10.0, 0.0));
    layers[0].push_back(make_square(3.0, 10.0, 0.0));
    auto perimeters = SLA::layer_perimeters_mm(layers);
    REQUIRE(perimeters.size() == 1);
    CHECK(perimeters[0] == Approx(32.0).margin(0.01)); // 4*5 + 4*3
}

TEST_CASE("LayerStats: peel force of a comb is above the square of the same area", "[SLA][LayerStats]")
{
    // Same 100 mm² of cured resin, spread over a 10x10 mm square or over ten 1 mm wide, 10 mm
    // long teeth with 1 mm gaps between them. The areas match; the comb has a much longer
    // boundary for the film to peel along, which is the whole point of the perimeter term.
    ExPolygons comb;
    for (int tooth = 0; tooth < 10; ++tooth) {
        const double x = 1.0 + tooth * 2.0;
        ExPolygon tooth_poly;
        tooth_poly.contour.points = {
            Point(scaled(x), scaled(-5.0)),
            Point(scaled(x + 1.0), scaled(-5.0)),
            Point(scaled(x + 1.0), scaled(5.0)),
            Point(scaled(x), scaled(5.0)),
            Point(scaled(x), scaled(-5.0))
        };
        comb.push_back(tooth_poly);
    }

    const std::vector<ExPolygons> square_layer{{make_square(10.0)}};
    const std::vector<ExPolygons> comb_layer{comb};

    const SLA::PeelForceCoefficients k = SLA::peel_force_coefficients(Domain::sla::VatFilmType::FEP);
    const std::vector<float> square_force = SLA::peel_force_estimate(square_layer, {}, k);
    const std::vector<float> comb_force   = SLA::peel_force_estimate(comb_layer, {}, k);

    REQUIRE(square_force.size() == 1);
    REQUIRE(comb_force.size() == 1);
    // The areas are what the test needs to hold, or the comparison proves nothing.
    CHECK(SLA::layer_areas_mm2(square_layer)[0] == Approx(100.0).margin(0.01));
    CHECK(SLA::layer_areas_mm2(comb_layer)[0] == Approx(100.0).margin(0.01));
    CHECK(SLA::layer_perimeters_mm(comb_layer)[0] == Approx(220.0).margin(0.01));
    CHECK(SLA::layer_perimeters_mm(square_layer)[0] == Approx(40.0).margin(0.01));
    CHECK(comb_force[0] > square_force[0]);
}

TEST_CASE("LayerStats: FEP peels harder than ACF on the same layer", "[SLA][LayerStats]")
{
    const std::vector<ExPolygons> layers = make_layers_with_squares({10.0, 6.0});

    const auto fep = SLA::peel_force_estimate(
        layers, {}, SLA::peel_force_coefficients(Domain::sla::VatFilmType::FEP));
    const auto acf = SLA::peel_force_estimate(
        layers, {}, SLA::peel_force_coefficients(Domain::sla::VatFilmType::ACF));

    REQUIRE(fep.size() == 2);
    REQUIRE(acf.size() == 2);
    CHECK(fep[0] > acf[0]);
    CHECK(fep[1] > acf[1]);
}

TEST_CASE("LayerStats: every film gives finite positive coefficients", "[SLA][LayerStats]")
{
    const std::vector<Domain::sla::VatFilmType> films{
        Domain::sla::VatFilmType::FEP, Domain::sla::VatFilmType::nFEP,
        Domain::sla::VatFilmType::PFA, Domain::sla::VatFilmType::ACF};

    const std::vector<ExPolygons> layers = make_layers_with_squares({20.0});
    for (Domain::sla::VatFilmType film : films) {
        const SLA::PeelForceCoefficients k = SLA::peel_force_coefficients(film);
        INFO("Film " << int(film));
        CHECK(std::isfinite(k.area_coefficient_n_per_mm2));
        CHECK(std::isfinite(k.perimeter_coefficient_n_per_mm));
        CHECK(std::isfinite(k.suction_coefficient_n_per_mm2));
        CHECK(k.area_coefficient_n_per_mm2 > 0.);
        CHECK(k.perimeter_coefficient_n_per_mm > 0.);
        CHECK(k.suction_coefficient_n_per_mm2 > 0.);
        CHECK(k.warning_n > 0.);

        // A 20x20 mm layer of the default film has to cost something finite and positive.
        const std::vector<float> forces = SLA::peel_force_estimate(layers, {}, k);
        REQUIRE(forces.size() == 1);
        CHECK(std::isfinite(forces[0]));
        CHECK(forces[0] > 0.f);
    }
}

TEST_CASE("LayerStats: the default settings are FEP with the film coefficients", "[SLA][LayerStats]")
{
    const SLA::PeelForceSettings settings;
    const SLA::PeelForceCoefficients k = settings.coefficients();
    const SLA::PeelForceCoefficients fep =
        SLA::peel_force_coefficients(Domain::sla::VatFilmType::FEP);
    CHECK(k.area_coefficient_n_per_mm2 == Approx(fep.area_coefficient_n_per_mm2));
    CHECK(k.perimeter_coefficient_n_per_mm == Approx(fep.perimeter_coefficient_n_per_mm));
    CHECK(k.suction_coefficient_n_per_mm2 == Approx(fep.suction_coefficient_n_per_mm2));
    // A negative setting takes the default of the film.
    CHECK(SLA::peel_force_warning_n(-1., k) == Approx(fep.warning_n));
}

TEST_CASE("LayerStats: an override replaces only the coefficient it names", "[SLA][LayerStats]")
{
    SLA::PeelForceSettings settings;
    settings.film                 = Domain::sla::VatFilmType::ACF;
    settings.area_coefficient     = 0.5;
    settings.perimeter_coefficient = 0.;

    const SLA::PeelForceCoefficients acf =
        SLA::peel_force_coefficients(Domain::sla::VatFilmType::ACF);
    const SLA::PeelForceCoefficients k = settings.coefficients();
    CHECK(k.area_coefficient_n_per_mm2 == Approx(0.5));
    CHECK(k.perimeter_coefficient_n_per_mm == Approx(acf.perimeter_coefficient_n_per_mm));
    CHECK(k.suction_coefficient_n_per_mm2 == Approx(acf.suction_coefficient_n_per_mm2));

    // The warning is a setting of its own, so a coefficient says nothing about it.
    CHECK(SLA::peel_force_warning_n(12., k) == Approx(12.));
    CHECK(SLA::peel_force_warning_n(0., k) == Approx(0.));
}

TEST_CASE("LayerStats: the model adds the cup openings as a suction term", "[SLA][LayerStats]")
{
    const std::vector<ExPolygons> layers = make_layers_with_squares({10.0, 10.0});
    const SLA::PeelForceCoefficients k = SLA::peel_force_coefficients(Domain::sla::VatFilmType::FEP);

    // A cup over both layers with a 20 mm² opening, as detect_cavities() would report it.
    SLA::CupHit cup;
    cup.first_layer      = 0;
    cup.last_layer       = 1;
    cup.opening_area_mm2 = 20.;
    const std::vector<float> suction = SLA::cup_suction_area_mm2({cup}, 2);
    REQUIRE(suction.size() == 2);
    CHECK(suction[0] == Approx(20.0).margin(0.01));
    CHECK(suction[1] == Approx(20.0).margin(0.01));

    const std::vector<float> plain = SLA::peel_force_estimate(layers, {}, k);
    const std::vector<float> with_cup = SLA::peel_force_estimate(layers, suction, k);
    REQUIRE(plain.size() == 2);
    REQUIRE(with_cup.size() == 2);
    CHECK(with_cup[0] == Approx(plain[0] + 20. * k.suction_coefficient_n_per_mm2).margin(0.01));
    CHECK(with_cup[1] == Approx(plain[1] + 20. * k.suction_coefficient_n_per_mm2).margin(0.01));
}

TEST_CASE("LayerStats: cup_suction_area_mm2 stops at the last layer", "[SLA][LayerStats]")
{
    SLA::CupHit cup;
    cup.first_layer      = 3;
    cup.last_layer       = 9;
    cup.opening_area_mm2 = 5.;

    // Fewer layers than the cup is long, e.g. a sliced result that was cut short.
    const std::vector<float> suction = SLA::cup_suction_area_mm2({cup}, 5);
    REQUIRE(suction.size() == 5);
    CHECK(suction[0] == Approx(0.0).margin(0.01));
    CHECK(suction[2] == Approx(0.0).margin(0.01));
    CHECK(suction[3] == Approx(5.0).margin(0.01));
    CHECK(suction[4] == Approx(5.0).margin(0.01));
}

TEST_CASE("LayerStats: peel_force_estimate of no layers is no forces", "[SLA][LayerStats]")
{
    const std::vector<ExPolygons> layers;
    const SLA::PeelForceCoefficients k = SLA::peel_force_coefficients(Domain::sla::VatFilmType::FEP);
    CHECK(SLA::peel_force_estimate(layers, {}, k).empty());
    CHECK(SLA::layer_areas_mm2(layers).empty());
    CHECK(SLA::layer_perimeters_mm(layers).empty());
}

TEST_CASE("LayerStats: peel force is the weighted sum of the three terms", "[SLA][LayerStats]")
{
    const std::vector<ExPolygons> layers = make_layers_with_squares({8.0});
    const SLA::PeelForceCoefficients k{0.2, 0.4, 0.6, 0.};
    const std::vector<float> areas = SLA::layer_areas_mm2(layers);
    const std::vector<float> perimeters = SLA::layer_perimeters_mm(layers);
    const std::vector<float> suction{3.};

    const std::vector<float> forces = SLA::peel_force_estimate(layers, suction, k);
    REQUIRE(forces.size() == 1);
    const double expected = 0.2 * areas[0] + 0.4 * perimeters[0] + 0.6 * 3.;
    CHECK(forces[0] == Approx(expected).margin(0.01));
}

TEST_CASE("LayerStats: layers over the warning threshold are listed", "[SLA][LayerStats]")
{
    const std::vector<float> forces{1.f, 25.f, 4.f, 30.f, 9.f};
    const std::vector<size_t> over = SLA::layers_over_peel_force(forces, 20.);
    REQUIRE(over.size() == 2);
    CHECK(over[0] == 1);
    CHECK(over[1] == 3);
}

TEST_CASE("LayerStats: a warning threshold of zero flags nothing", "[SLA][LayerStats]")
{
    const std::vector<float> forces{0.f, 1.f, 25.f, 400.f};
    CHECK(SLA::layers_over_peel_force(forces, 0.).empty());
    // A negative one is not a threshold either, it is how the film default is asked for.
    CHECK(SLA::layers_over_peel_force(forces, -1.).empty());
}

TEST_CASE("LayerStats: a threshold above every layer flags nothing", "[SLA][LayerStats]")
{
    const std::vector<float> forces{1.f, 2.f, 3.f};
    CHECK(SLA::layers_over_peel_force(forces, 3.).empty());
    // The comparison is strictly above, so a layer exactly at the limit is not called high.
    CHECK(SLA::layers_over_peel_force(forces, 2.9).size() == 1);
}
