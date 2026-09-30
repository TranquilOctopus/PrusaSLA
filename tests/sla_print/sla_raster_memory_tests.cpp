#include <catch2/catch_test_macros.hpp>

#include "libslic3r/SLA/RasterBase.hpp"
#include "libslic3r/SLA/RasterMemory.hpp"
#include "Slic3r/Biz/Algorithms/Execution/Execution.hpp"
#include "Slic3r/Biz/Algorithms/Execution/ExecutionTBB.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::sla;
using Slic3r::Biz::Algorithms::Execution::ex_tbb;
using Slic3r::Biz::Algorithms::Execution::for_each;

namespace {

// The Photon Mono M5 panel: 59 megapixels of 8 bit grayscale per layer.
constexpr size_t M5_WIDTH_PX  = 11520;
constexpr size_t M5_HEIGHT_PX = 5120;

size_t m5_layer_bytes()
{
    return M5_WIDTH_PX * M5_HEIGHT_PX * raw_raster_bytes_per_pixel();
}

// Rasterize `count` layers in a window of `batch`, the way SLAPrint::Steps::rasterize does after
// this todo, and let the guard in the raster report the highest number alive at the same time.
size_t peak_rasters_in_batches(size_t count, size_t batch, const Resolution& res)
{
    const PixelDim pxdim{0.026, 0.026};
    const RasterBase::Trafo trafo;

    reset_peak_live_raw_rasters();
    for (size_t first = 0; first < count; first += batch) {
        const size_t last = std::min(first + batch, count);
        for_each(
            ex_tbb,
            first,
            last,
            [res, pxdim, trafo](size_t)
            {
                // No polygons to draw: the buffer is allocated and cleared by the
                // constructor, which is what the budget is about.
                std::unique_ptr<RasterBase> raster =
                    create_raster_grayscale_aa(res, pxdim, 1.0, trafo);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        );
    }
    const size_t peak = peak_live_raw_rasters();
    const size_t left = live_raw_rasters();
    reset_peak_live_raw_rasters();
    // A raster must not outlive its layer, so nothing is left over between batches.
    REQUIRE(left == 0);
    return peak;
}

} // namespace

TEST_CASE("Raster memory: a raw raster is one byte per pixel", "[sla][raster_memory]")
{
    REQUIRE(raw_raster_bytes_per_pixel() == 1);
    // 59 megapixels, allocated and cleared by the raster constructor before anything is drawn.
    REQUIRE(m5_layer_bytes() == 58982400);
}

TEST_CASE("Raster memory: batch size is the budget over the layer cost", "[sla][raster_memory]")
{
    const size_t layer = m5_layer_bytes();

    // 512 MB budget and 59 MB layers: 9 of them fit (530 MB), and the tenth would not.
    REQUIRE(raw_raster_batch_size(layer, 500, 16) == 9);
    REQUIRE(raw_raster_batch_size(layer, 500, 32) == 9);
    REQUIRE(layer * raw_raster_batch_size(layer, 500, 32) <= raw_raster_byte_budget());
    REQUIRE(layer * (raw_raster_batch_size(layer, 500, 32) + 1) > raw_raster_byte_budget());
}

TEST_CASE(
    "Raster memory: batch size never exceeds the layers or the parallelism",
    "[sla][raster_memory]"
)
{
    const size_t layer = m5_layer_bytes();

    // Fewer layers than the budget allows.
    REQUIRE(raw_raster_batch_size(layer, 3, 16) == 3);
    REQUIRE(raw_raster_batch_size(layer, 1, 16) == 1);
    // Nothing to rasterize: still a usable step, the loop simply never runs.
    REQUIRE(raw_raster_batch_size(layer, 0, 16) == 1);
    // A layer that does not fit the budget on its own still gets a slot, it is the only one that
    // can be trimmed.
    REQUIRE(raw_raster_batch_size(raw_raster_byte_budget() * 2, 500, 16) == 1);
    // A small display is bounded by the parallelism, not by the budget.
    REQUIRE(raw_raster_batch_size(2560 * 1440, 500, 8) == 8);
    REQUIRE(raw_raster_batch_size(2560 * 1440, 500, 4) == 4);
    // A format that never builds a raster (SVG) has no layer cost to budget for.
    REQUIRE(raw_raster_batch_size(0, 500, 6) == 6);
    // A machine reporting no usable thread still rasterizes one layer at a time.
    REQUIRE(raw_raster_batch_size(layer, 500, 0) == 1);
}

TEST_CASE("Raster memory: windowing the layers caps the live rasters", "[sla][raster_memory]")
{
    // Small enough to stay fast, big enough that the buffers are real allocations.
    const Resolution res{2048, 1024};
    const size_t bytes = res.pixels() * raw_raster_bytes_per_pixel();
    const size_t count = 24;
    const size_t batch = raw_raster_batch_size(bytes, count, 8);

    REQUIRE(batch == 8);

    const size_t peak = peak_rasters_in_batches(count, batch, res);
    REQUIRE(peak <= batch);
    // Without the window this would be every layer at once, which is what the bound is for.
    REQUIRE(peak < count);
    REQUIRE(live_raw_rasters() == 0);
}

TEST_CASE("Raster memory: the live count follows the guard", "[sla][raster_memory]")
{
    reset_peak_live_raw_rasters();
    REQUIRE(live_raw_rasters() == 0);

    {
        RawRasterGuard first;
        REQUIRE(live_raw_rasters() == 1);
        {
            RawRasterGuard second;
            REQUIRE(live_raw_rasters() == 2);
        }
        REQUIRE(live_raw_rasters() == 1);
    }
    REQUIRE(live_raw_rasters() == 0);
    REQUIRE(peak_live_raw_rasters() == 2);
}
