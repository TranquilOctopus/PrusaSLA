#include <libslic3r/SLA/IslandDetection.hpp>

#include <libslic3r/ClipperUtils.hpp>
#include "ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"

namespace Slic3r::SLA {

std::vector<IslandHit> detect_islands(const std::vector<Domain::ExPolygons>& layers, double min_area_mm2)
{
    std::vector<IslandHit> hits;

    if (layers.size() < 2)
        return hits;

    for (size_t layer_idx = 1; layer_idx < layers.size(); ++layer_idx) {
        const Domain::ExPolygons& current_layer = layers[layer_idx];
        const Domain::ExPolygons& prev_layer = layers[layer_idx - 1];

        if (current_layer.empty())
            continue;

        // Subtract previous layer's polygons from current layer's polygons
        // Islands are regions in current layer that have no overlap with previous layer
        Domain::ExPolygons unsupported = diff_ex(current_layer, prev_layer);

        for (const Domain::ExPolygon& island : unsupported) {
            double area = Biz::Algorithms::ExPolygon::area(island);
            double area_mm2 = area * (1.0 / Slic3r::SCALING_FACTOR) * (1.0 / Slic3r::SCALING_FACTOR);

            if (area_mm2 >= min_area_mm2) {
                // Compute centroid of the island
                Domain::Point centroid_point = island.contour.centroid();
                Domain::Vec2d centroid(unscaled<double>(centroid_point.x()), unscaled<double>(centroid_point.y()));

                hits.push_back(IslandHit{
                    .layer_index = layer_idx,
                    .centroid = centroid,
                    .area_mm2 = area_mm2
                });
            }
        }
    }

    return hits;
}

} // namespace Slic3r::SLA