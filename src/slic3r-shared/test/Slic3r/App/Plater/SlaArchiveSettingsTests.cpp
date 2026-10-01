#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Plater/SlaArchiveSettings.hpp"
#include "Slic3r/App/PopNotification/PopNotificationLayout.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileMapper.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <variant>
#include <vector>

using Slic3r::App::analyze_archive_settings;
using Slic3r::App::build_archive_settings_notification;
using Slic3r::App::max_not_applied;
using Slic3r::App::SlaArchiveSettings;
using Slic3r::App::PopNotification::PopNotificationData;
using Slic3r::App::PopNotification::PopNotificationLayoutHeaderTextButtons;
using Slic3r::App::PopNotification::PopNotificationType;
using Slic3r::Biz::ResinProfile::ForeignResinProfile;
using Slic3r::Biz::ResinProfile::map_resin_profile;
using Slic3r::Biz::ResinProfile::MappingResult;
using Slic3r::Biz::ResinProfile::MappingStatus;
using Slic3r::Biz::ResinProfile::ResinImportResult;
using Slic3r::Biz::ResinProfile::TargetPrinterClass;
using Slic3r::Domain::SelectionId;

namespace {

/// What a dry run of the resin import of an archive with @p raw_values would produce. This is the
/// one call the review dialog makes too, so what the tests read here is what the dialog writes.
ResinImportResult review_of(
    const std::map<std::string, std::string>& raw_values,
    TargetPrinterClass printer_class = TargetPrinterClass::Tilt
)
{
    ForeignResinProfile profile;
    profile.source_format = "sliced-archive";
    profile.source_path   = "/somewhere/cube.sl1";
    profile.raw_values    = raw_values;

    ResinImportResult result;
    result.ok            = true;
    result.source_format = profile.source_format;
    result.mapping       = map_resin_profile(profile, printer_class);
    return result;
}

/// The values a resin preset of this fork has a key for: what store_sl1() writes into config.ini
/// of an archive, in the spellings it writes them in.
const std::map<std::string, std::string>& archive_material_values()
{
    static const std::map<std::string, std::string> values{
        {"expTime", "2.5"},
        {"expTimeFirst", "30"},
        {"layerHeight", "0.05"},
        {"numFade", "10"}
    };
    return values;
}

const std::map<std::string, std::string>& archive_print_values()
{
    static const std::map<std::string, std::string> values{
        {"support_type", "tree"},
        {"raft_type", "relief"},
        {"support_object_elevation", "0.2"}
    };
    return values;
}

std::map<std::string, std::string>
both(std::map<std::string, std::string> material, std::map<std::string, std::string> print)
{
    for (const auto& [key, value] : print)
        material.insert_or_assign(key, value);
    return material;
}

const PopNotificationLayoutHeaderTextButtons* buttons_of(const PopNotificationData& data)
{
    return std::get_if<PopNotificationLayoutHeaderTextButtons>(&data.layout);
}

} // namespace

TEST_CASE(
    "The spellings an SL1 archive writes reach the resin review",
    "[import][sla][archive][settings]"
)
{
    const ResinImportResult result = review_of(archive_material_values());

    SECTION("every material value of the archive is written to a resin preset key")
    {
        // store_sl1() writes config.ini straight off these config keys, in the same units, so the
        // values only need to be copied, not converted.
        CHECK(result.mapping.material_values.at("exposure_time") == "2.5");
        CHECK(result.mapping.material_values.at("initial_exposure_time") == "30");
        CHECK(result.mapping.material_values.at("resin_layer_height") == "0.05");
        CHECK(result.mapping.material_values.at("resin_faded_layers") == "10");
    }

    SECTION("they are named as offered, one label per value")
    {
        const SlaArchiveSettings settings = analyze_archive_settings(result);
        REQUIRE(settings.has_material());
        CHECK(settings.applied.size() == 4);
        CHECK(settings.not_applied.empty());
    }

    SECTION("the bookkeeping of the archive is reported instead of left as an unknown key")
    {
        // The review lists every key of the file, so an archive's own job records would
        // otherwise fill it with rows that say nothing.
        ForeignResinProfile profile;
        profile.source_format = "sliced-archive";
        profile.raw_values    = {{"expTime", "2.5"}, {"usedMaterial", "12.4"}, {"jobDir", "cube"}};

        const MappingResult mapping = map_resin_profile(profile, TargetPrinterClass::Tilt);
        for (const std::string& key : {"usedMaterial", "jobDir"}) {
            const auto row = std::find_if(
                mapping.report.begin(),
                mapping.report.end(),
                [&key](const Slic3r::Biz::ResinProfile::MappedField& r)
                { return r.source_key == key; }
            );
            REQUIRE(row != mapping.report.end());
            CHECK(row->status == MappingStatus::NotApplicable);
        }
    }
}

TEST_CASE(
    "The notification is offered for an archive with settings and not for one without",
    "[import][sla][archive][settings]"
)
{
    SECTION("an archive with material settings offers them")
    {
        const SlaArchiveSettings settings =
            analyze_archive_settings(review_of(archive_material_values()));
        bool used = false;

        const auto data = build_archive_settings_notification(
            settings,
            SelectionId{7},
            [&used]()
            {
                used = true;
                return true;
            }
        );

        REQUIRE(data.has_value());
        CHECK(data->type == PopNotificationType::SlaArchiveSettings);
        const PopNotificationLayoutHeaderTextButtons* layout = buttons_of(*data);
        REQUIRE(layout != nullptr);
        REQUIRE(layout->buttons.size() == 1);
        CHECK_FALSE(layout->buttons.front().text.empty());
        CHECK(layout->buttons.front().callback());
        CHECK(used);
    }

    SECTION("an archive with no material settings is not offered anything")
    {
        // The geometry still imports; there is simply nothing a review could write.
        const SlaArchiveSettings settings =
            analyze_archive_settings(review_of(archive_print_values()));
        CHECK_FALSE(settings.has_material());
        CHECK_FALSE(
            build_archive_settings_notification(settings, SelectionId{7}, []() { return true; })
                .has_value()
        );
    }

    SECTION("an empty archive is not offered anything")
    {
        const SlaArchiveSettings settings = analyze_archive_settings(review_of({}));
        CHECK_FALSE(settings.has_material());
        CHECK(settings.applied.empty());
        CHECK(settings.not_applied.empty());
    }
}

TEST_CASE(
    "The support and raft settings of an archive are named as not applied",
    "[import][sla][archive][settings]"
)
{
    const SlaArchiveSettings settings = analyze_archive_settings(
        review_of(both(archive_material_values(), archive_print_values()))
    );

    REQUIRE(settings.has_material());
    // Nothing is written for them, so they are named instead of being dropped in silence.
    CHECK(settings.not_applied.size() == 3);
    CHECK(settings.not_applied_left_out == 0);
    for (const std::string& key : {"support_type", "raft_type", "support_object_elevation"})
        CHECK(
            std::find(settings.not_applied.begin(), settings.not_applied.end(), key)
            != settings.not_applied.end()
        );

    const auto data =
        build_archive_settings_notification(settings, SelectionId{7}, []() { return true; });
    REQUIRE(data.has_value());
    const PopNotificationLayoutHeaderTextButtons* layout = buttons_of(*data);
    REQUIRE(layout != nullptr);
    CHECK(layout->text.find("support_type") != std::string::npos);
}

TEST_CASE(
    "Only a handful of not applied settings are named, the rest are counted",
    "[import][sla][archive][settings]"
)
{
    std::map<std::string, std::string> many = archive_material_values();
    const size_t total                      = max_not_applied + 4;
    for (size_t i = 0; i < total; ++i)
        many["support_setting_" + std::to_string(i)] = "1";

    const SlaArchiveSettings settings = analyze_archive_settings(review_of(many));

    CHECK(settings.has_material());
    CHECK(settings.not_applied.size() == max_not_applied);
    CHECK(settings.not_applied_left_out == 4);

    const auto data =
        build_archive_settings_notification(settings, SelectionId{7}, []() { return true; });
    REQUIRE(data.has_value());
    const PopNotificationLayoutHeaderTextButtons* layout = buttons_of(*data);
    REQUIRE(layout != nullptr);
    // The keys past the cap are not named, and the text says there are more.
    CHECK(layout->text.find("support_setting_0") != std::string::npos);
    CHECK(layout->text.find("support_setting_9") == std::string::npos);
}

TEST_CASE(
    "A value the resin preset has no label for is still named",
    "[import][sla][archive][settings]"
)
{
    ForeignResinProfile profile;
    profile.source_format = "sliced-archive";
    profile.raw_values = {{"resinDensity", "1.12"}, {"resinPrice", "25"}, {"bottleVolume", "1000"}};

    ResinImportResult result;
    result.ok      = true;
    result.mapping = map_resin_profile(profile, TargetPrinterClass::GenericMsla);

    const SlaArchiveSettings settings = analyze_archive_settings(result);
    REQUIRE(settings.has_material());
    // The density and the cost of a bottle have labels of their own, the size of the bottle has none
    // in the table, so it is named by its resin key rather than left out of the list.
    CHECK(
        std::find(settings.applied.begin(), settings.applied.end(), "bottle_volume")
        != settings.applied.end()
    );
}
