#include "Slic3r/App/Plater/SlaSupportPointsSettings.hpp"

#include <algorithm>
#include <cstdint>

namespace Slic3r::App::Plater {

using Domain::SLA::SupportPoint;
using Domain::SLA::SupportPoints;

// The preset values of the config definitions, in mm: support_preset_{mini,light,medium,heavy}_*
// (M2.18, M2.22). They are what the tool falls back to for a print preset that does not carry them.
static constexpr double MINI_TIP_DIAMETER_MM    = 0.2;
static constexpr double MINI_STEM_DIAMETER_MM   = 0.5;
static constexpr double MINI_BASE_DIAMETER_MM   = 1.4;
static constexpr double MINI_BASE_HEIGHT_MM     = 0.4;
static constexpr double LIGHT_TIP_DIAMETER_MM   = 0.30;
static constexpr double LIGHT_STEM_DIAMETER_MM  = 0.8;
static constexpr double LIGHT_BASE_DIAMETER_MM  = 2.0;
static constexpr double LIGHT_BASE_HEIGHT_MM    = 0.5;
static constexpr double MEDIUM_TIP_DIAMETER_MM  = 0.45;
static constexpr double MEDIUM_STEM_DIAMETER_MM = 1.2;
static constexpr double MEDIUM_BASE_DIAMETER_MM = 3.0;
static constexpr double MEDIUM_BASE_HEIGHT_MM   = 0.7;
static constexpr double HEAVY_TIP_DIAMETER_MM   = 0.60;
static constexpr double HEAVY_STEM_DIAMETER_MM  = 1.8;
static constexpr double HEAVY_BASE_DIAMETER_MM  = 4.0;
static constexpr double HEAVY_BASE_HEIGHT_MM    = 1.0;

double sla_support_point_field_value(SupportPoint::TipShape shape)
{
    return static_cast<double>(shape);
}

double sla_support_point_field_value(SupportPoint::BaseShape shape)
{
    return static_cast<double>(shape);
}

double sla_support_point_field_value(SupportOnModel on_model)
{
    return static_cast<double>(on_model);
}

namespace {

/// The one of the per-point geometry fields a field of the tool is, for the fields that are one.
/// The four sizes and the two states have no geometry field of their own.
SupportGeometryField geometry_field_of(SlaSupportPointField field)
{
    switch (field) {
    case SlaSupportPointField::TipDiameter:
        return SupportGeometryField::TipDiameter;
    case SlaSupportPointField::TipShape:
        return SupportGeometryField::TipShape;
    case SlaSupportPointField::TipLength:
        return SupportGeometryField::TipLength;
    case SlaSupportPointField::KnotDiameter:
        return SupportGeometryField::KnotDiameter;
    case SlaSupportPointField::StemSides:
        return SupportGeometryField::StemSides;
    case SlaSupportPointField::StemTaper:
        return SupportGeometryField::StemTaper;
    case SlaSupportPointField::FootShape:
        return SupportGeometryField::BaseShape;
    default:
        return SupportGeometryField::TipDiameter;
    }
}

void set_geometry_field(SlaSupportGeometry& geometry, SupportGeometryField field, double value)
{
    switch (field) {
    case SupportGeometryField::TipDiameter:
        geometry.tip_diameter_mm = value;
        break;
    case SupportGeometryField::TipShape:
        geometry.tip_shape = static_cast<SupportPoint::TipShape>(static_cast<int>(value));
        break;
    case SupportGeometryField::TipLength:
        geometry.tip_length_mm = value;
        break;
    case SupportGeometryField::KnotDiameter:
        geometry.knot_diameter_mm = value;
        break;
    case SupportGeometryField::StemSides:
        geometry.stem_sides = static_cast<int>(std::clamp(value, 0., 255.));
        break;
    case SupportGeometryField::StemTaper:
        geometry.stem_taper = value;
        break;
    case SupportGeometryField::BaseShape:
        geometry.base_shape = static_cast<SupportPoint::BaseShape>(static_cast<int>(value));
        break;
    }
}

SlaSupportGeometry geometry_of_field(SupportGeometryField field, double value)
{
    SlaSupportGeometry geometry;
    set_geometry_field(geometry, field, value);
    return geometry;
}

SlaSupportSizes sizes_of(const SupportPoint& point)
{
    SlaSupportSizes sizes;
    // The tool works in diameters, the point stores the radius of the tip.
    sizes.tip_diameter_mm  = 2. * static_cast<double>(point.head_front_radius);
    sizes.stem_diameter_mm = point.pillar_diameter;
    sizes.base_diameter_mm = point.base_diameter;
    sizes.base_height_mm   = point.base_height;
    return sizes;
}

SupportOnModel on_model_of(double value)
{
    return static_cast<SupportOnModel>(static_cast<int>(value));
}

} // namespace

SlaSupportNewValues sla_new_support_values(const SlaSupportPointsEditing& editing)
{
    SlaSupportNewValues values;
    values.geometry                    = editing.support_geometry;
    values.sizes.tip_diameter_mm       = editing.support_geometry.tip_diameter_mm;
    values.sizes.stem_diameter_mm      = editing.stem_diameter_mm;
    values.sizes.base_diameter_mm      = editing.base_diameter_mm;
    values.sizes.base_height_mm        = editing.base_height_mm;
    values.follow_global.tip_diameter  = editing.head_diameter_use_global;
    values.follow_global.stem_diameter = editing.pillar_diameter_use_global;
    values.follow_global.base_diameter = editing.base_diameter_use_global;
    values.follow_global.base_height   = editing.base_height_use_global;
    values.on_model                    = editing.new_support_on_model;
    return values;
}

SlaSupportSelectionView selection_support_view(const SlaSupportPointsEditing& editing)
{
    return selection_support_view(editing.points, editing.selected_point_indices);
}

SlaSupportSelectionView selection_support_view(
    const SupportPoints& points,
    const std::unordered_set<size_t>& selected_point_indices
)
{
    SlaSupportSelectionView view;
    view.geometry = selection_support_geometry(points, selected_point_indices);
    view.on_model = selection_support_on_model(points, selected_point_indices);
    view.count    = selected_point_indices.size();

    std::optional<SlaSupportSizes> common_sizes;
    bool sizes_agree                  = true;
    bool stem_follows_global          = true;
    bool base_diameter_follows_global = true;
    bool base_height_follows_global   = true;
    for (size_t idx : selected_point_indices) {
        if (idx >= points.size()) {
            continue;
        }
        const SlaSupportSizes sizes = sizes_of(points[idx]);
        if (!common_sizes.has_value()) {
            common_sizes = sizes;
        } else if (!(*common_sizes == sizes)) {
            // The points of the selection disagree, so there is no size to show.
            sizes_agree = false;
        }
        // A size of zero on a point means it follows the global setting, so one point with a value of
        // its own is enough for the whole selection to disagree about it.
        stem_follows_global = stem_follows_global && points[idx].pillar_diameter == 0.f;
        base_diameter_follows_global =
            base_diameter_follows_global && points[idx].base_diameter == 0.f;
        base_height_follows_global = base_height_follows_global && points[idx].base_height == 0.f;
    }

    view.sizes                       = sizes_agree ? common_sizes : std::nullopt;
    const bool has_selection         = !selected_point_indices.empty();
    view.follow_global.stem_diameter = has_selection && stem_follows_global;
    view.follow_global.base_diameter = has_selection && base_diameter_follows_global;
    view.follow_global.base_height   = has_selection && base_height_follows_global;
    // The tip diameter is the head radius of the point, so a zero of it is the tree's own pinhead.
    view.follow_global.tip_diameter =
        has_selection && view.geometry.has_value() && view.geometry->tip_diameter_mm == 0.;

    return view;
}

void sla_new_support_setting_changed(
    SlaSupportPointsEditing& editing,
    SlaSupportPointField field,
    double value
)
{
    // Nothing here touches a point: this is what the next clicked point takes.
    switch (field) {
    case SlaSupportPointField::TipDiameter:
    case SlaSupportPointField::TipShape:
    case SlaSupportPointField::TipLength:
    case SlaSupportPointField::KnotDiameter:
    case SlaSupportPointField::StemSides:
    case SlaSupportPointField::StemTaper:
    case SlaSupportPointField::FootShape:
        set_geometry_field(editing.support_geometry, geometry_field_of(field), value);
        break;
    case SlaSupportPointField::StemDiameter:
        editing.stem_diameter_mm = value;
        // A number the user typed is a number the point takes, so this size stops following the
        // global settings. It is what the single control of before did, and the box next to the
        // slider is unchecked right after (M2.33).
        editing.pillar_diameter_use_global = false;
        break;
    case SlaSupportPointField::BaseDiameter:
        editing.base_diameter_mm         = value;
        editing.base_diameter_use_global = false;
        break;
    case SlaSupportPointField::BaseHeight:
        editing.base_height_mm         = value;
        editing.base_height_use_global = false;
        break;
    case SlaSupportPointField::SupportOnModel:
        editing.new_support_on_model = on_model_of(value);
        break;
    case SlaSupportPointField::FollowGlobalTipDiameter:
        editing.head_diameter_use_global = value != 0.;
        break;
    case SlaSupportPointField::FollowGlobalStemDiameter:
        editing.pillar_diameter_use_global = value != 0.;
        break;
    case SlaSupportPointField::FollowGlobalBaseDiameter:
        editing.base_diameter_use_global = value != 0.;
        break;
    case SlaSupportPointField::FollowGlobalBaseHeight:
        editing.base_height_use_global = value != 0.;
        break;
    }
}

void sla_selected_support_setting_changed(
    SlaSupportPointsEditing& editing,
    SlaSupportPointField field,
    double value
)
{
    if (editing.selected_point_indices.empty()) {
        return;
    }

    // What the tool shows for a size is what a point takes back when it stops following the global
    // setting, so the two read the same numbers.
    const SlaSupportSizes global_sizes = sla_new_support_values(editing).sizes;
    const bool follow_global           = value != 0.;

    for (size_t idx : editing.selected_point_indices) {
        if (idx >= editing.points.size()) {
            continue;
        }
        SupportPoint& point = editing.points[idx];
        switch (field) {
        case SlaSupportPointField::TipDiameter:
            apply_support_geometry(
                point,
                geometry_of_field(SupportGeometryField::TipDiameter, value),
                SupportGeometryField::TipDiameter
            );
            break;
        case SlaSupportPointField::StemDiameter:
            point.pillar_diameter = static_cast<float>(value);
            break;
        case SlaSupportPointField::BaseDiameter:
            point.base_diameter = static_cast<float>(value);
            break;
        case SlaSupportPointField::BaseHeight:
            point.base_height = static_cast<float>(value);
            break;
        case SlaSupportPointField::TipShape:
        case SlaSupportPointField::TipLength:
        case SlaSupportPointField::KnotDiameter:
        case SlaSupportPointField::StemSides:
        case SlaSupportPointField::StemTaper:
        case SlaSupportPointField::FootShape: {
            const SupportGeometryField geometry_field = geometry_field_of(field);
            apply_support_geometry(point, geometry_of_field(geometry_field, value), geometry_field);
            break;
        }
        case SlaSupportPointField::SupportOnModel:
            point.on_model = on_model_of(value);
            break;
        case SlaSupportPointField::FollowGlobalTipDiameter:
            apply_support_geometry(
                point,
                geometry_of_field(
                    SupportGeometryField::TipDiameter,
                    follow_global ? 0. : global_sizes.tip_diameter_mm
                ),
                SupportGeometryField::TipDiameter
            );
            break;
        case SlaSupportPointField::FollowGlobalStemDiameter:
            point.pillar_diameter =
                static_cast<float>(follow_global ? 0. : global_sizes.stem_diameter_mm);
            break;
        case SlaSupportPointField::FollowGlobalBaseDiameter:
            point.base_diameter =
                static_cast<float>(follow_global ? 0. : global_sizes.base_diameter_mm);
            break;
        case SlaSupportPointField::FollowGlobalBaseHeight:
            point.base_height =
                static_cast<float>(follow_global ? 0. : global_sizes.base_height_mm);
            break;
        }
    }
}

SlaSupportPreset sla_support_preset(const std::string& preset_name)
{
    if (preset_name == "mini") {
        return {
            MINI_TIP_DIAMETER_MM,
            MINI_STEM_DIAMETER_MM,
            MINI_BASE_DIAMETER_MM,
            MINI_BASE_HEIGHT_MM
        };
    }
    if (preset_name == "light") {
        return {
            LIGHT_TIP_DIAMETER_MM,
            LIGHT_STEM_DIAMETER_MM,
            LIGHT_BASE_DIAMETER_MM,
            LIGHT_BASE_HEIGHT_MM
        };
    }
    if (preset_name == "medium") {
        return {
            MEDIUM_TIP_DIAMETER_MM,
            MEDIUM_STEM_DIAMETER_MM,
            MEDIUM_BASE_DIAMETER_MM,
            MEDIUM_BASE_HEIGHT_MM
        };
    }
    return {
        HEAVY_TIP_DIAMETER_MM,
        HEAVY_STEM_DIAMETER_MM,
        HEAVY_BASE_DIAMETER_MM,
        HEAVY_BASE_HEIGHT_MM
    };
}

const std::string& sla_support_preset_name(int preset_index)
{
    static const std::string names[4]{"mini", "light", "medium", "heavy"};
    return names[std::clamp(preset_index, 0, 3)];
}

void
sla_new_support_preset_changed(SlaSupportPointsEditing& editing, const SlaSupportPreset& preset)
{
    editing.support_geometry.tip_diameter_mm = preset.tip_diameter_mm;
    editing.stem_diameter_mm                 = preset.stem_diameter_mm;
    editing.base_diameter_mm                 = preset.base_diameter_mm;
    editing.base_height_mm                   = preset.base_height_mm;
    // A preset is a bundle of values of its own, so a point placed from now on carries them instead
    // of following the global settings.
    editing.head_diameter_use_global   = false;
    editing.pillar_diameter_use_global = false;
    editing.base_diameter_use_global   = false;
    editing.base_height_use_global     = false;
}

void sla_selected_support_preset_changed(
    SlaSupportPointsEditing& editing,
    const SlaSupportPreset& preset
)
{
    if (editing.selected_point_indices.empty()) {
        return;
    }

    // What a clicked point takes is not touched: the preset of the "Selected supports" group is an
    // edit of the selection like any other value of it.
    for (size_t idx : editing.selected_point_indices) {
        if (idx >= editing.points.size()) {
            continue;
        }
        apply_support_geometry(
            editing.points[idx],
            geometry_of_field(SupportGeometryField::TipDiameter, preset.tip_diameter_mm),
            SupportGeometryField::TipDiameter
        );
        editing.points[idx].pillar_diameter = static_cast<float>(preset.stem_diameter_mm);
        editing.points[idx].base_diameter   = static_cast<float>(preset.base_diameter_mm);
        editing.points[idx].base_height     = static_cast<float>(preset.base_height_mm);
    }
}

} // namespace Slic3r::App::Plater
