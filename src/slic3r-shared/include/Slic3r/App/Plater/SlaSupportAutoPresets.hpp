#pragma once

#include "Slic3r/App/Plater/SlaSupportPointsSettings.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

#include <string>
#include <vector>

namespace Slic3r::App::Plater {

/// How thick the band of the model that gets the heavy supports is, in layers. Two layers is the
/// first island of a pre-supported miniature (the base or the neck it is glued by) and nothing of
/// the detail above it, which is what the miniature printers ask for: a fat support where the glue
/// hides it and small ones everywhere else, so no detail is lost to a support of its own (M2.37).
constexpr double auto_support_base_layers = 2.;

/// Which preset the automatic placement gives which of the points it generated (M2.37), worked out
/// from the generated points alone: no model, no config, no scene. The caller reads the two
/// settings of "Supports & raft" into @p choice and the preset dimensions of the print preset into
/// @p presets, and this decides where they land.
struct SlaAutoSupportChoice
{
    /// support_auto_heavy_base: the supports of the lowest island of the model take the heavy
    /// preset. Off gives that island the detail preset like every other support.
    bool heavy_base{true};
    /// support_auto_detail_preset: the preset of every generated support that is not on the base.
    Domain::sla::SupportAutoDetailPreset detail{Domain::sla::SupportAutoDetailPreset::Light};
};

/// The two preset bundles of one automatic placement, the four sizes each of them configures.
struct SlaAutoSupportPresets
{
    /// What the base of the model gets, i.e. the Heavy preset of the support tool.
    SlaSupportPreset base;
    /// What every other generated support gets.
    SlaSupportPreset detail;
};

/// The preset a detail value names, as the support tool names its preset buttons: "mini", "light" or
/// "medium". Any value that is not one of them is the detail of the default (Light), which is what
/// a print preset that does not carry the key gets.
const std::string& sla_auto_detail_preset_name(Domain::sla::SupportAutoDetailPreset preset);

/// Whether @p point is on the island the model stands on: an island point no more than
/// auto_support_base_layers layers above @p lowest_z_mm, the lowest point of the model's own mesh.
/// The lift the scene draws the model by moves the model and its points together, so the height of
/// a point in the object's frame and the height of the lowest point of the mesh in the same frame
/// are the same distance no matter how high the model is drawn.
///
/// A slope point is never the base (it is the extra support of an overhang, not the part that
/// holds the model up), and a layer height of zero gives no base at all rather than every point of
/// the model as one.
bool is_sla_auto_support_base_point(
    const Domain::SLA::SupportPoint& point,
    double lowest_z_mm,
    double layer_height_mm
);

/// Sizes @p points the way the automatic placement sizes what it generates: the base of the model
/// takes @p presets.base when @p choice.heavy_base is on and every other generated point takes
/// @p presets.detail. The points the user placed or edited are never touched, so a generation never
/// resizes a support of theirs. @p points is the set the generator produced, which replaces the
/// points of the model rather than adding to them.
void sla_apply_auto_support_presets(
    Domain::SLA::SupportPoints& points,
    double lowest_z_mm,
    double layer_height_mm,
    const SlaAutoSupportPresets& presets,
    const SlaAutoSupportChoice& choice
);

} // namespace Slic3r::App::Plater
