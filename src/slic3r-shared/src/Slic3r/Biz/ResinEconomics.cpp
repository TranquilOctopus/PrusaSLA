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

// ConfigView::get asserts on a missing key: it panics, it does not throw, so a try/catch
// around it would not help. Look the key up and leave the value unset when it is absent,
// not a double, or not positive.
static std::optional<double> positive_double(const Domain::ConfigView& config, const std::string& key)
{
    const auto it = config.values().find(key);
    if (it == config.values().end() || !it->second.holds_alternative<double>())
        return std::nullopt;
    const double value = it->second.get<double>();
    return value > 0.0 ? std::optional<double>{value} : std::nullopt;
}

ResinEconomicsResult ResinEconomics::calculate(const Domain::SLA::PrintStatistics& stats, const Domain::ConfigView& config)
{
    ResinEconomicsInput input;
    input.used_material_mm3 = stats.objects_used_material + stats.support_used_material;

    input.bottle_volume_ml = positive_double(config, "bottle_volume");
    input.bottle_weight_kg = positive_double(config, "bottle_weight");
    input.bottle_cost      = positive_double(config, "bottle_cost");

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