#include "Slic3r/Biz/ResinEconomics.hpp"

#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/SLA/PrintStatistics.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace Slic3r::Biz {

ResinEconomicsResult ResinEconomics::calculate(const ResinEconomicsInput& input)
{
    ResinEconomicsResult result;

    const double used_material_ml = input.used_material_mm3 / 1000.0; // mm³ to ml
    result.millilitres = used_material_ml;

    // Calculate grams if density is available
    if (input.material_density_g_ml.has_value() && *input.material_density_g_ml > 0.0) {
        result.grams = used_material_ml * *input.material_density_g_ml;
    }

    // Calculate cost and bottles_fraction if bottle_volume and bottle_cost are available
    if (input.bottle_volume_ml.has_value() && *input.bottle_volume_ml > 0.0) {
        result.bottles_fraction = used_material_ml / *input.bottle_volume_ml;

        if (input.bottle_cost.has_value()) {
            result.cost = result.bottles_fraction.value() * *input.bottle_cost;
        }
    }

    // Build human-readable summary
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);

    if (result.millilitres.has_value()) {
        ss << *result.millilitres << " ml";
    }

    if (result.grams.has_value()) {
        if (!ss.str().empty()) ss << ", ";
        ss << *result.grams << " g";
    }

    if (result.cost.has_value()) {
        if (!ss.str().empty()) ss << ", ";
        ss << *result.cost;
    }

    if (result.bottles_fraction.has_value()) {
        if (!ss.str().empty()) ss << ", ";
        ss << *result.bottles_fraction << " bottles";
    }

    result.summary = ss.str();
    if (result.summary.empty()) {
        result.summary = "No data";
    }

    return result;
}

ResinEconomicsResult ResinEconomics::calculate(const Domain::SLA::PrintStatistics& stats, const Domain::ConfigView& config)
{
    ResinEconomicsInput input;
    input.used_material_mm3 = stats.objects_used_material + stats.support_used_material;

    // Read bottle_volume (ml)
    try {
        const double bottle_volume = config.get<double>("bottle_volume");
        if (bottle_volume > 0.0) {
            input.bottle_volume_ml = bottle_volume;
        }
    } catch (...) {
        // Key not found or type mismatch, leave as nullopt
    }

    // Read bottle_weight (kg)
    try {
        const double bottle_weight = config.get<double>("bottle_weight");
        if (bottle_weight > 0.0) {
            input.bottle_weight_kg = bottle_weight;
        }
    } catch (...) {
        // Key not found or type mismatch, leave as nullopt
    }

    // Read bottle_cost
    try {
        const double bottle_cost = config.get<double>("bottle_cost");
        if (bottle_cost > 0.0) {
            input.bottle_cost = bottle_cost;
        }
    } catch (...) {
        // Key not found or type mismatch, leave as nullopt
    }

    // Read material_density (g/ml)
    try {
        const double material_density = config.get<double>("material_density");
        if (material_density > 0.0) {
            input.material_density_g_ml = material_density;
        }
    } catch (...) {
        // Key not found or type mismatch, leave as nullopt
    }

    return calculate(input);
}

} // namespace Slic3r::Biz