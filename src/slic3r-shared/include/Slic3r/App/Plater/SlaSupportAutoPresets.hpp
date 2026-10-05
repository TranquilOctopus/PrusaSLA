#pragma once

#include "Slic3r/App/Plater/SlaSupportPointsSettings.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace Slic3r::App::Plater {

/// How thick the band of the model that gets the heavy supports is, in layers. Two layers is the
/// first island of a pre-supported miniature (the base or the neck it is glued by) and nothing of
/// the detail above it, which is what the miniature printers ask for: a fat support where the glue
/// hides it and small ones everywhere else, so no detail is lost to a support of its own (M2.37).
constexpr double auto_support_base_layers = 2.;

/// Which of the five preset buttons of the tool is the heavy base, i.e. the class the lowest island
/// of the model takes while support_auto_heavy_base is on: button 3, the "heavy" id, which is T0.4
/// since M7.8.1.
inline constexpr int sla_auto_heavy_base_preset_index = 3;

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

/// The two preset bundles of one automatic placement: a support class each of them (M7.8.1, R3), so
/// a generated point gets a ball contact sunk half its own tip, the cone under it, a hexagonal
/// stem and a prism base along with the four sizes the settings carry.
struct SlaAutoSupportPresets
{
    /// What the base of the model gets, i.e. the T0.4 class of the support tool. It is the class of
    /// @p classes at sla_auto_heavy_base_preset_index whenever the caller fills all five.
    SlaSupportPreset base;
    /// What every other generated point gets.
    SlaSupportPreset detail;
    /// All five tip classes of the rulebook as the print preset of the printer carries them, which
    /// is where the class of a role comes from (M7.8.8): index @p i is what
    /// sla_support_preset_name(@p i) names, so 0 is mini (T0.1) and 4 is xheavy (T0.6). They
    /// default to the values of the config definitions, which is what a print preset that carries
    /// none of the keys gets.
    std::array<SlaSupportPreset, std::size_t(sla_support_preset_count)> classes{
        sla_support_preset(sla_support_preset_name(0)),
        sla_support_preset(sla_support_preset_name(1)),
        sla_support_preset(sla_support_preset_name(2)),
        sla_support_preset(sla_support_preset_name(3)),
        sla_support_preset(sla_support_preset_name(4))
    };

    /// The values of the class a preset name asks for (sla_support_preset_name's ids, which is what
    /// rulebook_tip_class and sla_auto_detail_preset_name answer with). A name the tool does not
    /// have takes the last class, which is what sla_support_preset() does with such a name.
    const SlaSupportPreset& class_of(const std::string& preset_name) const;
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

/// Sizes @p points the way the automatic placement sizes what it generates: a point the generator
/// classified takes the tip class of its role (M7.8.8, sla_role_tip_class), and a point it did not
/// - one of a project written before the roles existed, or one from a path that does not classify -
/// takes the band rule of M2.37: the base of the model takes @p presets.base while
/// @p choice.heavy_base is on and every other generated point takes @p presets.detail.
///
/// Every point gets its whole class, geometry and four sizes, through apply_sla_support_preset() -
/// the one function a preset button uses as well, so an automatic support and a hand-placed one of
/// the same class are the same support. The points the user placed or edited are never touched, so
/// a generation never resizes a support of theirs. @p points is the set the generator produced,
/// which replaces the points of the model rather than adding to them.
void sla_apply_auto_support_presets(
    Domain::SLA::SupportPoints& points,
    double lowest_z_mm,
    double layer_height_mm,
    const SlaAutoSupportPresets& presets,
    const SlaAutoSupportChoice& choice
);

} // namespace Slic3r::App::Plater
