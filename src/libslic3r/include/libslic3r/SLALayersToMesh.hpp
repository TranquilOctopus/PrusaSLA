#pragma once

#include "Slic3r/Domain/TriangleMesh.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace Slic3r::sla {

// One decoded 8-bit grayscale layer image, as it was sent to the printer display.
struct GrayLayerImage {
    size_t width = 0, height = 0;
    std::vector<uint8_t> pixels; // row-major, width*height bytes
};

struct LayersToMeshParams {
    // The size of one pixel on the display the layers were rendered for.
    double pixel_width_mm = 0., pixel_height_mm = 0.;
    // The physical size of the display the layers were rendered for.
    double display_width_mm = 0., display_height_mm = 0.;
    // The mirroring and the orientation the layers were rendered with. These have
    // to match the display settings of the printer, the same way the rasterizer
    // (sla::create_raster_grayscale_aa) used them when the layers were drawn.
    bool mirror_x = false, mirror_y = false;
    bool portrait = false;
    // The height of the first layer and the height of the layers above it.
    double first_layer_height_mm = 0., layer_height_mm = 0.;
    // Everything darker than this is considered outside of the object.
    uint8_t threshold = 128;
};

// Marching squares on each layer, then stacks the layer outlines into a closed mesh.
// The layers are expected in bottom to top order. An empty mesh is returned for
// empty input, and also when stop() returns true (cancel) before all layers are done.
Domain::TriangleMesh layers_to_mesh(const std::vector<GrayLayerImage> &layers,
                                    const LayersToMeshParams &       params,
                                    std::function<bool()>            stop = {});

} // namespace Slic3r::sla
