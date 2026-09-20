#ifndef SLIC3R_FORMAT_GOOSLA_HPP
#define SLIC3R_FORMAT_GOOSLA_HPP

#include <string>
#include <memory>

#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/SLA/RasterBase.hpp"

namespace Slic3r {

std::unique_ptr<ISlaRasterizer> create_goo_rasterizer(const SLAPrintConfigView& cfg);

} // namespace Slic3r

#endif // SLIC3R_FORMAT_GOOSLA_HPP