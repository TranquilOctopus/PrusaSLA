#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/PopNotification/PopNotificationLayout.hpp"
#include "Slic3r/App/SlaHollowingSuggestion.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "libslic3r/SLAResult.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using Slic3r::App::build_hollowing_suggestion_notification;
using Slic3r::App::hollowing_saved_ml;
using Slic3r::App::hollowing_settings;
using Slic3r::App::hollowing_suggest_margin_mm;
using Slic3r::App::hollowing_suggest_min_saving_ml;
using Slic3r::App::hollowing_suggestion_eligible;
using Slic3r::App::hollowing_suggestion_text;
using Slic3r::App::SlaHollowingSettings;
using Slic3r::App::SlaHollowingSuggestion;
using Slic3r::App::suggest_hollowing;
using Slic3r::App::PopNotification::PopNotificationData;
using Slic3r::App::PopNotification::PopNotificationLayoutTextButtons;
using Slic3r::App::PopNotification::PopNotificationType;
using Slic3r::Biz::Slicing::Sla::ObjectSliceStats;
using Slic3r::Domain::ConfigView;
using Slic3r::Domain::FullConfigSLA;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::SelectionId;

namespace {

/// A solid block of @p width x @p depth x @p height mm, sliced at @p layer_height mm: the per layer
/// areas, outlines and thicknesses of such a block are the ones `merge_slices_and_eval_stats()`
/// measures of it, and the narrowest cross section at half its height is the thinner of the two
/// horizontal sides.
ObjectSliceStats solid_block(
    double width,
    double depth,
    double height,
    double layer_height = 0.4,
    std::string name    = "cube",
    size_t object_id    = 1
)
{
    const size_t layers = static_cast<size_t>(height / layer_height);
    ObjectSliceStats stats;
    stats.object_id       = {object_id};
    stats.name            = std::move(name);
    stats.hollowed        = false;
    stats.min_section_mm  = std::min(width, depth);
    stats.layer_areas_mm2 = std::vector<float>(layers, static_cast<float>(width * depth));
    stats.layer_perimeters_mm =
        std::vector<float>(layers, static_cast<float>(2. * (width + depth)));
    stats.layer_thicknesses_mm = std::vector<float>(layers, static_cast<float>(layer_height));
    double volume_mm3          = 0.;
    for (const float area : stats.layer_areas_mm2)
        volume_mm3 += static_cast<double>(area) * layer_height;
    stats.volume_mm3 = volume_mm3;
    return stats;
}

const PopNotificationLayoutTextButtons* buttons_of(const PopNotificationData& data)
{
    return std::get_if<PopNotificationLayoutTextButtons>(&data.layout);
}

/// The config of a slice of this build: the defaults of the SLA print preset, so the two keys the
/// suggestion reads are really there and the test covers the path that reads them.
ConfigView default_sla_config()
{
    ConfigView view{std::make_shared<const FullConfigSLA>(FullConfigSLA::defaults()), {}};
    // Until finalize() runs the view holds no values and every lookup misses, which would make the
    // test cover the fallback instead of the config.
    view.finalize();
    return view;
}

} // namespace

TEST_CASE(
    "A solid block is offered for hollowing, a hollow one is not",
    "[Sla][hollowing][suggestion]"
)
{
    const SlaHollowingSettings settings{}; // 20 ml over 3 mm walls, as the print preset defaults

    SECTION("a solid 40 mm cube costs 64 ml and a shell of it about 45 ml")
    {
        const ObjectSliceStats cube = solid_block(40., 40., 40.);

        CHECK(cube.volume_mm3 == Approx(64000.));
        CHECK(hollowing_saved_ml(cube, settings.wall_mm) == Approx(44.8).margin(0.05));
        CHECK(hollowing_suggestion_eligible(cube, settings));

        const std::vector<SlaHollowingSuggestion> suggestions =
            suggest_hollowing({cube}, PrinterTechnology::SLA, settings);
        REQUIRE(suggestions.size() == 1);
        CHECK(suggestions.front().name == "cube");
        CHECK(suggestions.front().object_id == cube.object_id);
        CHECK(suggestions.front().cured_ml == Approx(64.));
        CHECK(suggestions.front().saved_ml == Approx(44.8).margin(0.05));
        CHECK(suggestions.front().wall_mm == Approx(3.));
        CHECK(suggestions.front().min_section_mm == Approx(40.));
    }

    SECTION("the same cube printed hollow is not offered")
    {
        ObjectSliceStats cube = solid_block(40., 40., 40.);
        cube.hollowed         = true;

        CHECK_FALSE(hollowing_suggestion_eligible(cube, settings));
        CHECK(suggest_hollowing({cube}, PrinterTechnology::SLA, settings).empty());
    }

    SECTION("the estimate is an upper bound: the true saving of the cube is well under it")
    {
        // The exact saving is the solid cube less the 34 mm cube a 3 mm shell leaves, and the
        // estimate overstates it because the corners of the shell are counted twice.
        const ObjectSliceStats cube = solid_block(40., 40., 40.);
        const double exact_ml       = (40. * 40. * 40. - 34. * 34. * 34.) / 1000.;
        CHECK(exact_ml == Approx(17.7).margin(0.1));
        CHECK(hollowing_saved_ml(cube, settings.wall_mm) > exact_ml);
    }

    SECTION("a wall of nothing saves nothing")
    {
        const ObjectSliceStats cube = solid_block(40., 40., 40.);
        CHECK(hollowing_saved_ml(cube, 0.) == Approx(0.));
    }
}

TEST_CASE("A shape a wall cannot stand in is not offered", "[Sla][hollowing][suggestion]")
{
    SECTION("a 3 mm plate laid flat needs too little resin to be worth a second print")
    {
        // 4.8 ml of its own, under the 20 ml of the default threshold.
        const ObjectSliceStats plate = solid_block(40., 40., 3., 0.5, "plate");
        CHECK(plate.volume_mm3 == Approx(4800.));
        CHECK_FALSE(hollowing_suggestion_eligible(plate, SlaHollowingSettings{}));
        CHECK(suggest_hollowing({plate}, PrinterTechnology::SLA, SlaHollowingSettings{}).empty());
    }

    SECTION("a 3 mm plate stood on edge is skipped for its shape")
    {
        // 30 ml of its own, so the volume rule lets it through and the shape rule is what says no:
        // a 3 mm cross section cannot hold two 3 mm walls and the margin.
        const ObjectSliceStats plate = solid_block(3., 100., 100., 0.4, "plate");
        CHECK(plate.volume_mm3 == Approx(30000.));
        CHECK(plate.min_section_mm == Approx(3.));

        const SlaHollowingSettings low_threshold{1., 3., hollowing_suggest_margin_mm};
        CHECK_FALSE(hollowing_suggestion_eligible(plate, low_threshold));
        CHECK(suggest_hollowing({plate}, PrinterTechnology::SLA, low_threshold).empty());
    }

    SECTION("the cross section has to hold two walls plus the margin, not just one")
    {
        const SlaHollowingSettings settings{}; // 3 mm walls, 1 mm margin: over 7 mm, or nothing
        // Both blocks are over the volume threshold on their own, so only the shape rule can tell
        // them apart: a 7 mm cross section is exactly two walls and no margin, 7.5 mm leaves half
        // of it over.
        const ObjectSliceStats at_the_line = solid_block(7., 40., 80., 0.4, "at the line");
        CHECK(at_the_line.volume_mm3 > 20. * 1000.);
        CHECK_FALSE(hollowing_suggestion_eligible(at_the_line, settings));

        const ObjectSliceStats over_the_line = solid_block(7.5, 40., 80., 0.4, "over the line");
        CHECK(over_the_line.volume_mm3 > 20. * 1000.);
        CHECK(hollowing_suggestion_eligible(over_the_line, settings));
    }

    SECTION("a model with no cross section at half its height is not offered")
    {
        // Zero is how the merge step reports a body it could not cut at half the height, so the
        // rule says nothing about the shape rather than guessing at it.
        ObjectSliceStats no_section = solid_block(40., 40., 40., 0.4, "vase");
        no_section.min_section_mm   = 0.;
        CHECK_FALSE(hollowing_suggestion_eligible(no_section, SlaHollowingSettings{}));
    }

    SECTION("a model under the volume threshold is not offered")
    {
        // A 15 mm cube needs about 3.4 ml, well under the 20 ml of the default threshold.
        const ObjectSliceStats small = solid_block(15., 15., 15., 0.4, "small");
        CHECK(small.volume_mm3 < 20. * 1000.);
        CHECK_FALSE(hollowing_suggestion_eligible(small, SlaHollowingSettings{}));
    }
}

TEST_CASE("A threshold of zero suggests nothing", "[Sla][hollowing][suggestion]")
{
    const ObjectSliceStats cube = solid_block(40., 40., 40., 0.4, "cube");

    const SlaHollowingSettings off{0., 3., hollowing_suggest_margin_mm};
    CHECK_FALSE(hollowing_suggestion_eligible(cube, off));
    CHECK(suggest_hollowing({cube}, PrinterTechnology::SLA, off).empty());

    // The same cube at the default threshold, so the zero is what turned the suggestion off and
    // nothing else about the model.
    CHECK(hollowing_suggestion_eligible(cube, SlaHollowingSettings{}));
}

TEST_CASE("Only an SLA printer is offered a hollowing suggestion", "[Sla][hollowing][suggestion]")
{
    const std::vector<ObjectSliceStats> plate{solid_block(40., 40., 40., 0.4, "cube")};

    CHECK(suggest_hollowing(plate, PrinterTechnology::SLA, SlaHollowingSettings{}).size() == 1);
    CHECK(suggest_hollowing(plate, PrinterTechnology::FFF, SlaHollowingSettings{}).empty());
}

TEST_CASE("The suggestion names the model and its saving", "[Sla][hollowing][suggestion]")
{
    SlaHollowingSuggestion suggestion;
    suggestion.name     = "vase";
    suggestion.saved_ml = 44.8;

    SECTION("the line names the model and the saving, to one decimal")
    {
        const std::string text = hollowing_suggestion_text(suggestion);
        CHECK(text.find("vase") != std::string::npos);
        CHECK(text.find("44.8") != std::string::npos);
        CHECK(text.find("hollowed") != std::string::npos);
    }

    SECTION("a model the slice named nothing is still named as a model")
    {
        suggestion.name.clear();
        const std::string text = hollowing_suggestion_text(suggestion);
        CHECK(text.find("44.8") != std::string::npos);
        CHECK(text.find("hollowed") != std::string::npos);
    }
}

TEST_CASE(
    "The hollowing offer is a notification with one button that opens the tool",
    "[Sla][hollowing][suggestion]"
)
{
    SlaHollowingSuggestion suggestion;
    suggestion.object_id = Slic3r::Domain::ObjectID{7};
    suggestion.name      = "vase";
    suggestion.cured_ml  = 64.;
    suggestion.saved_ml  = 44.8;
    suggestion.wall_mm   = 3.;

    SECTION("the button hands the model back to the caller and the notification closes")
    {
        bool pressed                                  = false;
        const std::optional<PopNotificationData> data = build_hollowing_suggestion_notification(
            suggestion,
            SelectionId{3},
            [&pressed]()
            {
                pressed = true;
                return true;
            }
        );

        REQUIRE(data.has_value());
        CHECK(data->type == PopNotificationType::SlaHollowingSuggestion);
        CHECK(data->project_id == SelectionId{3});

        const PopNotificationLayoutTextButtons* layout = buttons_of(*data);
        REQUIRE(layout != nullptr);
        REQUIRE(layout->buttons.size() == 1);
        CHECK_FALSE(layout->buttons.front().text.empty());
        CHECK_FALSE(pressed);
        CHECK(layout->buttons.front().callback());
        CHECK(pressed);
    }

    SECTION("a saving below what the reader can see is not offered at all")
    {
        suggestion.saved_ml                           = hollowing_suggest_min_saving_ml / 2.;
        const std::optional<PopNotificationData> data = build_hollowing_suggestion_notification(
            suggestion,
            SelectionId{3},
            []() { return true; }
        );
        CHECK_FALSE(data.has_value());
    }
}

TEST_CASE(
    "The suggestion settings come off the config of the slice",
    "[Sla][hollowing][suggestion]"
)
{
    SECTION("the defaults of the SLA print preset are what the two keys hold")
    {
        const SlaHollowingSettings settings = hollowing_settings(default_sla_config());
        CHECK(settings.min_volume_ml == Approx(20.));
        CHECK(settings.wall_mm == Approx(3.));
        CHECK(settings.margin_mm == Approx(hollowing_suggest_margin_mm));
    }

    SECTION("a key the config does not carry keeps the value the caller passed in")
    {
        const SlaHollowingSettings fallback{7., 2., 0.5};
        const ConfigView
            without_values{std::make_shared<const FullConfigSLA>(FullConfigSLA::defaults()), {}};
        // Not finalized, so the view holds no values and every lookup misses: what comes back is
        // then the fallback, which is how a slice of a file written before the keys existed is
        // still judged rather than skipped.
        const SlaHollowingSettings settings = hollowing_settings(without_values, fallback);
        CHECK(settings.min_volume_ml == Approx(7.));
        CHECK(settings.wall_mm == Approx(2.));
        CHECK(settings.margin_mm == Approx(0.5));
    }
}
