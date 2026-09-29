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

// Renders a width_px x height_px window of one layer at the printer's native pixel size
// (one display pixel is one image pixel, so pixel_width_mm / pixel_height_mm of the result
// are the unscaled display pixel dimensions). The window is centred on (center_x_mm,
// center_y_mm) in slice coordinates - the same coordinate system the display rectangle
// [0, display_width] x [0, display_height] spans. Gamma and mirroring / orientation are
// the same as in render_sla_layer_image(), so the returned image is a true magnification
// of a part of the downscaled image.
SlaLayerImage render_sla_layer_image_region(const Domain::ExPolygons& slice,
                                            const Domain::ConfigView& printer_config,
                                            double center_x_mm, double center_y_mm,
                                            size_t width_px, size_t height_px);

} // namespace Slic3r::sla