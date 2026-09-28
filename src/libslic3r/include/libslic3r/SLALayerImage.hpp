#pragma once

#include <libslic3r/SLAResult.hpp>

#include <vector>
#include <cstddef>
#include <cstdint>

namespace Slic3r::sla {

struct SlaLayerImage {
    size_t width = 0;
    size_t height = 0;
    std::vector<uint8_t> pixels; // 8-bit gray, row-major, width*height bytes, as sent to the screen
    double pixel_width_mm = 0.;
    double pixel_height_mm = 0.;
};

// Renders one layer (SLAResult::slices[i]) with the printer's display settings from
// printer_config (the SLAResultData::config). The image is downscaled to fit inside
// max_width x max_height pixels keeping the aspect ratio; 0 for both means full resolution.
SlaLayerImage render_sla_layer_image(const Domain::ExPolygons& slice,
                                     const Domain::ConfigView& printer_config,
                                     size_t max_width, size_t max_height);

} // namespace Slic3r::sla