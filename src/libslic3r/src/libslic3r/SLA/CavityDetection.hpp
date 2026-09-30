#pragma once

#include <cstddef>
#include <vector>

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::SLA {

/// A suction cup (PLAN B5b): a region of empty space that is completely enclosed by solid in its
/// layer (a hole of an ExPolygon of the model slice) and stays enclosed going upward until a
/// layer covers it, an upside-down pocket that pins a vacuum against the vat film on every peel.
struct CupHit
{
    size_t first_layer = 0; //< lowest layer the cup is a hole of, the layer with the opening
    size_t last_layer  = 0; //< highest layer the cup is a hole of, the layer below the roof
    //< centre of the opening onto the vat, in mm
    Domain::Vec2d opening_centroid = Domain::Vec2d::Zero();
    //< the opening onto the vat, the part of the first layer's region with no solid under it, in mm²
    double opening_area_mm2 = 0.;
    //< the hole area times the layer thickness, summed over the layers, in mm³
    double volume_mm3 = 0.;
};

/// Trapped resin (PLAN B5b): an enclosed cavity with no path to the outside in 3D. A hole that
/// starts with solid under it and ends with solid over it holds resin that never gets to the vat,
/// which is where hollow prints without a drain hole put it.
struct TrappedResinHit
{
    size_t first_layer     = 0;
    size_t last_layer      = 0;
    Domain::Vec2d centroid = Domain::Vec2d::Zero(); //< centre of the cavity footprint, in mm
    double volume_mm3      = 0.;
};

struct CavityDetectionOptions
{
    /// A pocket closed above and open below through less than this is not reported: the opening is
    /// too small to seal against the vat film, so it is no cup, and it connects the pocket to the
    /// vat, so the resin is not trapped either. 1 mm² by default.
    double min_cup_opening_mm2 = 1.;

    /// Cavities holding less resin than this are ignored. 1 mm³ by default.
    double min_trapped_volume_mm3 = 1.;
};

struct CavityAnalysis
{
    std::vector<CupHit> cups;
    std::vector<TrappedResinHit> trapped_resin;
};

/**
 * @brief Find suction cups and trapped resin on the sliced layers.
 *
 * @param layers The merged slice of every printed layer (model, supports and pad), in scaled
 *        coordinates, bottom layer first.
 * @param layer_thicknesses_mm The thickness of each layer in mm, as many entries as layers. The
 *        layers are counted from the plate, so the sum of the thicknesses below a layer is its Z.
 * @return The cups and the trapped cavities, each sorted by its first layer.
 *
 * Both kinds come out of one walk over the layers, which is linear in the number of layers: the
 * holes of a layer are matched against the regions carried over from the layer below with a
 * union-find over the overlapping ones, so a region that splits and merges again stays one
 * region. A region is closed above when the layer that ends it covers it with solid and open below
 * when the layer under its first layer does not, which is the difference between a cup and a
 * cavity of trapped resin.
 */
CavityAnalysis detect_cavities(const std::vector<Domain::ExPolygons>& layers,
                               const std::vector<float>& layer_thicknesses_mm,
                               const CavityDetectionOptions& opts = {});

} // namespace Slic3r::SLA
