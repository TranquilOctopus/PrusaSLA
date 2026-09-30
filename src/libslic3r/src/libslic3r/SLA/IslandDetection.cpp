#include <libslic3r/SLA/IslandDetection.hpp>

#include <libslic3r/ClipperUtils.hpp>
#include <libslic3r/ExPolygon.hpp>
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"

namespace Slic3r::SLA {

std::vector<IslandHit> detect_islands(const std::vector<Domain::ExPolygons>& layers, double min_area_mm2)
{
    std::vector<IslandHit> hits;

    if (layers.size() < 2)
        return hits;

    constexpr double sf = Biz::Algorithms::Scaling::SCALING_FACTOR;
    constexpr double epsilon_area = 1e-6 * sf * sf; // 1e-6 mm² in scaled units

    for (size_t layer_idx = 1; layer_idx < layers.size(); ++layer_idx) {
        const Domain::ExPolygons& current_layer = layers[layer_idx];
        const Domain::ExPolygons& prev_layer = layers[layer_idx - 1];

        if (current_layer.empty())
            continue;

        for (const Domain::ExPolygon& region : current_layer) {
            // Check if this region has any overlap with the previous layer
            Domain::ExPolygons intersection = intersection_ex(Domain::ExPolygons{region}, prev_layer);

            double overlap_area = 0.0;
            for (const Domain::ExPolygon& poly : intersection) {
                overlap_area += Biz::Algorithms::ExPolygon::area(poly);
            }

            // If no meaningful overlap, this region is an island
            if (overlap_area <= epsilon_area) {
                double area = Biz::Algorithms::ExPolygon::area(region);
                double area_mm2 = area * sf * sf;

                if (area_mm2 >= min_area_mm2) {
                    Domain::Point centroid_point = region.contour.centroid();
                    Domain::Vec2d centroid(unscaled<double>(centroid_point.x()), unscaled<double>(centroid_point.y()));

                    hits.push_back(
                        IslandHit{
                            .layer_index = layer_idx,
                            .centroid    = centroid,
                            .area_mm2    = area_mm2,
                            .region      = region
                        }
                    );
                }
            }
        }
    }

    return hits;
}

IslandOwner attribute_island(const IslandHit& island, const std::vector<ObjectLayer>& object_layers)
{
    IslandOwner owner;
    // Any overlap at all beats no object, the winner is the object covering the most of the island.
    double best_overlap = 0.0;

    if (island.region.contour.empty())
        return owner;

    const Domain::ExPolygons island_area{island.region};

    for (const ObjectLayer& object : object_layers) {
        if (object.slices.empty())
            continue;

        double overlap = 0.0;
        for (const Domain::ExPolygon& poly : intersection_ex(island_area, object.slices))
            overlap += Biz::Algorithms::ExPolygon::area(poly);

        if (overlap > best_overlap) {
            best_overlap    = overlap;
            owner.object_id = object.object_id;
            owner.name      = object.name;
        }
    }

    return owner;
}

} // namespace Slic3r::SLA