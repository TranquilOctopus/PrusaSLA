#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/SlaLayerKeys.hpp"

#include <vector>

using Slic3r::App::sla_layer_for_key;
using Slic3r::App::SlaLayerKey;

namespace {

constexpr size_t no_layers = 0;
constexpr size_t one_layer = 1;
constexpr size_t hundred   = 100;

std::vector<size_t> no_issues()
{
    return {};
}

} // namespace

TEST_CASE("SlaLayerKeys - no layers means no layer to go to", "[SlaLayerKeys]")
{
    for (SlaLayerKey key : {SlaLayerKey::PreviousLayer,
                            SlaLayerKey::NextLayer,
                            SlaLayerKey::PreviousTenLayers,
                            SlaLayerKey::NextTenLayers,
                            SlaLayerKey::FirstLayer,
                            SlaLayerKey::LastLayer,
                            SlaLayerKey::PreviousIssue,
                            SlaLayerKey::NextIssue}) {
        CHECK_FALSE(sla_layer_for_key(key, 0, no_layers, no_issues()).has_value());
    }
}

TEST_CASE("SlaLayerKeys - one layer is both the first and the last", "[SlaLayerKeys]")
{
    for (SlaLayerKey key : {SlaLayerKey::PreviousLayer,
                            SlaLayerKey::NextLayer,
                            SlaLayerKey::PreviousTenLayers,
                            SlaLayerKey::NextTenLayers,
                            SlaLayerKey::FirstLayer,
                            SlaLayerKey::LastLayer,
                            SlaLayerKey::PreviousIssue,
                            SlaLayerKey::NextIssue}) {
        CHECK_FALSE(sla_layer_for_key(key, 0, one_layer, no_issues()).has_value());
    }
}

TEST_CASE("SlaLayerKeys - one layer up and down moves one layer", "[SlaLayerKeys]")
{
    CHECK(*sla_layer_for_key(SlaLayerKey::NextLayer, 41, hundred, no_issues()) == 42);
    CHECK(*sla_layer_for_key(SlaLayerKey::PreviousLayer, 41, hundred, no_issues()) == 40);
}

TEST_CASE("SlaLayerKeys - the arrows stop at the ends of the range", "[SlaLayerKeys]")
{
    SECTION("the first layer has no layer below it")
    {
        CHECK_FALSE(sla_layer_for_key(SlaLayerKey::PreviousLayer, 0, hundred, no_issues()).has_value());
        CHECK_FALSE(sla_layer_for_key(SlaLayerKey::PreviousTenLayers, 0, hundred, no_issues()).has_value());
        CHECK_FALSE(sla_layer_for_key(SlaLayerKey::FirstLayer, 0, hundred, no_issues()).has_value());
    }

    SECTION("the last layer has no layer above it")
    {
        CHECK_FALSE(sla_layer_for_key(SlaLayerKey::NextLayer, 99, hundred, no_issues()).has_value());
        CHECK_FALSE(sla_layer_for_key(SlaLayerKey::NextTenLayers, 99, hundred, no_issues()).has_value());
        CHECK_FALSE(sla_layer_for_key(SlaLayerKey::LastLayer, 99, hundred, no_issues()).has_value());
    }

    SECTION("a layer next to the end still moves")
    {
        CHECK(*sla_layer_for_key(SlaLayerKey::NextLayer, 98, hundred, no_issues()) == 99);
        CHECK(*sla_layer_for_key(SlaLayerKey::PreviousLayer, 1, hundred, no_issues()) == 0);
    }
}

TEST_CASE("SlaLayerKeys - ten layers at a time move ten, or what is left", "[SlaLayerKeys]")
{
    SECTION("in the middle of the print")
    {
        CHECK(*sla_layer_for_key(SlaLayerKey::NextTenLayers, 5, hundred, no_issues()) == 15);
        CHECK(*sla_layer_for_key(SlaLayerKey::PreviousTenLayers, 85, hundred, no_issues()) == 75);
    }

    SECTION("a print shorter than the step lands on the other end instead of nowhere")
    {
        CHECK(*sla_layer_for_key(SlaLayerKey::NextTenLayers, 5, 8, no_issues()) == 7);
        CHECK(*sla_layer_for_key(SlaLayerKey::PreviousTenLayers, 5, 8, no_issues()) == 0);
    }
}

TEST_CASE("SlaLayerKeys - Home and End jump to the ends of the range", "[SlaLayerKeys]")
{
    CHECK(*sla_layer_for_key(SlaLayerKey::FirstLayer, 63, hundred, no_issues()) == 0);
    CHECK(*sla_layer_for_key(SlaLayerKey::LastLayer, 63, hundred, no_issues()) == 99);
}

TEST_CASE("SlaLayerKeys - a slider pointing outside the result still lands on a layer", "[SlaLayerKeys]")
{
    // A slider left over from a longer print points above the last layer of this one, so every
    // key starts from the layer it really shows.
    CHECK(*sla_layer_for_key(SlaLayerKey::PreviousLayer, 500, hundred, no_issues()) == 98);
    CHECK(*sla_layer_for_key(SlaLayerKey::PreviousTenLayers, 500, hundred, no_issues()) == 89);
    CHECK(*sla_layer_for_key(SlaLayerKey::FirstLayer, 500, hundred, no_issues()) == 0);

    // On that layer there is nothing above it any more, the same as when the slider was right.
    CHECK_FALSE(sla_layer_for_key(SlaLayerKey::NextLayer, 500, hundred, no_issues()).has_value());
    CHECK_FALSE(sla_layer_for_key(SlaLayerKey::LastLayer, 500, hundred, no_issues()).has_value());
}

TEST_CASE("SlaLayerKeys - without issues the issue keys move nothing", "[SlaLayerKeys]")
{
    CHECK_FALSE(sla_layer_for_key(SlaLayerKey::PreviousIssue, 41, hundred, no_issues()).has_value());
    CHECK_FALSE(sla_layer_for_key(SlaLayerKey::NextIssue, 41, hundred, no_issues()).has_value());
}

TEST_CASE("SlaLayerKeys - the issue keys step to the nearest issue in that direction", "[SlaLayerKeys]")
{
    // Three layers carry an issue: 7, 42 (the layer the user is on) and 90.
    const std::vector<size_t> issues{7, 42, 90};

    SECTION("an issue below and above")
    {
        CHECK(*sla_layer_for_key(SlaLayerKey::PreviousIssue, 42, hundred, issues) == 7);
        CHECK(*sla_layer_for_key(SlaLayerKey::NextIssue, 42, hundred, issues) == 90);
    }

    SECTION("an issue on the layer the user is on is left behind")
    {
        CHECK(*sla_layer_for_key(SlaLayerKey::PreviousIssue, 50, hundred, issues) == 42);
        CHECK(*sla_layer_for_key(SlaLayerKey::NextIssue, 30, hundred, issues) == 42);
    }

    SECTION("no issue in that direction")
    {
        CHECK_FALSE(sla_layer_for_key(SlaLayerKey::PreviousIssue, 7, hundred, issues).has_value());
        CHECK_FALSE(sla_layer_for_key(SlaLayerKey::NextIssue, 90, hundred, issues).has_value());
        CHECK_FALSE(sla_layer_for_key(SlaLayerKey::PreviousIssue, 0, hundred, issues).has_value());
        CHECK_FALSE(sla_layer_for_key(SlaLayerKey::NextIssue, 99, hundred, issues).has_value());
    }

    SECTION("the closest one wins, not the first or the last of the list")
    {
        const std::vector<size_t> many{3, 12, 41, 43, 60, 61, 62, 97};
        CHECK(*sla_layer_for_key(SlaLayerKey::PreviousIssue, 42, hundred, many) == 41);
        CHECK(*sla_layer_for_key(SlaLayerKey::NextIssue, 42, hundred, many) == 43);
    }

    SECTION("an issue on a layer this result does not have is skipped")
    {
        // A result of 50 layers whose issues were reported against a longer print.
        const std::vector<size_t> stale{7, 80};
        CHECK_FALSE(sla_layer_for_key(SlaLayerKey::NextIssue, 42, 50, stale).has_value());
        CHECK(*sla_layer_for_key(SlaLayerKey::PreviousIssue, 42, 50, stale) == 7);
    }
}

TEST_CASE("SlaLayerKeys - a layer with more than one issue is stepped over once", "[SlaLayerKeys]")
{
    // The slicer can report two issues on one layer. The window builds the list with every layer
    // only once, and the keys use that list, so they step over the layer and past it.
    const std::vector<size_t> issues{42, 42, 90};
    CHECK(*sla_layer_for_key(SlaLayerKey::NextIssue, 42, hundred, issues) == 90);
    CHECK_FALSE(sla_layer_for_key(SlaLayerKey::NextIssue, 90, hundred, issues).has_value());
}