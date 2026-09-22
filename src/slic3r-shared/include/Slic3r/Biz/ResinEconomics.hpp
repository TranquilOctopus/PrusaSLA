#pragma once

#include "Slic3r/Domain/SLA/PrintStatistics.hpp"
#include "Slic3r/Domain/Config.hpp"

#include <optional>
#include <string>

namespace Slic3r::Biz {

/**
 * @brief Result of resin economics calculation for a single bed or project total.
 */
struct ResinEconomicsResult
{
    /// Volume of resin used in millilitres (objects + support)
    std::optional<double> millilitres;
    /// Weight of resin used in grams
    std::optional<double> grams;
    /// Cost of resin used
    std::optional<double> cost;
    /// Number of bottles used (fractional)
    std::optional<double> bottles_fraction;
    /// Human-readable summary for UI display
    std::string summary;
};

/**
 * @brief Input parameters for resin economics calculation.
 */
struct ResinEconomicsInput
{
    /// Total material used in mm³ (objects + support)
    double used_material_mm3 = 0.0;
    /// Bottle volume in ml
    std::optional<double> bottle_volume_ml;
    /// Bottle weight in kg
    std::optional<double> bottle_weight_kg;
    /// Bottle cost in currency units
    std::optional<double> bottle_cost;
    /// Material density in g/ml
    std::optional<double> material_density_g_ml;
};

/**
 * @brief Pure calculation component for resin economics.
 * Given used material and bottle specifications, computes volume, weight, cost, and bottle fraction.
 * All outputs are std::optional to gracefully handle missing or zero bottle values.
 */
class ResinEconomics
{
public:
    /**
     * @brief Calculate resin economics from input parameters.
     * @param input Used material volume and bottle specifications
     * @return ResinEconomicsResult with computed values (std::optional for missing/zero inputs)
     */
    static ResinEconomicsResult calculate(const ResinEconomicsInput& input);

    /**
     * @brief Calculate resin economics from SLA print statistics and config.
     * @param stats Print statistics containing objects_used_material and support_used_material in mm³
     * @param config Config view to read bottle_volume, bottle_weight, bottle_cost, material_density
     * @return ResinEconomicsResult with computed values
     */
    static ResinEconomicsResult calculate(const Domain::SLA::PrintStatistics& stats, const Domain::ConfigView& config);
};

} // namespace Slic3r::Biz