#include "libslic3r/SLA/RasterMemory.hpp"

#include <algorithm>
#include <atomic>

namespace Slic3r::sla {

namespace {

// Rasters are created and dropped on the slice worker threads, so both the live count and its
// high-water mark are updated atomically.
std::atomic<size_t> live_count{0};
std::atomic<size_t> peak_count{0};

} // namespace

size_t raw_raster_batch_size(size_t bytes_per_layer, size_t layer_count, size_t max_parallel)
{
    const size_t parallel = std::max<size_t>(max_parallel, 1);
    if (layer_count == 0)
        return 1;

    // A format that never builds a raster (SVG) reports 0 bytes, and then the budget has nothing
    // to divide: such a layer is bounded by its encoded size, not by the display resolution.
    const size_t by_budget = bytes_per_layer == 0 ?
        parallel :
        std::max<size_t>(1, raw_raster_byte_budget() / bytes_per_layer);

    return std::clamp(by_budget, size_t(1), std::min(layer_count, parallel));
}

RawRasterGuard::RawRasterGuard()
{
    const size_t live = live_count.fetch_add(1, std::memory_order_relaxed) + 1;

    // Raise the high-water mark without ever lowering it: every thread that sees a new maximum
    // writes it, and the last write is the largest.
    size_t peak = peak_count.load(std::memory_order_relaxed);
    while (live > peak && !peak_count.compare_exchange_weak(peak, live, std::memory_order_relaxed))
    {
        // The failed exchange refreshed peak with the current value, try again.
    }
}

RawRasterGuard::~RawRasterGuard()
{
    live_count.fetch_sub(1, std::memory_order_relaxed);
}

size_t live_raw_rasters()
{
    return live_count.load(std::memory_order_relaxed);
}

size_t peak_live_raw_rasters()
{
    return peak_count.load(std::memory_order_relaxed);
}

void reset_peak_live_raw_rasters()
{
    peak_count.store(live_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

} // namespace Slic3r::sla
