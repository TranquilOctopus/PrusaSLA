# Raster memory in the SLA slice and export path

Work for M4.14. Everything below was found by reading the code; **no measurement was taken in
this session** (nothing was built or run), so the byte figures are computed from the code paths
and the display resolution, not observed. Section 8 lists what a human still has to measure.

## 1. The numbers for a Photon Mono M5 layer

11520 x 5120 = 58 982 400 pixels.

| thing | bytes per pixel | bytes per layer |
|---|---|---|
| raw raster (`AGGRaster::m_buf`) | 1 | 59.0 MB (56.25 MiB) |
| encoded layer, Anycubic/pm5 `pw0` RLE | - | 10 kB .. 500 kB, depending on the layer |
| encoded layer, Elegoo `gooimg` RLE | - | same order |
| encoded layer, SL1 PNG | - | 0.1 .. 20 MB, depending on the layer |
| encoded layer, SL1 SVG (text) | - | 0.1 .. 10 MB, contour dependent |

The raw raster is one byte per pixel, not two. `AGGRaster` holds it in
`std::vector<agg::pixfmt_gray8::pixel_type>`, and that `pixel_type` is
`struct { value_type c[1]; }` with `value_type = int8u` (`bundled_deps/agg/agg/agg_pixfmt_gray.h:144-146`).
The unused `a` member of `agg::gray8` is *not* stored: the rendering buffer is built over the same
memory with a byte width of `width * num_components` and `num_components` is 1
(`AGGRaster.hpp:142-144`). Only the gray formats exist in this path; there is no 16 bit raster
anywhere in it.

## 2. The path

```
SidebarPlaterActionButtons.cpp:216      Slice button -> SlicingInteractor::slice_bed
  SlicingInteractor.cpp:420             -> BackgroundProcess::slice
    BackgroundProcess.cpp:267           -> SLAPrint::slice  (worker thread, bad_alloc caught at :273)
      SLAPrint.cpp:1265                 two steps: slapsMergeSlicesAndEval, then slapsRasterize
        SLAPrintSteps.cpp:1421          merge_slices_and_eval_stats()
        SLAPrintSteps.cpp:1747          rasterize()          <- the one that holds raw rasters
          SLAPrintSteps.cpp:1777        m_on_sla_result -> main thread -> SLAResultCache
  ProjectInteractor.cpp:912             export takes a shared_ptr to the cached result
    ResultExportDataFinalizer.cpp:125   ISlaArchiveFormat::store(file, data)
```

Encoded layers live in one place for the whole session: `SLAResultData::files.data`
(`libslic3r/SLAResult.hpp:25`, a `std::vector<std::vector<uint8_t>>`, one entry per layer), owned by
`SLAResultCache::m_results` (`Biz/SLAResultCache.cpp:22-32`). That is per `(project, bed)`, so
every bed sliced in a session keeps its encoded set until the bed is removed.

## 3. The one place that holds full-resolution pixels: the rasterize step

`SLAPrintSteps.cpp:1747` builds `FilesData files(layers.size())` and then, **before this commit**,
ran a single

```cpp
execution::for_each(execution::ex_tbb, size_t(0), layers.size(), [&](size_t idx) {
    files[idx] = rasterizer.create_file(layers[idx].transformed_slices());
});
```

Granularity defaults to 1 (`Execution.hpp:57`) and no concurrency was passed, so TBB ran
`parallel_for(blocked_range{0, N, 1})` over the whole print with one task per layer and
`max_concurrency()` workers (`ExecutionTBB.hpp:40-48`). `create_file` allocates the full-resolution
raster, draws the layer, encodes it and drops the raster before returning:

| format | `create_file` | raster |
|---|---|---|
| SL1 PNG | `Format/SL1.cpp:60-67` | 59 MB |
| SL1 SVG | `Format/SL1_SVG.cpp:248-256` | none, it builds an SVG string (`SL1_SVG.cpp:138`) |
| Anycubic / pm5 | `Format/AnycubicSLA.cpp:105-112` | 59 MB |
| Elegoo .goo | `Format/GooSLA.cpp:150-157` | 59 MB |

So the raw raster never outlives its layer, and the encoded set is written as it grows. The only
thing that multiplied the buffers was the parallelism:

```
peak of the rasterize step = min(max_concurrency, layer_count) x 59 MB
                            + the encoded layers accumulated so far
```

At 12K that is **944 MB on a 16 thread machine and 1.9 GB on 32 threads**, decided by the core
count and by nothing in the code. For comparison, the merge step above it
(`SLAPrintSteps.cpp:1582-1583`) *does* pass `execution::max_concurrency(execution::ex_tbb)`, which
makes the missing bound here an oversight rather than a decision.

**Fixed in this commit.** The loop is now windowed: `raw_raster_batch_size()`
(`libslic3r/SLA/RasterMemory.hpp`) is the budget over what one layer costs, clamped to the layer
count and to the available parallelism, and `rasterize()` walks the print in windows of that size
(`SLAPrintSteps.cpp:1755-1775`). Each rasterizer reports its own layer cost through
`ISlaRasterizer::raw_raster_bytes()`, so SVG reports 0 and is not throttled. The budget is 512 MB
(`raw_raster_byte_budget()`), which is 9 layers at 12K (530 MB) and under 8 % of the 8 GB the
bad_alloc report comes from. A machine with 9 or fewer threads (what an 8 GB machine usually has)
therefore keeps the parallelism it had, and a wide machine gets a bound instead of a core count.

**The trade-off is real and worth stating plainly:** rasterizing a display sized layer is CPU
bound, so the budget and the wall time of the step trade directly against each other. On a 12K
print on a 24 core machine the step now runs 9 wide instead of 24. No code structure removes that;
`raw_raster_byte_budget()` is the single knob.

## 4. The second copy of the whole print, at export time

`store_anycubic` (`Biz/ResultExport/SLA/AnycubicSLA.cpp:367`) had to know every layer's offset in
the image block before that block could be written, and got there by concatenating the encoded
layers into a second buffer:

```
data.files.data          = the encoded print, held by the result cache
layer_images             = the same bytes again, for the duration of the export
```

so the export peak was twice the encoded print. The layer offsets are derived from the layer sizes
alone, so the definitions could be collected on their own; that is what the code does now
(`AnycubicSLA.cpp:407-444`, `layer_defs`, 36 bytes per layer). The old `reserve(layer_count *
32768)` on that buffer was also a guess: a detailed layer encodes to more than 32 kB, and the
vector then grew geometrically, so it could have held 2x the encoded print.

`store_goo` declared the same `layer_images` vector and never wrote to it
(`GooSLA.cpp:258` and `:463` before this commit): 32 kB x layer_count held for nothing, 16 MB for a
500 layer print. Removed.

`store_pm5` (`:515`) and `store_sl1` (`:196`) never duplicated anything: both stream from
`data.files.data`, and `store_pm5` seeks back to patch the offsets it wrote as placeholders
(`AnycubicSLA.cpp:814-827`).

## 5. What is retained for the whole print, and how much

`SLAResultData::files.data`, one encoded layer per layer, for every bed ever sliced:

| format | bytes per layer | 500 layers at 12K |
|---|---|---|
| Anycubic / pm5 RLE | 10 .. 500 kB | 5 .. 250 MB |
| .goo RLE | 10 .. 500 kB | 5 .. 250 MB |
| SL1 PNG | 0.1 .. 20 MB | 50 MB .. 10 GB |
| SL1 SVG | 0.1 .. 10 MB | 50 MB .. 5 GB |

The 2026-09-22 `std::bad_alloc` is this table, not section 3: both run-length encoders used to
`reserve()` the uncompressed image and keep the capacity after the vector was moved into the result
(59 MB per layer for Anycubic, 118 MB for .goo), so a few hundred layers went past 8 GB. That was
fixed in `574213b5e1`, and the export tests assert the capacity of a stored layer stays under 1 MB.
The `std::bad_alloc` is therefore already addressed; what M4.14 adds is that the *remaining*
display sized term is now bounded rather than accidental.

## 6. The other half of the peak: polygon copies in the merge step

Not full resolution, but for a tall model it is the larger of the two, and it is three copies of
every layer at once inside `merge_slices_and_eval_stats()`:

| what | where | lifetime |
|---|---|---|
| `PrintLayers::m_printer_input` | `SLAPrint.hpp:455`, filled at `SLAPrintSteps.cpp:1522-1527` | the whole `SLAPrint`, never cleared after slicing |
| `all_layer_polygons` (island detection, M4.8b) | `SLAPrintSteps.cpp:1589-1594` | the merge step |
| `slices` (per-layer stats, M4.9) | `SLAPrintSteps.cpp:1619-1626` | the merge step, alongside the previous one |

Peak is 3x the polygon memory of the model, and the first copy stays resident until the next
slice. Polygon memory does not depend on the display resolution, but it does depend on layer count,
so a tall 12K print hits it. `all_layer_polygons` and `slices` could both be views over
`m_printer_input` instead of copies; that is the next cheapest win and is not part of this commit.

## 7. Paths that are already bounded

* **Preview** (`SLALayerImage.cpp:96` and `:147`) builds a raster, but a downscaled one (1024x1024)
  or a window (256x256): 1 MB and 64 kB, one layer at a time. It shows up in the live raster count
  while it runs, which is why `reset_peak_live_raw_rasters()` exists.
* **`SlaLayerImage` / `RasterBase`** are never accumulated; the only holders are two `std::optional`s
  in the preview window (`App/Preview/SlaLayerImageWindow.hpp:113,126`).
* **Import** (`Biz/ResultExport/SLA/SL1Import.cpp:188-208`) is the one place in the repository that
  really does hold every layer as raw pixels (`std::vector<sla::GrayLayerImage>`, 1 byte per pixel,
  59 MB x layers at 12K), plus a transient RGBA buffer of 236 MB per layer while decoding
  (`SL1Import.cpp:57-68`). It is the import direction, not the slice path, and it is untouched here.
* **The writers** hold no raw raster at all: the registry (`SlaArchiveFormat.cpp`) and the format
  adapters (`SL1Format.cpp`) are stateless, and `store()` takes the result by const reference
  (`SlaArchiveFormat.hpp:34`).

## 8. What a human still has to measure

The todo asks for the peak *working set per step*, for a small and a tall model, on an idle
machine. That needs a build and a run, so M4.14 stays open. What to run once this builds:

1. Build `sla_print_tests` and `slic3r-shared-tests`; the new cases
   `tests/sla_print/sla_raster_memory_tests.cpp` and
   `src/slic3r-shared/test/Slic3r/Biz/ResultExport/SLA/SlaRasterMemoryTests.cpp` cover the bound
   without needing a printer.
2. Slice a small and a tall model on the M5 profile and watch the process working set. The peak of
   the rasterize step should now sit under 512 MB instead of `min(cores, layers) x 59 MB`, and the
   merge step's peak should be visible as the polygon term of section 6. Step timings come from the
   same progress labels, and the 12K rasterize time is the number to compare against the 3-6 minutes
   in the todo: it gets slower on a machine with more than 9 threads.
3. `sla::live_raw_rasters()` and `sla::peak_live_raw_rasters()` are exported for this; calling
   `reset_peak_live_raw_rasters()` before a slice gives the peak of that slice alone.
4. If section 6 turns out to dominate for tall models, the copies there are the next thing to
   remove, and the Anycubic RLE size (10-500 kB per layer) is worth a look at the same time: 250 MB
   of encoded layers for a 500 layer 12K print is what a display that is mostly empty costs.
