#include <libslic3r/SLA/LayerStats.hpp>

#include <libslic3r/ExPolygon.hpp>
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/Geometry/ConvexHull.hpp"
#include "Slic3r/Biz/Algorithms/Polyline.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r::SLA {

using Domain::sla::VatFilmType;

namespace {

// The caliper of a convex polygon in mm: the smallest distance between two parallel lines that
// enclose it. Such a pair always has one of the two lines along an edge of the polygon, so the
// caliper is the smallest, over the edges, of the width of the polygon measured from that edge.
// The hull of a cross section of a model has few vertices and this runs once per print, so the
// widths are measured the plain way instead of with rotating calipers.
double convex_caliper_mm(const Domain::Polygon& hull)
{
    const size_t count = hull.size();
    if (count < 3) {
        return 0.; // a point or a segment has no width to speak of
    }

    double smallest = 0.;
    for (size_t i = 0; i < count; ++i) {
        const Domain::Vec2d a     = unscale(hull[i]);
        const Domain::Vec2d b     = unscale(hull[(i + 1) % count]);
        const Domain::Vec2d edge  = b - a;
        const double length       = edge.norm();
        if (length <= 0.) {
            continue;
        }
        // The distance of a point from the line of the edge, taken unsigned so that the winding of
        // the hull does not matter.
        double widest = 0.;
        for (size_t j = 0; j < count; ++j) {
            const Domain::Vec2d p = unscale(hull[j]);
            widest = std::max(
                widest,
                std::abs(edge.x() * (p.y() - a.y()) - edge.y() * (p.x() - a.x())) / length
            );
        }
        if (smallest == 0. || widest < smallest) {
            smallest = widest;
        }
    }
    return smallest;
}

} // namespace

PeelForceCoefficients peel_force_coefficients(VatFilmType film)
{
    // Rough literature values, not measurements on any printer. See
    // doc/sla-fork/profiling/peel-force.md. FEP stretches the most before it lets go and so pulls
    // the hardest, nFEP is a little softer, PFA is stiffer than nFEP but thin, and ACF gives way
    // early because it inflates instead of stretching.
    switch (film) {
    case VatFilmType::FEP:
        return PeelForceCoefficients{0.10, 0.15, 0.30, 20.};
    case VatFilmType::nFEP:
        return PeelForceCoefficients{0.08, 0.12, 0.25, 18.};
    case VatFilmType::PFA:
        return PeelForceCoefficients{0.06, 0.10, 0.20, 15.};
    case VatFilmType::ACF:
        return PeelForceCoefficients{0.02, 0.04, 0.12, 8.};
    }
    return PeelForceCoefficients{};
}

std::string_view vat_film_name(VatFilmType film)
{
    switch (film) {
    case VatFilmType::nFEP:
        return "nFEP";
    case VatFilmType::PFA:
        return "PFA";
    case VatFilmType::ACF:
        return "ACF";
    case VatFilmType::FEP:
        break;
    }
    return "FEP";
}

PeelForceCoefficients PeelForceSettings::coefficients() const
{
    PeelForceCoefficients out = peel_force_coefficients(film);
    if (area_coefficient > 0.)
        out.area_coefficient_n_per_mm2 = area_coefficient;
    if (perimeter_coefficient > 0.)
        out.perimeter_coefficient_n_per_mm2 = perimeter_coefficient;
    return out;
}

double peel_force_warning_n(double warning_setting, const PeelForceCoefficients& coefficients)
{
    if (warning_setting >= 0.)
        return warning_setting; // zero means off, a positive value is used as it is
    return coefficients.warning_n;
}

std::vector<float> layer_areas_mm2(const std::vector<Domain::ExPolygons>& layers)
{
    std::vector<float> areas;
    areas.reserve(layers.size());

    constexpr double sf = Biz::Algorithms::Scaling::SCALING_FACTOR;
    constexpr double scale_sq = sf * sf;

    for (const auto& layer_polygons : layers) {
        double area_scaled = Biz::Algorithms::ExPolygon::area(layer_polygons);
        float area_mm2 = static_cast<float>(area_scaled * scale_sq);
        areas.push_back(area_mm2);
    }

    return areas;
}

std::vector<float> layer_perimeters_mm(const std::vector<Domain::ExPolygons>& layers)
{
    std::vector<float> perimeters;
    perimeters.reserve(layers.size());

    constexpr double sf = Biz::Algorithms::Scaling::SCALING_FACTOR;

    for (const auto& layer_polygons : layers) {
        double length_scaled = 0.;
        // The film is peeled along every contour and along every hole of the layer: a comb has
        // more of both than a solid block of the same area. The lengths are summed in scaled
        // units and turned into mm at the end, the way layer_areas_mm2() does with the areas.
        for (const auto& ep : layer_polygons) {
            length_scaled += Biz::Algorithms::Polyline::length(ep.contour.points);
            for (const auto& hole : ep.holes)
                length_scaled += Biz::Algorithms::Polyline::length(hole.points);
        }
        perimeters.push_back(static_cast<float>(length_scaled * sf));
    }

    return perimeters;
}

std::vector<float> peel_force_estimate(const std::vector<LayerPeelInput>& layers,
                                       const PeelForceCoefficients& coefficients)
{
    std::vector<float> forces;
    forces.reserve(layers.size());

    for (const LayerPeelInput& layer : layers) {
        double force = coefficients.area_coefficient_n_per_mm2 * layer.area_mm2
                     + coefficients.perimeter_coefficient_n_per_mm2 * layer.perimeter_mm
                     + coefficients.suction_coefficient_n_per_mm2 * layer.suction_area_mm2;
        forces.push_back(static_cast<float>(force));
    }

    return forces;
}

std::vector<float> peel_force_estimate(const std::vector<Domain::ExPolygons>& layers,
                                       const std::vector<float>& suction_area_mm2,
                                       const PeelForceCoefficients& coefficients)
{
    const std::vector<float> areas   = layer_areas_mm2(layers);
    const std::vector<float> suction = suction_area_mm2.size() == layers.size()
                                           ? suction_area_mm2
                                           : std::vector<float>(layers.size(), 0.f);
    const std::vector<float> perimeters = layer_perimeters_mm(layers);

    std::vector<LayerPeelInput> inputs(layers.size());
    for (size_t layer = 0; layer < inputs.size(); ++layer) {
        inputs[layer] = LayerPeelInput{areas[layer], perimeters[layer], suction[layer]};
    }

    return peel_force_estimate(inputs, coefficients);
}

std::vector<size_t> layers_over_peel_force(const std::vector<float>& peel_force_n, double threshold_n)
{
    std::vector<size_t> over;
    if (threshold_n <= 0.)
        return over; // the warning is off
    for (size_t layer = 0; layer < peel_force_n.size(); ++layer) {
        if (peel_force_n[layer] > threshold_n)
            over.push_back(layer);
    }
    return over;
}

double min_cross_section_mm(const Domain::ExPolygons& slices)
{
    double smallest = 0.;
    for (const Domain::ExPolygon& slice : slices) {
        // The contour only: a hole of the layer is empty space inside the model, not material a
        // wall would have to be printed in. The hull of the contour is what the caliper is
        // measured on, so a region with a narrow neck is as thin as that neck.
        const Domain::Polygon hull = Biz::Algorithms::Geometry::convex_hull(slice.contour);
        const double caliper        = convex_caliper_mm(hull);
        if (caliper <= 0.) {
            continue; // no polygon with a shape, nothing to measure
        }
        if (smallest == 0. || caliper < smallest) {
            smallest = caliper;
        }
    }
    return smallest;
}

} // namespace Slic3r::SLA