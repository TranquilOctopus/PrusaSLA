#pragma once

#include "Slic3r/App/Plater/SlaSupportBrace.hpp"
#include "Slic3r/App/Plater/SlaSupportGeometry.hpp"
#include "Slic3r/App/Plater/SlaSupportOnModel.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsEditing.hpp"

#include <optional>
#include <string>
#include <unordered_set>

namespace Slic3r::App::Plater {

/// Which of the two groups of the tool's support settings a change belongs to (M2.33). One field set
/// did both jobs at once, so it was never clear what a changed value was going to touch: this is
/// the split Chitubox and Lychee show, "New supports" (what a clicked point takes) and
/// "Selected supports" (what the points of the selection carry).
enum class SlaSupportSettingsGroup
{
    NewSupports,
    SelectedSupports
};

/// Every value of either group. The three sizes that a point can take from the global settings
/// instead of carrying its own have their own field for that switch.
enum class SlaSupportPointField
{
    TipDiameter,
    StemDiameter,
    BaseDiameter,
    BaseHeight,
    TipShape,
    TipLength,
    KnotDiameter,
    StemSides,
    StemTaper,
    FootShape,
    SupportOnModel,
    Bracing,
    FollowGlobalTipDiameter,
    FollowGlobalStemDiameter,
    FollowGlobalBaseDiameter,
    FollowGlobalBaseHeight
};

/// The value of a dropdown as the number the routing takes: the value of its enumeration.
double sla_support_point_field_value(Domain::SLA::SupportPoint::TipShape shape);
double sla_support_point_field_value(Domain::SLA::SupportPoint::BaseShape shape);
double sla_support_point_field_value(SupportOnModel on_model);
double sla_support_point_field_value(SupportBrace brace);

/// The four sizes of a support, in the units the tool shows them. A size of zero on a point means
/// the point follows the global setting, which is why a selection can disagree on one of them
/// without any of the points carrying a number of its own.
struct SlaSupportSizes
{
    double tip_diameter_mm{0.};
    double stem_diameter_mm{0.};
    double base_diameter_mm{0.};
    double base_height_mm{0.};

    friend bool operator==(const SlaSupportSizes& lhs, const SlaSupportSizes& rhs)
    {
        return lhs.tip_diameter_mm == rhs.tip_diameter_mm
            && lhs.stem_diameter_mm == rhs.stem_diameter_mm
            && lhs.base_diameter_mm == rhs.base_diameter_mm
            && lhs.base_height_mm == rhs.base_height_mm;
    }
};

/// Which of the four sizes a support takes from the global settings instead of carrying its own.
struct SlaSupportGlobalSizes
{
    bool tip_diameter{false};
    bool stem_diameter{false};
    bool base_diameter{false};
    bool base_height{false};
};

/// What the "Selected supports (N)" group is shown with: the geometry and the sizes the selected
/// points carry, empty where the points disagree, and which of the sizes they leave to the global
/// settings. @p count is how many points are selected, which is what the group is titled with.
struct SlaSupportSelectionView
{
    std::optional<SlaSupportGeometry> geometry;
    std::optional<SlaSupportSizes> sizes;
    SlaSupportGlobalSizes follow_global;
    /// The "may rest on the model" state of the selection, which is a value of its own (M2.26), so
    /// it is shown for a selection that agrees on it even when there is no geometry to show.
    std::optional<SupportOnModel> on_model;
    /// The same for the bracing of the pillars of the selection (M2.38).
    std::optional<SupportBrace> brace;
    size_t count{0};
};

/// What the "New supports" group is shown with: the values a clicked point takes. Unlike the
/// selection there is never a disagreement here, the next point takes exactly these.
struct SlaSupportNewValues
{
    SlaSupportGeometry geometry;
    SlaSupportSizes sizes;
    SlaSupportGlobalSizes follow_global;
    SupportOnModel on_model{SupportOnModel::Inherit};
    SupportBrace brace{SupportBrace::Inherit};
};

/// The values a clicked point takes, read off the editing state of the tool.
SlaSupportNewValues sla_new_support_values(const SlaSupportPointsEditing& editing);

/// What the selected points are shown with. Every part is empty (or false) while nothing is
/// selected, so the group can be hidden and its fields left blank.
SlaSupportSelectionView selection_support_view(const SlaSupportPointsEditing& editing);

/// The same view of any set of points, which is what the engine-side tools and the tests ask for.
SlaSupportSelectionView selection_support_view(
    const Domain::SLA::SupportPoints& points,
    const std::unordered_set<size_t>& selected_point_indices
);

/// Writes one value of the "New supports" group: what a clicked point takes from now on. The points
/// that exist keep what they carry, which is the whole point of the group (M2.33).
void sla_new_support_setting_changed(
    SlaSupportPointsEditing& editing,
    SlaSupportPointField field,
    double value
);

/// Writes one value of the "Selected supports" group on the points that are selected, one field at
/// a time so that setting the tip shape keeps the sizes a point already has. The points that are
/// not selected are not touched, and neither is what a clicked point takes (M2.33).
///
/// A FollowGlobal field takes the selected points back to the global setting (true) or gives them
/// the value the tool currently shows for that size (false).
void sla_selected_support_setting_changed(
    SlaSupportPointsEditing& editing,
    SlaSupportPointField field,
    double value
);

/// The four sizes of the Mini / Light / Medium / Heavy presets (M2.18, M2.22), as the config keys
/// of "Supports & raft" configure them.
struct SlaSupportPreset
{
    double tip_diameter_mm{0.4};
    double stem_diameter_mm{0.8};
    double base_diameter_mm{2.0};
    double base_height_mm{0.5};
};

/// The values of one preset as the config definitions ship them, which is what a print preset that
/// does not carry the keys falls back to. One of "mini", "light", "medium", "heavy".
SlaSupportPreset sla_support_preset(const std::string& preset_name);

/// The name of the preset of a preset button of either group: 0 Mini, 1 Light, 2 Medium, 3 Heavy.
const std::string& sla_support_preset_name(int preset_index);

/// A preset button of the "New supports" group: the four values a clicked point takes from now on.
/// The points that exist are not touched.
void
sla_new_support_preset_changed(SlaSupportPointsEditing& editing, const SlaSupportPreset& preset);

/// A preset button of the "Selected supports" group: the four values land on the selected points
/// and on nothing else.
void sla_selected_support_preset_changed(
    SlaSupportPointsEditing& editing,
    const SlaSupportPreset& preset
);

} // namespace Slic3r::App::Plater
