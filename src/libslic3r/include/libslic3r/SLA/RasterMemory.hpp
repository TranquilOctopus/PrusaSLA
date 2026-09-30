#ifndef SLA_RASTERMEMORY_HPP
#define SLA_RASTERMEMORY_HPP

// Deliberately free of any other include: this header is public (src/libslic3r/include) and the
// tests of the App and Biz layers need it, while RasterBase.hpp is a private header of the engine.

#include <cstddef>

namespace Slic3r::sla {

// A full-resolution raw raster is the one buffer in the slice path whose size is the display
// resolution. AGGRaster holds it in a std::vector<agg::pixfmt_gray8::pixel_type>, and that
// pixel_type is a single uint8_t (agg_pixfmt_gray.h), so the buffer is one byte per pixel. The
// alpha half of agg::gray8 is not stored: m_rbuf is built over the same memory with a byte
// width of width * num_components, and num_components is 1.
constexpr size_t raw_raster_bytes_per_pixel()
{
    return 1;
}

/// How many raw rasters the rasterize step may hold at the same time. The step used to run one
/// unbounded tbb::parallel_for over every layer, so the number of live rasters was the core
/// count: on a 11520 x 5120 display (59 MP) one raster is 59 MB, and a 16 thread machine held
/// about 944 MB of them while slicing. 512 MB is under 8 % of the 8 GB the bad_alloc report is
/// from, and still leaves 9 layers in flight there (530 MB), so a machine that fits the budget
/// keeps the parallelism it had. Rasterizing a display-sized layer is CPU bound, so the budget and
/// the wall time of the step trade directly against each other; this constant is the only knob.
constexpr size_t raw_raster_byte_budget()
{
    return size_t(512) * 1024 * 1024;
}

/// How many layers the rasterize step may keep in memory at once, given what one layer costs.
/// Clamped to the layer count and to the parallelism available, so small displays (where a layer
/// is a few MB and the budget is never reached) are unaffected. Never returns 0, so a caller can
/// step a loop by it; a print without layers never runs the loop.
size_t raw_raster_batch_size(size_t bytes_per_layer, size_t layer_count, size_t max_parallel);

/// Number of full-resolution raw rasters alive right now, and the highest number seen since the
/// last reset. The count comes from the raster itself, so it covers every user of one and not
/// only the slice: the preview and any test rendering a layer show up in it too. Reset before
/// the work whose peak matters.
size_t live_raw_rasters();
size_t peak_live_raw_rasters();
void reset_peak_live_raw_rasters();

/// Counts one live raw raster for its lifetime. Thread safe, so layers rasterized in parallel
/// are counted independently.
class RawRasterGuard
{
public:
    RawRasterGuard();
    ~RawRasterGuard();

    RawRasterGuard(const RawRasterGuard&)            = delete;
    RawRasterGuard& operator=(const RawRasterGuard&) = delete;
};

} // namespace Slic3r::sla

#endif // SLA_RASTERMEMORY_HPP
