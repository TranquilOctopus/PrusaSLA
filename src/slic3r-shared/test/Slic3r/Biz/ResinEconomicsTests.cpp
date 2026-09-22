#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

#include "Slic3r/Biz/ResinEconomics.hpp"
#include "Slic3r/Domain/SLA/PrintStatistics.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"

using namespace Slic3r::Biz;
using namespace Slic3r::Domain;

TEST_CASE("ResinEconomics::calculate - normal values", "[resin_economics]")
{
    ResinEconomicsInput input;
    input.used_material_mm3 = 50000.0; // 50 ml
    input.bottle_volume_ml = 1000.0;   // 1 liter bottle
    input.bottle_weight_kg = 1.0;
    input.bottle_cost = 50.0;          // $50 per bottle
    input.material_density_g_ml = 1.1; // 1.1 g/ml

    ResinEconomicsResult result = ResinEconomics::calculate(input);

    REQUIRE(result.millilitres.has_value());
    REQUIRE(*result.millilitres == 50.0); // 50000 mm³ = 50 ml

    REQUIRE(result.grams.has_value());
    REQUIRE(*result.grams == 55.0); // 50 ml * 1.1 g/ml

    REQUIRE(result.cost.has_value());
    REQUIRE(*result.cost == 2.5); // (50/1000) * 50 = 2.5

    REQUIRE(result.bottles_fraction.has_value());
    REQUIRE(*result.bottles_fraction == 0.05); // 50/1000
}

TEST_CASE("ResinEconomics::calculate - zero bottle volume", "[resin_economics]")
{
    ResinEconomicsInput input;
    input.used_material_mm3 = 50000.0; // 50 ml
    input.bottle_volume_ml = 0.0;      // Zero bottle volume
    input.bottle_weight_kg = 1.0;
    input.bottle_cost = 50.0;
    input.material_density_g_ml = 1.1;

    ResinEconomicsResult result = ResinEconomics::calculate(input);

    REQUIRE(result.millilitres.has_value());
    REQUIRE(*result.millilitres == 50.0);

    REQUIRE(result.grams.has_value());
    REQUIRE(*result.grams == 55.0);

    // Cost and bottles_fraction should not be set due to zero bottle volume
    REQUIRE_FALSE(result.cost.has_value());
    REQUIRE_FALSE(result.bottles_fraction.has_value());
}

TEST_CASE("ResinEconomics::calculate - missing cost", "[resin_economics]")
{
    ResinEconomicsInput input;
    input.used_material_mm3 = 50000.0; // 50 ml
    input.bottle_volume_ml = 1000.0;
    input.bottle_weight_kg = 1.0;
    input.bottle_cost = std::nullopt;  // Missing cost
    input.material_density_g_ml = 1.1;

    ResinEconomicsResult result = ResinEconomics::calculate(input);

    REQUIRE(result.millilitres.has_value());
    REQUIRE(*result.millilitres == 50.0);

    REQUIRE(result.grams.has_value());
    REQUIRE(*result.grams == 55.0);

    // Cost should not be set
    REQUIRE_FALSE(result.cost.has_value());

    // Bottles fraction should be set since volume is available
    REQUIRE(result.bottles_fraction.has_value());
    REQUIRE(*result.bottles_fraction == 0.05);
}

TEST_CASE("ResinEconomics::calculate - missing density", "[resin_economics]")
{
    ResinEconomicsInput input;
    input.used_material_mm3 = 50000.0; // 50 ml
    input.bottle_volume_ml = 1000.0;
    input.bottle_weight_kg = 1.0;
    input.bottle_cost = 50.0;
    input.material_density_g_ml = std::nullopt; // Missing density

    ResinEconomicsResult result = ResinEconomics::calculate(input);

    REQUIRE(result.millilitres.has_value());
    REQUIRE(*result.millilitres == 50.0);

    // Grams should not be set
    REQUIRE_FALSE(result.grams.has_value());

    REQUIRE(result.cost.has_value());
    REQUIRE(*result.cost == 2.5);

    REQUIRE(result.bottles_fraction.has_value());
    REQUIRE(*result.bottles_fraction == 0.05);
}

TEST_CASE("ResinEconomics::calculate - all missing bottle values", "[resin_economics]")
{
    ResinEconomicsInput input;
    input.used_material_mm3 = 50000.0; // 50 ml
    input.bottle_volume_ml = std::nullopt;
    input.bottle_weight_kg = std::nullopt;
    input.bottle_cost = std::nullopt;
    input.material_density_g_ml = std::nullopt;

    ResinEconomicsResult result = ResinEconomics::calculate(input);

    REQUIRE(result.millilitres.has_value());
    REQUIRE(*result.millilitres == 50.0);

    REQUIRE_FALSE(result.grams.has_value());
    REQUIRE_FALSE(result.cost.has_value());
    REQUIRE_FALSE(result.bottles_fraction.has_value());

    // Summary should still show ml
    REQUIRE(result.summary.find("50.00 ml") != std::string::npos);
}

TEST_CASE("ResinEconomics::calculate - from PrintStatistics and ConfigView", "[resin_economics]")
{
    // Create a minimal FullConfigSLA with bottle settings
    auto full_config = std::make_shared<const FullConfigSLA>(FullConfigSLA::defaults());
    // Note: FullConfigSLA::defaults() should include the SLA config definitions with bottle settings
    
    ConfigView config_view(full_config, {});
    // Until finalize() runs, values() is empty and every config lookup misses, which would make
    // this test silently cover the no-config fallback instead of the config path.
    config_view.finalize();

    SLA::PrintStatistics stats;
    stats.objects_used_material = 30000.0; // 30 ml
    stats.support_used_material = 20000.0; // 20 ml

    ResinEconomicsResult result = ResinEconomics::calculate(stats, config_view);

    // Should at least have millilitres from the stats
    REQUIRE(result.millilitres.has_value());
    REQUIRE(*result.millilitres == 50.0); // 30 + 20 = 50 ml
}

TEST_CASE("ResinEconomics::calculate - zero used material", "[resin_economics]")
{
    ResinEconomicsInput input;
    input.used_material_mm3 = 0.0;
    input.bottle_volume_ml = 1000.0;
    input.bottle_cost = 50.0;
    input.material_density_g_ml = 1.1;

    ResinEconomicsResult result = ResinEconomics::calculate(input);

    REQUIRE(result.millilitres.has_value());
    REQUIRE(*result.millilitres == 0.0);

    REQUIRE(result.grams.has_value());
    REQUIRE(*result.grams == 0.0);

    REQUIRE(result.cost.has_value());
    REQUIRE(*result.cost == 0.0);

    REQUIRE(result.bottles_fraction.has_value());
    REQUIRE(*result.bottles_fraction == 0.0);
}