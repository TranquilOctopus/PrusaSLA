#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/SlaObjectUseRows.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"

#include <memory>
#include <string>
#include <vector>

using Slic3r::App::build_sla_object_use_rows;
using Slic3r::App::sla_object_use_row_text;
using Slic3r::App::sla_object_use_value_text;
using Slic3r::App::SlaObjectUseRow;
using Slic3r::App::SlaObjectUseRows;
using Slic3r::App::unsliced_sla_object_use_rows;
using Slic3r::Domain::ConfigView;
using Slic3r::Domain::FullConfigSLA;
using Slic3r::Domain::ObjectID;
using Slic3r::SLA::ObjectResinUse;

namespace {

/// The UTF-8 bytes of U+2014, the dash the SLA sidebar shows where a value is not available.
const std::string no_value{"\xE2\x80\x94"};

/// A resin that is sold in a litre bottle for 50, so a model of 12.4 ml costs 0.62.
ConfigView resin_config(double bottle_volume_ml = 1000., double bottle_cost = 50.)
{
    auto full_config = std::make_shared<FullConfigSLA>(FullConfigSLA::defaults());
    full_config->set("bottle_volume", bottle_volume_ml);
    full_config->set("bottle_cost", bottle_cost);

    ConfigView config(full_config, {});
    // Until finalize() runs, values() is empty and every lookup misses.
    config.finalize();
    return config;
}

ObjectID object_id(uint16_t id)
{
    ObjectID out;
    out.id = id;
    return out;
}

/// What the slicer would have reported for a model of @p volume_mm3.
ObjectResinUse model_use(const std::string& name, double volume_mm3)
{
    ObjectResinUse use;
    use.name = name;
    // The id is left alone on purpose: the rows are keyed by the name in these tests.
    use.model_volume_mm3   = volume_mm3;
    use.support_volume_mm3 = 0.;
    use.raft_volume_mm3    = 0.;
    use.footprint_mm2      = volume_mm3;
    return use;
}

} // namespace

TEST_CASE("build_sla_object_use_rows", "[sla_object_use_rows]")
{
    SECTION("no model on the plate")
    {
        const SlaObjectUseRows rows = build_sla_object_use_rows({}, resin_config());
        CHECK(rows.empty());
        // One model is not a breakdown, so there is no table to show.
        CHECK_FALSE(rows.show());
        CHECK(rows.title() == "Per model (0)");
    }

    SECTION("a single model is not worth a table")
    {
        const SlaObjectUseRows rows =
            build_sla_object_use_rows({model_use("vase", 12400.)}, resin_config());
        REQUIRE(rows.rows.size() == 1);
        CHECK_FALSE(rows.show());
        CHECK(rows.rows.front().object_name == "vase");
        // 12400 mm3 is 12.4 ml, which is 0.62 of the price of the litre bottle.
        CHECK(*rows.rows.front().millilitres == Catch::Approx(12.4));
        CHECK(*rows.rows.front().cost == Catch::Approx(0.62));
        CHECK(rows.volume_mm3 == Catch::Approx(12400.));
        CHECK(*rows.millilitres == Catch::Approx(12.4));
        CHECK(*rows.cost == Catch::Approx(0.62));
    }

    SECTION("more than one model is a table")
    {
        const SlaObjectUseRows rows = build_sla_object_use_rows(
            {model_use("vase", 12400.), model_use("cube", 1000.)},
            resin_config()
        );
        REQUIRE(rows.rows.size() == 2);
        CHECK(rows.show());
        CHECK(rows.title() == "Per model (2)");

        // The models keep the order of the plate and they add up to the figures above them.
        CHECK(rows.rows.front().object_name == "vase");
        CHECK(rows.rows.back().object_name == "cube");
        CHECK(rows.volume_mm3 == Catch::Approx(13400.));
        CHECK(*rows.millilitres == Catch::Approx(13.4));
        CHECK(*rows.cost == Catch::Approx(0.67));
    }

    SECTION("the model of a row is found by its id")
    {
        std::vector<ObjectResinUse> use{model_use("vase", 12400.), model_use("cube", 1000.)};
        use.front().object_id = object_id(7);
        use.back().object_id  = object_id(9);

        const SlaObjectUseRows rows = build_sla_object_use_rows(use, resin_config());
        REQUIRE(rows.rows.size() == 2);
        CHECK(rows.rows.front().object_id == object_id(7));
        CHECK(rows.rows.back().object_id == object_id(9));
    }

    SECTION("a resin without a price shows the volume only")
    {
        // No bottle volume in the preset: there is no cost to show and none is invented.
        const SlaObjectUseRows rows = build_sla_object_use_rows(
            {model_use("vase", 12400.), model_use("cube", 1000.)},
            resin_config(0., 50.)
        );
        REQUIRE(rows.rows.size() == 2);
        CHECK(*rows.rows.front().millilitres == Catch::Approx(12.4));
        CHECK_FALSE(rows.rows.front().cost.has_value());
        CHECK_FALSE(rows.cost.has_value());
        CHECK(sla_object_use_value_text(rows.rows.front()) == "12.4 ml");
    }

    SECTION("a resin that is not priced shows the volume only")
    {
        // The SLA material defaults carry no bottle cost: there is no cost to show and none is
        // invented.
        ConfigView unpriced{std::make_shared<FullConfigSLA>(FullConfigSLA::defaults()), {}};
        unpriced.finalize();

        const SlaObjectUseRows rows = build_sla_object_use_rows(
            {model_use("vase", 12400.), model_use("cube", 1000.)},
            unpriced
        );
        REQUIRE(rows.rows.size() == 2);
        CHECK(*rows.rows.front().millilitres == Catch::Approx(12.4));
        CHECK_FALSE(rows.rows.front().cost.has_value());
        CHECK_FALSE(rows.cost.has_value());
        CHECK(sla_object_use_value_text(rows.rows.front()) == "12.4 ml");
    }
}

TEST_CASE("unsliced_sla_object_use_rows", "[sla_object_use_rows]")
{
    SECTION("the models of a bed that has not been sliced")
    {
        const SlaObjectUseRows rows = unsliced_sla_object_use_rows({"vase", "cube"});
        REQUIRE(rows.rows.size() == 2);
        CHECK(rows.show());
        CHECK(rows.title() == "Per model (2)");
        // Before a slice there is nothing to break down, so every row is the dash the plate figures
        // carry in the same state.
        CHECK(sla_object_use_row_text(rows.rows.front()) == "vase  " + no_value);
        CHECK(sla_object_use_row_text(rows.rows.back()) == "cube  " + no_value);
        CHECK(rows.volume_mm3 == Catch::Approx(0.));
        CHECK_FALSE(rows.millilitres.has_value());
        CHECK_FALSE(rows.cost.has_value());
    }

    SECTION("a single model is still no table")
    {
        const SlaObjectUseRows rows = unsliced_sla_object_use_rows({"vase"});
        REQUIRE(rows.rows.size() == 1);
        CHECK_FALSE(rows.show());
        CHECK(sla_object_use_value_text(rows.rows.front()) == no_value);
    }

    SECTION("an empty plate")
    {
        const SlaObjectUseRows rows = unsliced_sla_object_use_rows({});
        CHECK(rows.empty());
        CHECK_FALSE(rows.show());
    }
}

TEST_CASE("sla_object_use_row_text", "[sla_object_use_rows]")
{
    const ConfigView config = resin_config();
    const SlaObjectUseRows rows =
        build_sla_object_use_rows({model_use("vase", 12400.), model_use("cube", 1000.)}, config);

    SECTION("the figures of one model")
    {
        CHECK(sla_object_use_value_text(rows.rows.front()) == "12.4 ml, 0.62");
        CHECK(sla_object_use_value_text(rows.rows.back()) == "1.0 ml, 0.05");
    }

    SECTION("the name in front of the figures")
    {
        CHECK(sla_object_use_row_text(rows.rows.front()) == "vase  12.4 ml, 0.62");
        CHECK(sla_object_use_row_text(rows.rows.back()) == "cube  1.0 ml, 0.05");
    }

    SECTION("a model without a name shows the figures alone")
    {
        SlaObjectUseRow row;
        row.millilitres = 3.;
        CHECK(sla_object_use_row_text(row) == "3.0 ml");
        CHECK(sla_object_use_value_text(row) == "3.0 ml");
    }

    SECTION("a volume without a price")
    {
        SlaObjectUseRow row;
        row.millilitres = 3.;
        row.cost        = 0.;
        CHECK(sla_object_use_value_text(row) == "3.0 ml, 0.00");
    }

    SECTION("no value at all")
    {
        CHECK(sla_object_use_value_text(SlaObjectUseRow{}) == no_value);
    }
}
