#include <libslic3r/SLA/LayerStats.hpp>

#include <libslic3r/ExPolygon.hpp>
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"

namespace Slic3r::SLA {

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

std::vector<float> peel_force_estimate(const std::vector<float>& areas_mm2)
{
    std::vector<float> forces;
    forces.reserve(areas_mm2.size());

    for (float area : areas_mm2) {
        forces.push_back(area * PEEL_FORCE_K);
    }

    return forces;
}

} // namespace Slic3r::SLA