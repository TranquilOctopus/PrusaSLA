#ifndef slic3r_SLA_SuppotstIslands_UniformSupportIsland_hpp_
#define slic3r_SLA_SuppotstIslands_UniformSupportIsland_hpp_

#include <libslic3r/ExPolygon.hpp>
#include "libslic3r/SLA/SupportIslands/SampleConfig.hpp"
#include "SupportIslandPoint.hpp"
#include "libslic3r/SLA/SupportPointGenerator.hpp" // Peninsula

namespace Slic3r::sla {

/// <summary>
/// Distribute support points across island area defined by ExPolygon.
/// </summary>
/// <param name="island">Shape of island</param>
/// <param name="permanent">Place supported by already existing supports</param>
/// <param name="config">Configuration of support density</param>
/// <param name="thr">Ask for a stop while the island is sampled (M4.16). The sampling of a big
/// island is the longest single step of the support tool and it walks many samples, so it asks as
/// it goes: the default never asks, which is what a caller that cannot be stopped wants.</param>
/// <returns>Support points laying inside of the island</returns>
SupportIslandPoints uniform_support_island(
    const ExPolygon &island, const Points &permanent, const SampleConfig &config,
    ThrowOnCancel thr = &detail::generator_no_throw_on_cancel);

/// <summary>
/// Distribute support points across peninsula
/// </summary>
/// <param name="peninsula">half island with anotation of the coast and land outline</param>
/// <param name="permanent">Place supported by already existing supports</param>
/// <param name="config">Density distribution parameters</param>
/// <param name="thr">Ask for a stop while the peninsula is sampled (M4.16)</param>
/// <returns>Support points laying inside of the peninsula</returns>
SupportIslandPoints uniform_support_peninsula(
    const Peninsula &peninsula, const Points& permanent, const SampleConfig &config,
    ThrowOnCancel thr = &detail::generator_no_throw_on_cancel);

/// <summary>
/// Check for tests that developer do not forget disable visualization after debuging.
/// </summary>
bool is_uniform_support_island_visualization_disabled();

} // namespace Slic3r::sla
#endif // slic3r_SLA_SuppotstIslands_UniformSupportIsland_hpp_
