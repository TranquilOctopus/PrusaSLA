#ifndef SLIC3R_FORMAT_CTBSLA_HPP
#define SLIC3R_FORMAT_CTBSLA_HPP

#include <string>
#include <memory>

#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/SLA/RasterBase.hpp"

namespace Slic3r {

// Chitubox .ctb layer images, the run-length scheme documented in doc/sla-fork/formats/ctb.md.
std::unique_ptr<ISlaRasterizer> create_ctb_rasterizer(const SLAPrintConfigView& cfg);

} // namespace Slic3r

#endif // SLIC3R_FORMAT_CTBSLA_HPP
