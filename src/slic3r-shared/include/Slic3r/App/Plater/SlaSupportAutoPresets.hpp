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
    /// preset, which is the T0.4 class of the rulebook (the "heavy" id). Off gives that island the
    /// detail preset like every other support.
    bool heavy_base{true};
    /// support_auto_detail_preset: the preset of every generated support that is not on the base,
    /// named by the size of its contact since M7.8.1: Mini is T0.1, Light is T0.2 and Medium is
    /// T0.3. The setting keeps the ids the support tool names its presets by, so the key of an old
    /// project still means what it meant.
    Domain::sla::SupportAutoDetailPreset detail{Domain::sla::SupportAutoDetailPreset::Light};
};

/// The preset bundles of one automatic placement: a support class each of them (M7.8.1, R3), so
/// a generated point gets a ball contact sunk half its own tip, the cone under it, a hexagonal
/// stem and a prism base along with the four sizes the settings carry. All three are read as they
/// are handed in, so a caller fills every one of them.
struct SlaAutoSupportPresets
{
    /// What the base of the model gets, i.e. the T0.4 class of the support tool.
    SlaSupportPreset base;
    /// What every other generated support gets.
    SlaSupportPreset detail;
    /// What a generated point in a detailed region gets (M7.8.5, R4.9): the T0.1 class, which is
    /// preset button 1 of the tool. It is a bundle of its own rather than a smaller `detail`, since
    /// R4.9 asks for the minimum tip whatever the role of the point would have given it and whatever
    /// support_auto_detail_preset says, and only for a reason of its own.
    SlaSupportPreset minimum;
};

/// Whether a generated point takes the minimum class for the reason of its role and not for where it
/// is (M7.8.5, R4.9): a point in a detailed region does, and that is the only role that does. The
/// anchor of the lowest island is the one role R4.9 leaves alone, so a point of the base keeps the
/// heavy class whatever the surface under it is like (R4.1), and a fragile point is not Detail
/// either - which class a fragile support takes is the mapping of M7.8.2 (rulebook_tip_class), not
/// this rule.
bool sla_auto_support_is_detailed(const Domain::SLA::SupportPoint& point);

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
/// takes @p presets.base when @p choice.heavy_base is on, a point in a detailed region takes
/// @p presets.minimum (R4.9, M7.8.5) unless it is on the base, and every other generated point
/// takes @p presets.detail. Every point gets its whole class, geometry and four sizes, through
/// apply_sla_support_preset() - the one function a preset button uses as well, so an automatic
/// support and a hand-placed one of the same class are the same support. The points the user placed
/// or edited are never touched, so a generation never resizes a support of theirs. @p points is the
/// set the generator produced, which replaces the points of the model rather than adding to them.
void sla_apply_auto_support_presets(
    Domain::SLA::SupportPoints& points,
    double lowest_z_mm,
    double layer_height_mm,
    const SlaAutoSupportPresets& presets,
    const SlaAutoSupportChoice& choice
);

} // namespace Slic3r::App::Plater
