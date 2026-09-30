#include "libslic3r/SLALayersToMesh.hpp"

#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/MarchingSquares.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/SLA/RasterBase.hpp"
#include "libslic3r/SlicesToTriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace Slic3r::sla {

namespace {

// Row-major grayscale raster adapter for the marching squares algorithm.
struct GrayRaster {
    size_t rows = 0, cols = 0;
    const uint8_t *data = nullptr;

    uint8_t pixel(size_t row, size_t col) const { return data[row * cols + col]; }
};

template<class Fn> void foreach_vertex(ExPolygon &poly, Fn &&fn)
{
    for (auto &p : poly.contour.points) fn(p);
    for (auto &h : poly.holes)
        for (auto &p : h.points) fn(p);
}

// Undo the transformations the rasterizer applied to the polygons, so a layer
// rendered with the given trafo comes back at the position it was drawn from.
void invert_raster_trafo(ExPolygons &              expolys,
                         const RasterBase::Trafo & trafo,
                         coord_t                   width,
                         coord_t                   height)
{
    for (ExPolygon &expoly : expolys) {
        if (trafo.mirror_y)
            foreach_vertex(expoly, [height](Point &p) { p.y() = height - p.y(); });

        if (trafo.mirror_x)
            foreach_vertex(expoly, [width](Point &p) { p.x() = width - p.x(); });

        expoly.translate(-trafo.center_x, -trafo.center_y);

        if (trafo.flipXY)
            foreach_vertex(expoly, [](Point &p) { std::swap(p.x(), p.y()); });

        if ((trafo.mirror_x + trafo.mirror_y + trafo.flipXY) % 2) {
            expoly.contour.reverse();
            for (auto &h : expoly.holes) h.reverse();
        }
    }
}

} // namespace

} // namespace Slic3r::sla

namespace marchsq {

// Register the grayscale layer raster for the marching squares algorithm.
template<> struct _RasterTraits<Slic3r::sla::GrayRaster> {
    using Rst = Slic3r::sla::GrayRaster;

    // The type of pixel cell in the raster
    using ValueType = uint8_t;

    // Value at a given position
    static uint8_t get(const Rst &rst, size_t row, size_t col) { return rst.pixel(row, col); }

    // Number of rows and cols of the raster
    static size_t rows(const Rst &rst) { return rst.rows; }
    static size_t cols(const Rst &rst) { return rst.cols; }
};

} // namespace marchsq

namespace Slic3r::sla {

Domain::TriangleMesh layers_to_mesh(const std::vector<GrayLayerImage> &layers,
                                    const LayersToMeshParams &       params,
                                    std::function<bool()>            stop,
                                    std::function<void(double)>      progress)
{
    if (layers.empty() || params.pixel_width_mm <= 0. || params.pixel_height_mm <= 0.)
        return {};

    // The very same trafo the rasterizer used when the layers were drawn.
    RasterBase::Trafo trafo{params.portrait ? RasterBase::roPortrait : RasterBase::roLandscape,
                            {params.mirror_x, params.mirror_y}};

    // The extent of the raster in mm. A portrait display is rotated, which swaps
    // the display dimensions along the raster axes.
    const double raster_width_mm =
        params.portrait ? params.display_height_mm : params.display_width_mm;
    const double raster_height_mm =
        params.portrait ? params.display_width_mm : params.display_height_mm;
    const coord_t raster_width = scaled(raster_width_mm);
    const coord_t raster_height = scaled(raster_height_mm);

    std::vector<ExPolygons> slices;
    slices.reserve(layers.size());

    for (size_t layer_index = 0; layer_index < layers.size(); ++layer_index) {
        const GrayLayerImage &layer = layers[layer_index];
        if (progress)
            progress(double(layer_index) / double(layers.size()));
        if (stop && stop())
            return {};

        if (layer.width < 2 || layer.height < 2 ||
            layer.pixels.size() < layer.width * layer.height) {
            slices.emplace_back();
            continue;
        }

        GrayRaster raster{layer.height, layer.width, layer.pixels.data()};
        std::vector<marchsq::Ring> rings = marchsq::execute(raster, params.threshold);

        Polygons polys;
        polys.reserve(rings.size());
        for (const marchsq::Ring &ring : rings) {
            Polygon poly;
            poly.points.reserve(ring.size());
            for (const marchsq::Coord &crd : ring)
                poly.points.emplace_back(scaled(crd.c * params.pixel_width_mm),
                                         scaled(crd.r * params.pixel_height_mm));
            polys.emplace_back(std::move(poly));
        }

        ExPolygons expolys = union_ex(polys);
        invert_raster_trafo(expolys, trafo, raster_width, raster_height);
        slices.push_back(std::move(expolys));
    }

    if (slices.empty())
        return {};

    if (progress)
        progress(1.);

    return Biz::Algorithms::TriangleMesh::construct(
        slices_to_mesh(slices, 0., params.layer_height_mm, params.first_layer_height_mm));
}

} // namespace Slic3r::sla
