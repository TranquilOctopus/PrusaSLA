#pragma once

#include "Slic3r/App/PopNotification/PopNotificationCenter.hpp"
#include "Slic3r/App/PopNotification/PopNotificationData.hpp"
#include "Slic3r/Biz/IProjectsChangedListener.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/SLAResultCache.hpp"
#include "Slic3r/Biz/StatusCache.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/SlicingId.hpp"
#include "libslic3r/SLAResult.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::App {

/**
 * @brief How much room a wall has to stand in before a shape can be hollowed out, on top of the two
 * walls that have to fit into it.
 *
 * A hollow shape carries a wall on both of its sides, so the width a wall is offset in has to hold
 * two of them, and the offset surfaces of the engine are not exact: they come out of a voxel grid
 * and are rounded by `hollowing_closing_distance`, so the narrowest cross section of a model whose
 * two walls nearly meet is a shape whose shell closes up on itself and prints as a solid. This is
 * the room left over for that, so the rule below reads as "a shape is hollowable where a wall fits
 * twice with a margin to spare".
 *
 * One millimetre, which is a third of the 3 mm default wall and larger than the default closing
 * distance, so a shape at the rule is not one click away from being unsound.
 */
inline constexpr double hollowing_suggest_margin_mm = 1.0;

/// @brief The two numbers a suggestion is decided by, both read off the finished slice.
///
/// They are the print preset of the slice and nothing else: the wall that is asked for is the
/// `hollowing_min_thickness` the print would hollow at, and the threshold is the
/// `hollowing_suggest_min_volume` of the same config, so what the notification asks for and what
/// the hollowing tool would then do are read from one place.
struct SlaHollowingSettings
{
    /// @brief `hollowing_suggest_min_volume` in ml: a model needs at least this much resin of its
    /// own before it is worth hollowing. Zero turns the suggestion off, and no model is suggested
    /// at or below it.
    double min_volume_ml = 20.;
    /// @brief `hollowing_min_thickness` in mm: the wall a shell of the model would be printed at,
    /// and so the width that has to fit into the cross section. It is the print-level value, which
    /// is what a plate is normally printed at; a model that overrides the key for itself alone is
    /// judged by the print value, which is the one the notification asks for.
    double wall_mm = 3.;
    /// @brief The margin of hollowing_suggest_margin_mm, and nothing else. It is a field rather
    /// than a constant so that a caller can ask the narrower question in a test.
    double margin_mm = hollowing_suggest_margin_mm;
};

/**
 * @brief One model of a finished slice that would cost less resin hollowed out.
 *
 * The numbers come from the slice that was just made, so nothing here slices again to get them.
 */
struct SlaHollowingSuggestion
{
    /// The model to hollow, as the slice named it.
    Domain::ObjectID object_id{};
    /// @brief The name of the model, as the slice reported it. Empty when the slice named the model
    /// nothing, and the notification then reads without a name rather than naming a blank one.
    std::string name;
    /// @brief What the solid body of the model costs now, in ml: the cured volume of its layers,
    /// supports and raft excluded, from ObjectSliceStats::volume_mm3.
    double cured_ml = 0.;
    /// @brief What a shell of @ref wall_mm would leave out of it, in ml. An upper bound, see
    /// hollowing_saved_ml(), and rounded down by a tenth of a millilitre so a shape where the
    /// bound and the truth meet says nothing.
    double saved_ml = 0.;
    /// @brief The wall the estimate was made with, in mm: the `hollowing_min_thickness` the
    /// hollowing tool would open with.
    double wall_mm = 0.;
    /// @brief The thinnest cross section of the body at half the height, in mm: the width two
    /// walls of @ref wall_mm have to fit into. Zero means the body had no cross section there,
    /// which is read as "cannot be hollowed".
    double min_section_mm = 0.;
};

/**
 * @brief What hollowing the model would save, in ml, as an upper bound.
 *
 * A shell of @p wall_mm taken off the outside of a body costs the outline of the body multiplied
 * by the wall, layer by layer: on a layer the shell is the perimeter times the wall, and over the
 * layers the shell volume is the sum of that times the layer thickness. So the saving is the cured
 * volume of the body less that shell, which is the estimate the brief asks for and the one the
 * notification reads.
 *
 * It is an UPPER bound, and deliberately not sharpened into an exact figure: where two offset
 * surfaces meet in a corner they cut away the same resin twice, so the real saving of a 40 mm cube
 * with 3 mm walls is nearer 25 ml than the 45 ml this says. An upper bound is the safe direction
 * for a suggestion - it never tells the user a hollow print saves less than it does - and the
 * notification says "about" for the same reason. A layer whose shell would be thicker than the
 * layer itself (a sliver, where a wall cannot be stood at all) counts as no saving rather than as a
 * negative one; the eligibility rule of hollowing_suggestion_eligible() is what keeps those out of
 * the notification in the first place.
 *
 * @param stats What the slice measured for one model object.
 * @param wall_mm The wall of the shell, in mm. Zero or less saves nothing.
 */
double hollowing_saved_ml(const Biz::Slicing::Sla::ObjectSliceStats& stats, double wall_mm);

/**
 * @brief Whether a model of a finished slice is worth telling the user to hollow out.
 *
 * Four things say no, and they are the whole rule:
 *
 * - the model is printed hollow already, so there is nothing inside to take out;
 * - `hollowing_suggest_min_volume` is zero, or the model needs less resin than it says: below the
 *   threshold the resin saved is not worth the second print the hollowing costs;
 * - the model has no cross section at half its height, so its shape is not one this rule can judge
 *   (nothing of it was printed at that height, which the slice reports as zero);
 * - the thinnest cross section does not hold two walls and the margin of
 *   hollowing_suggest_margin_mm, so a shell would close on itself there. A wall is offset inwards
 *   from the surface on both sides of the shell, so the width it has to fit into is twice the wall,
 *   and the margin of hollowing_suggest_margin_mm is what is left of the offset error of the engine
 *   (see hollowing_suggest_margin_mm).
 *
 * A model that says yes is not promised anything: hollowing is the user's call and this only asks.
 *
 * @param stats What the slice measured for one model object.
 * @param settings The threshold, the wall and the margin of the slice.
 */
bool hollowing_suggestion_eligible(
    const Biz::Slicing::Sla::ObjectSliceStats& stats,
    const SlaHollowingSettings& settings
);

/**
 * @brief The models of a finished slice that are worth hollowing out, one entry each.
 *
 * In the order the slice reported them, which is the order of the plate, so the notifications come
 * up in the same order as the models on it.
 *
 * Nothing is ever suggested for a printer that is not an SLA printer: an FFF model has no wall, no
 * resin in millilitres and no `hollowing_suggest_min_volume`, and saying so here rather than in the
 * caller keeps the rule with the rest of it.
 *
 * @param object_stats One entry per model object of the slice, `SLAResultData::object_slice_stats`.
 * @param technology The printer the slice was made for.
 * @param settings The threshold, the wall and the margin of the slice.
 */
std::vector<SlaHollowingSuggestion> suggest_hollowing(
    const std::vector<Biz::Slicing::Sla::ObjectSliceStats>& object_stats,
    Domain::PrinterTechnology technology,
    const SlaHollowingSettings& settings
);

/**
 * @brief Read the suggestion settings out of the config of a finished slice.
 *
 * The config is the one the slice was made with, so a print sliced before a setting was changed
 * cannot be told about a value it was not made with. A key the config does not carry keeps the
 * value of @p fallback, which is how a slice of an older file, or of a printer with no hollowing
 * keys at all, is still judged rather than skipped.
 *
 * @param config The `SLAResultData::config` of the finished slice.
 * @param fallback What a key the config does not carry is read as.
 */
SlaHollowingSettings
hollowing_settings(const Domain::ConfigView& config, const SlaHollowingSettings& fallback = {});

/// @brief The one line of the notification of a suggestion, "<model> could be hollowed: about N ml
/// of resin saved". A model the slice named nothing reads without a name.
std::string hollowing_suggestion_text(const SlaHollowingSuggestion& suggestion);

/**
 * @brief The notification of one suggestion, or nothing when the suggestion saves too little to be
 * worth a second print.
 *
 * The button opens the hollowing tool on the model, which is where the wall is set and the drain
 * hole added; nothing is hollowed from here and nothing is sliced. Only the tool opens, so the
 * print on the plate is the print the user still has.
 *
 * Pure: it builds the data and hands it back, and the button is a plain callback, so a test reads
 * the layout back and presses the button without a window.
 *
 * @param suggestion What hollowing_suggestion_eligible() said yes to.
 * @param project_id The project the model sits in.
 * @param on_open_hollowing Called when the button is pressed. The caller selects the model and
 * opens the hollowing tool on it, and returns whether the notification should close.
 */
std::optional<PopNotification::PopNotificationData> build_hollowing_suggestion_notification(
    const SlaHollowingSuggestion& suggestion,
    Domain::SelectionId project_id,
    std::function<bool()> on_open_hollowing
);

/// @brief The saving below which a suggestion is not worth the second print it costs, in ml. A
/// tenth of a millilitre, which is below what the reader of a one decimal figure can see.
inline constexpr double hollowing_suggest_min_saving_ml = 0.1;

/**
 * @brief Tell the user which of the models of a finished slice could be hollowed to save resin.
 *
 * One notification per model, each with its own "Open hollowing" button, because the models are
 * separate decisions and the hollowing tool takes one model at a time. A plate of five big solids
 * therefore comes back with five offers rather than one list nothing could be done with.
 *
 * It listens for the end of a slice and only then, so nothing here can slice: the numbers are read
 * out of the slice that was just made, and the button opens a tool that changes the model for the
 * next slice and not this one. FFF is skipped (see suggest_hollowing()), so is a plate whose
 * threshold is zero, and so is a hollow model, a thin one and a small one.
 *
 * The suggestions are closed again when the slice they came from is gone: a new slice starting, a
 * model moving and the project closing or changing all of them, which is the same set the M4.8c
 * issue notification closes on.
 */
class SlaHollowingNotification final :
    public Biz::IStatusCacheChangedListener,
    public Biz::ISelectedProjectChangedListener,
    public Biz::IProjectsChangedListener
{
public:
    /// @brief Opens the hollowing tool on a model. Supplied by the caller because the tool is
    /// activated through the navigator, which the module owns and this class does not.
    using OpenHollowingFn = std::function<void(Domain::ObjectID)>;

    SlaHollowingNotification(
        Biz::ProjectInteractor& project_interactor,
        OpenHollowingFn open_hollowing,
        PopNotification::PopNotificationCenter& notify
    );

    ~SlaHollowingNotification() final;

    /**
     * @brief Offer what the finished slice says, or close the offers when a new slice starts.
     * @note Implementation of IStatusCacheChangedListener interface
     */
    void on_status_cache_status_code_changed(const Domain::SlicingId id) override;

    void on_status_cache_progress_changed(const Domain::SlicingId) override {}

    void on_status_cache_warnings_changed(const Domain::SlicingId) override {}

    void on_status_cache_errors_changed(const Domain::SlicingId) override {}

    /**
     * @brief Drop the offers of the project that is no longer selected.
     * @note Implementation of ISelectedProjectChangedListener interface
     */
    void on_selected_project_changed(size_t index) override;

    /**
     * @brief Drop the offers of a project that is closing.
     * @note Implementation of IProjectsChangedListener interface
     */
    void on_project_will_be_removed(Domain::SelectionId project_id) override;

    /**
     * @brief Drop the offers when the plate changes, since the numbers behind them do not hold any
     * more.
     * @note Implementation of IProjectsChangedListener interface
     */
    void on_project_changed(Domain::SelectionId project_id) override;

private:
    /// @brief The suggestions of a finished slice, and one notification for each of them.
    void offer(const Domain::SlicingId& slicing_id);

    /// @brief Whether a printable instance of the model still sits on the plate of @p project_id,
    /// which is what the hollowing tool needs to open on it.
    bool on_plate(Domain::SelectionId project_id, Domain::ObjectID object_id) const;

    /// @brief Select the model and open the hollowing tool on it, which is what the "Open hollowing"
    /// button does. Nothing is hollowed and nothing is sliced here: the tool opens on the model for
    /// the user to change, and the slice on the plate stays the one it was.
    bool select_and_open(Domain::ObjectID object_id);

    /// @brief Close every offer of this class, and answer whether there was one.
    bool close_offers();

    Biz::ProjectInteractor& m_project_interactor;
    OpenHollowingFn m_open_hollowing;
    PopNotification::PopNotificationCenter& m_notify;
    Domain::SlicingId m_current_slicing_id{};
};

} // namespace Slic3r::App
