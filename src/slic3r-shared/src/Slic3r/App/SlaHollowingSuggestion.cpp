#include "Slic3r/App/SlaHollowingSuggestion.hpp"

#include "Slic3r/App/IsSlaActive.hpp"
#include "Slic3r/App/PopNotification/PopNotificationCenter.hpp"
#include "Slic3r/App/PopNotification/PopNotificationObservableList.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/SLAResultCache.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/Scene/Selection.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/SlicingId.hpp"

#include "fmt/format.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

namespace Slic3r::App {

using Biz::Slicing::SLAResultData;
using Biz::Slicing::Sla::ObjectSliceStats;
using Slic3r::Biz::Slicing::StatusCode;

namespace {

// A millilitre of resin is a cubic centimetre, so the volume the slice measures in mm3 is the
// volume a bottle is counted in when it is divided by a thousand.
constexpr double mm3_per_ml = 1000.;

// Long enough to read the line and decide, short enough not to pile up over the plater of a plate
// with several big solids on it.
constexpr std::chrono::seconds offer_timeout{std::chrono::seconds::rep{30}};

/// @brief A double of @p config, or nothing when the key is not there or is not a number. The
/// config of a slice is a map of resolved values, so a key the print never set is simply absent
/// rather than holding a default of its own.
std::optional<double> number_of(const Domain::ConfigView& config, const std::string& key)
{
    const auto& values = config.values();
    const auto found   = values.find(key);
    if (found == values.end() || !found->second.holds_alternative<double>())
        return std::nullopt;
    return found->second.get<double>();
}

/// @brief The model of @p object_id, or nullptr when the plate no longer holds it. A suggestion is
/// built from a slice, so a model that was removed in the meantime is simply nothing to offer.
const Domain::ModelObject* find_object(const Domain::Project& project, Domain::ObjectID object_id)
{
    for (const Domain::ModelObject* object : project.model().objects) {
        if (object != nullptr && object->id().id == object_id.id)
            return object;
    }
    return nullptr;
}

/// @brief A printable instance of @p object that sits on a bed of @p project, or nullptr. The same
/// rule as the support tool uses, because the hollowing tool works on one instance and needs it to
/// be on the plate.
const Domain::ModelInstance*
printable_instance_of(const Domain::Project& project, const Domain::ModelObject* object)
{
    if (object == nullptr)
        return nullptr;
    for (const Domain::ModelInstance* instance : object->instances) {
        if (instance == nullptr || !instance->is_printable())
            continue;
        const Domain::BedRef bed_ref = instance->get_last_bed();
        if (project.find_bed_instance_by_id(bed_ref.instance_id) != nullptr)
            return instance;
    }
    return nullptr;
}

} // namespace

double hollowing_saved_ml(const ObjectSliceStats& stats, double wall_mm)
{
    if (wall_mm <= 0.)
        return 0.;

    // The shell of one layer is the outline of the body on that layer times the wall; the shell of
    // the model is that over every layer, each of its own thickness. The area and the outline are
    // the ones the merge step measured of the body, and the thickness is the one the layer was
    // printed at, so this is read off the slice rather than estimated from the mesh again.
    const size_t layers = std::min(stats.layer_areas_mm2.size(), stats.layer_perimeters_mm.size());
    double shell_mm3    = 0.;
    for (size_t layer = 0; layer < layers; ++layer) {
        const double thickness =
            layer < stats.layer_thicknesses_mm.size() ? stats.layer_thicknesses_mm[layer] : 0.f;
        if (thickness <= 0.)
            continue;
        // A layer whose wall would be wider than the layer itself is a sliver a wall cannot be
        // stood in, so it saves nothing rather than a negative amount.
        const double shell_mm2 =
            std::max(0., stats.layer_areas_mm2[layer] - stats.layer_perimeters_mm[layer] * wall_mm);
        shell_mm3 += shell_mm2 * thickness;
    }
    return shell_mm3 / mm3_per_ml;
}

bool
hollowing_suggestion_eligible(const ObjectSliceStats& stats, const SlaHollowingSettings& settings)
{
    // Already hollow: there is nothing solid inside to take out, and hollowing it again would only
    // make the walls thicker than the user asked for.
    if (stats.hollowed)
        return false;
    // The switch: zero threshold means the user turned the suggestion off, and no model of any
    // size is then suggested.
    if (settings.min_volume_ml <= 0.)
        return false;
    if (stats.volume_mm3 < settings.min_volume_ml * mm3_per_ml)
        return false;
    // No cross section at half the height means the slice could not judge the shape, so nothing is
    // said about it rather than something being guessed.
    if (stats.min_section_mm <= 0.)
        return false;
    // Two walls and the margin of the offset error have to fit into the narrowest cross section.
    if (stats.min_section_mm <= 2. * settings.wall_mm + settings.margin_mm)
        return false;
    return hollowing_saved_ml(stats, settings.wall_mm) >= hollowing_suggest_min_saving_ml;
}

std::vector<SlaHollowingSuggestion> suggest_hollowing(
    const std::vector<ObjectSliceStats>& object_stats,
    Domain::PrinterTechnology technology,
    const SlaHollowingSettings& settings
)
{
    std::vector<SlaHollowingSuggestion> out;
    if (technology != Domain::PrinterTechnology::SLA)
        return out; // an FFF model has no wall and no resin in millilitres

    out.reserve(object_stats.size());
    for (const ObjectSliceStats& stats : object_stats) {
        if (!hollowing_suggestion_eligible(stats, settings))
            continue;
        SlaHollowingSuggestion suggestion;
        suggestion.object_id      = stats.object_id;
        suggestion.name           = stats.name;
        suggestion.cured_ml       = stats.volume_mm3 / mm3_per_ml;
        suggestion.saved_ml       = hollowing_saved_ml(stats, settings.wall_mm);
        suggestion.wall_mm        = settings.wall_mm;
        suggestion.min_section_mm = stats.min_section_mm;
        out.push_back(std::move(suggestion));
    }
    return out;
}

SlaHollowingSettings
hollowing_settings(const Domain::ConfigView& config, const SlaHollowingSettings& fallback)
{
    SlaHollowingSettings settings = fallback;
    // The two keys of the print the slice was made with. Read in the spelling the domain config
    // uses, which is the one ConfigDefsSLA.cpp declares them under.
    if (const std::optional<double> min_volume = number_of(config, "hollowing_suggest_min_volume"))
        settings.min_volume_ml = *min_volume;
    if (const std::optional<double> wall = number_of(config, "hollowing_min_thickness"))
        settings.wall_mm = *wall;
    return settings;
}

std::string hollowing_suggestion_text(const SlaHollowingSuggestion& suggestion)
{
    // "about" because the number is an upper bound of the shell the tool would remove (see
    // hollowing_saved_ml()) and the corners of a shape are counted twice in it.
    if (suggestion.name.empty()) {
        // TRN: The line of a notification that a model of the plate could be hollowed to save
        // resin, for a model the slice named nothing. {0} is the resin saved in millilitres, one
        // decimal, and the leading text is kept apart so a translator can name the model where the
        // grammar of the sentence wants it.
        return fmt::format(
            fmt::runtime(
                Biz::_u8L("A model on the plate could be hollowed: about {0} ml of resin saved")
            ),
            suggestion.saved_ml
        );
    }
    // TRN: The line of a notification that a model of the plate could be hollowed to save resin.
    // {0} is the name of the model and {1} the resin saved in millilitres, one decimal.
    return fmt::format(
        fmt::runtime(Biz::_u8L("{0} could be hollowed: about {1} ml of resin saved")),
        suggestion.name,
        suggestion.saved_ml
    );
}

std::optional<PopNotification::PopNotificationData> build_hollowing_suggestion_notification(
    const SlaHollowingSuggestion& suggestion,
    Domain::SelectionId project_id,
    std::function<bool()> on_open_hollowing
)
{
    if (suggestion.saved_ml < hollowing_suggest_min_saving_ml)
        return std::nullopt;

    using namespace PopNotification;
    return PopNotificationData{
        .type    = PopNotificationType::SlaHollowingSuggestion,
        .level   = PopNotificationLevel::Regular,
        .timeout = offer_timeout,
        .layout =
            PopNotificationLayoutTextButtons{
                .text    = hollowing_suggestion_text(suggestion),
                .buttons = {PopNotificationButtonData{
                    // TRN: The button of the notification that a model could be hollowed. It opens the
                    // hollowing tool on that model, and hollows nothing by itself.
                    .text     = Biz::_u8L("Open hollowing"),
                    .callback = std::move(on_open_hollowing)
                }},
                .icon = Render::Icon::None
            },
        .project_id = project_id,
    };
}

SlaHollowingNotification::SlaHollowingNotification(
    Biz::ProjectInteractor& project_interactor,
    OpenHollowingFn open_hollowing,
    PopNotification::PopNotificationCenter& notify
) :
    m_project_interactor(project_interactor),
    m_open_hollowing(std::move(open_hollowing)),
    m_notify(notify)
{
    m_project_interactor.status_cache().add_listener<Biz::IStatusCacheChangedListener>(this);
    m_project_interactor.add_listener<Biz::ISelectedProjectChangedListener>(this);
    m_project_interactor.add_listener<Biz::IProjectsChangedListener>(this);
}

SlaHollowingNotification::~SlaHollowingNotification()
{
    close_offers();
    m_project_interactor.status_cache().remove_listener<Biz::IStatusCacheChangedListener>(this);
    m_project_interactor.remove_listener<Biz::ISelectedProjectChangedListener>(this);
    m_project_interactor.remove_listener<Biz::IProjectsChangedListener>(this);
}

void SlaHollowingNotification::on_status_cache_status_code_changed(const Domain::SlicingId id)
{
    // Only the project the user is looking at: an offer about another plate is noise.
    if (id.project_id != m_project_interactor.selected_project_id())
        return;

    const std::optional<Biz::Slicing::Status> status =
        m_project_interactor.status_cache().get_status(id);
    if (!status.has_value())
        return;

    if (status->code == StatusCode::Finished) {
        m_current_slicing_id = id;
        offer(id);
        return;
    }

    // Anything else means the slice the offers came from is no longer the slice of the plate, so
    // the numbers behind the lines are stale. This is the same set of codes the M4.8c issue
    // notification closes on.
    if (status->code == StatusCode::Running
        || status->code == StatusCode::Modified
        || status->code == StatusCode::Updating
        || status->code == StatusCode::InvalidData
        || status->code == StatusCode::Empty
        || status->code == StatusCode::Removed)
    {
        close_offers();
        if (m_current_slicing_id == id)
            m_current_slicing_id = Domain::SlicingId{};
    }
}

void SlaHollowingNotification::on_selected_project_changed(size_t index)
{
    (void) index;
    close_offers();
    m_current_slicing_id = Domain::SlicingId{};
}

void SlaHollowingNotification::on_project_will_be_removed(Domain::SelectionId project_id)
{
    (void) project_id;
    close_offers();
    m_current_slicing_id = Domain::SlicingId{};
}

void SlaHollowingNotification::on_project_changed(Domain::SelectionId project_id)
{
    (void) project_id;
    close_offers();
    if (m_current_slicing_id.project_id == project_id)
        m_current_slicing_id = Domain::SlicingId{};
}

void SlaHollowingNotification::offer(const Domain::SlicingId& slicing_id)
{
    const std::optional<Biz::SLAResultRef> result =
        m_project_interactor.sla_result_cache().get_result(slicing_id);
    if (!result.has_value() || !result->get().export_data)
        return;

    // FFF is skipped before the config is read: an FFF config carries neither of the two keys, so
    // it would be judged against the defaults and a 40 mm cube would be offered as an SLA model.
    if (!is_sla_active(m_project_interactor))
        return;

    // The settings come off the config of this very slice, so a print sliced before a setting was
    // changed is never judged against a value it was not made with.
    const SLAResultData& data           = *result->get().export_data;
    const SlaHollowingSettings settings = hollowing_settings(data.config);
    const std::vector<SlaHollowingSuggestion> suggestions =
        suggest_hollowing(data.object_slice_stats, Domain::PrinterTechnology::SLA, settings);
    if (suggestions.empty())
        return;

    // The offers of the previous slice go first, so a plate that is re-sliced with one model
    // hollowed does not keep the line of a model that no longer needs it.
    close_offers();

    for (const SlaHollowingSuggestion& suggestion : suggestions) {
        // A model that has left the plate in the meantime, or whose instances have all come off
        // it, has no tool to open, so nothing is offered for it.
        if (!on_plate(slicing_id.project_id, suggestion.object_id))
            continue;

        const Domain::ObjectID object_id = suggestion.object_id;
        const std::optional<PopNotification::PopNotificationData> notification =
            build_hollowing_suggestion_notification(
                suggestion,
                slicing_id.project_id,
                [this, object_id]() { return select_and_open(object_id); }
            );
        if (!notification.has_value())
            continue;

        // A new notification every time: one line per model, each with its own button, because
        // the models are separate decisions and the hollowing tool takes one model at a time.
        m_notify
            .upsert_notification(std::move(*notification), PopNotification::never_equal_matcher);
    }
}

bool
SlaHollowingNotification::on_plate(Domain::SelectionId project_id, Domain::ObjectID object_id) const
{
    const Domain::Project& project = m_project_interactor.project(project_id);
    return printable_instance_of(project, find_object(project, object_id)) != nullptr;
}

bool SlaHollowingNotification::select_and_open(Domain::ObjectID object_id)
{
    const Domain::Project& project = m_project_interactor.selected_project();
    const Domain::ModelInstance* instance =
        printable_instance_of(project, find_object(project, object_id));
    if (instance == nullptr)
        return false; // the model left the plate between the offer and the button

    // The selection comes before the tool: the hollowing tool is enabled by a whole instance being
    // selected, the same way "Edit supports" selects the models before it opens the support tool.
    m_project_interactor.scene_interactor().set_object_selection(
        {Biz::Scene::SelectionMode::Instance, {Domain::ElementRef{object_id.id, instance->id().id}}}
    );
    m_open_hollowing(object_id);
    return true;
}

bool SlaHollowingNotification::close_offers()
{
    PopNotification::PopNotificationObservableList& list = m_notify.observable_list();
    bool open                                            = false;
    for (size_t i = 0; i < list.size(); i++) {
        if (list.at(i).type == PopNotification::PopNotificationType::SlaHollowingSuggestion) {
            open = true;
            break;
        }
    }
    if (!open)
        return false;
    list.close_notifications_of_type(PopNotification::PopNotificationType::SlaHollowingSuggestion);
    return true;
}

} // namespace Slic3r::App
