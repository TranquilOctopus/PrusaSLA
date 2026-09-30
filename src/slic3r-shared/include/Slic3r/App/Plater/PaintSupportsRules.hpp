#pragma once

#include "Slic3r/Domain/PrinterTechnology.hpp"

#include <string>

namespace Slic3r::App::Plater {

/**
 * @brief What the paint tool is called and what its two brushes do, in the wording of a technology.
 *
 * The FFF tool paints the supports of the FFF support painting, so its brushes are "Paint" and
 * "Block". The SLA tool paints the same ModelVolume::supported_facets, but they are the input of
 * the automatic support point generator of the SLA print (M2.30), so its brushes are
 * "Paint supports" and "Block supports" and the block brush says what blocking does not do.
 */
struct PaintSupportsStrings
{
    /// Name of the tool panel.
    std::string tool_name;
    /// Left mouse button: the regions that must get a support point.
    std::string paint;
    /// Right mouse button: the regions that must not get a support point.
    std::string block;
    /// Shift and a mouse button: the paint is taken back.
    std::string remove;
    /// What the block brush does, empty when the technology has nothing to add to it.
    std::string block_hint;
};

/// The wording of the support paint tool for a technology.
PaintSupportsStrings paint_supports_strings(Domain::PrinterTechnology technology);

/**
 * @brief Whether the "Automatic painting" button of the paint dialog may be shown.
 *
 * The button paints the spots of the FFF support search into the model, which needs a slice of the
 * bed up to that step. An SLA print is never sliced from a tool or a UI action, only the
 * Slice button slices, so the button stays FFF only. The SLA support painting is hand painting: it
 * marks the plate modified, and the points of the model are built by the Auto support button of the
 * SLA supports panel, after which the M2.21 preview service draws the new tree.
 */
bool paint_supports_automatic_painting_visible(Domain::PrinterTechnology technology);

} // namespace Slic3r::App::Plater
