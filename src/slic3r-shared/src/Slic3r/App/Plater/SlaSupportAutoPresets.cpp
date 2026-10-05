#include "Slic3r/App/Plater/SlaSupportAutoPresets.hpp"

#include "Slic3r/App/Plater/SlaSupportRoles.hpp"

#include <cstddef>
#include <optional>
#include <string>

namespace Slic3r::App::Plater {

using Domain::SLA::SupportPoint;
using Domain::SLA::SupportPoints;
using Domain::SLA::SupportPointType;

const std::string& sla_auto_detail_preset_name(Domain::sla::SupportAutoDetailPreset preset)
{
    // The order is the order of the preset buttons of the tool, so a value that is not one of the
    // three the key offers (a preset of a build that knew more of them) is the default rather than
    // nothing.
    switch (preset) {
    case Domain::sla::SupportAutoDetailPreset::Mini:
        return sla_support_preset_name(0);
    case Domain::sla::SupportAutoDetailPreset::Medium:
        return sla_support_preset_name(2);
    case Domain::sla::SupportAutoDetailPreset::Light:
    default:
        return sla_support_preset_name(1);
    }
}

bool is_sla_auto_support_base_point(
    const SupportPoint& point,
    double lowest_z_mm,
    double layer_height_mm
)
{
    // Only an island point holds the model up; a slope point is the extra support of an overhang.
    if (!point.is_island()) {
        return false;
    }

    // A layer height of nothing would make every point of the model the base of it. The generator
    // always has the layer height it sampled at, so this only covers a caller that has none.
    if (!(layer_height_mm > 0.)) {
        return false;
    }

    return static_cast<double>(point.pos.z())
        <= lowest_z_mm + auto_support_base_layers * layer_height_mm;
}

const SlaSupportPreset& SlaAutoSupportPresets::class_of(const std::string& preset_name) const
{
    for (std::size_t index = 0; index < classes.size(); ++index) {
        if (sla_support_preset_name(static_cast<int>(index)) == preset_name) {
            return classes[index];
        }
    }
    // A name the tool has no button for is the largest class, as it is in sla_support_preset().
    return classes.back();
}

bool sla_auto_support_is_detailed(const SupportPoint& point)
{
    // The role the generator gave the point is what says it stands in a detailed region: the engine
    // measured the surface around it, so nothing here has to look at the model again. It is the one
    // reason R4.9 gives for the minimum tip, and the anchor of the lowest island is not Detail at
    // all, so the base of the model keeps its heavy class on relief as well as on a plate (R4.1).
    return point.role == SupportPoint::Role::Detail;
}

void sla_apply_auto_support_presets(
    SupportPoints& points,
    double lowest_z_mm,
    double layer_height_mm,
    const SlaAutoSupportPresets& presets,
    const SlaAutoSupportChoice& choice
)
{
    for (SupportPoint& point : points) {
        // The automatic placement sizes the points it generated and nothing else. The generator
        // writes island and slope points, so a point of this set is never one the user placed; the
        // check keeps it that way even if a future generator (or a caller that hands the model's
        // own points in) carries one.
        if (point.type == SupportPointType::manual_add) {
            continue;
        }

        // A point the generator classified takes the whole tip class of its role (M7.8.8, the rule
        // in sla_role_tip_class), the two settings of M2.37 deciding what a role is given.
        if (const std::optional<std::string> tip_class =
                sla_role_tip_class(point.role, choice.heavy_base, choice.detail);
            tip_class.has_value())
        {
            apply_sla_support_preset(point, presets.class_of(*tip_class));
            continue;
        }

        // A point with no role keeps the band rule of M2.37: the lowest island heavy, everything
        // else the detail preset.
        const bool is_base = is_sla_auto_support_base_point(point, lowest_z_mm, layer_height_mm);

        // R4.9 (M7.8.5): a point in a detailed region takes the minimum class, except where it is
        // the anchor of the lowest island, which the heavy class of R4.1 stays with.
        if (!is_base && sla_auto_support_is_detailed(point)) {
            apply_sla_support_preset(point, presets.minimum);
            continue;
        }

        apply_sla_support_preset(
            point,
            is_base && choice.heavy_base ? presets.base : presets.detail
        );
    }
}

} // namespace Slic3r::App::Plater
