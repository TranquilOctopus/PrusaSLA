#pragma once

#include <vector>

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::SLA {

struct IslandHit
{
    size_t layer_index = 0;
    Domain::Vec2d centroid = Domain::Vec2d::Zero();
    double area_mm2 = 0.0;
};

std::vector<IslandHit> detect_islands(const std::vector<Domain::ExPolygons>& layers, double min_area_mm2);

} // namespace Slic3r::SLA