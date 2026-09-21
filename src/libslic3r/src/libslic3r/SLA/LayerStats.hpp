#pragma once

#include <vector>

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"

namespace Slic3r::SLA {

/// Compute the exposed area (in mm²) for each layer.
/// @param layers Vector of ExPolygons per layer (in scaled coordinates).
/// @return Vector of areas in mm², one per layer.
std::vector<float> layer_areas_mm2(const std::vector<Domain::ExPolygons>& layers);

/// Estimate peel force for each layer based on its area.
/// @param areas_mm2 Vector of layer areas in mm².
/// @return Vector of estimated peel forces in Newtons (placeholder constant; only relative values meaningful).
std::vector<float> peel_force_estimate(const std::vector<float>& areas_mm2);

/// Placeholder proportionality constant for peel force estimation (N/mm²).
/// This is a rough approximation; only relative layer-to-layer values are meaningful
/// until empirical measurements are available.
constexpr float PEEL_FORCE_K = 0.1f; // N/mm²

} // namespace Slic3r::SLA