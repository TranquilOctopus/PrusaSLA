// Copyright (c) Prusa Research 2020 - 2022 Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv
//
// PrusaSlicer is released under the terms of the AGPLv3 or higher

#ifndef ARCHIVETRAITS_HPP
#define ARCHIVETRAITS_HPP

#include <string>
#include <memory>
#include <functional>

#include "SLAArchiveReader.hpp"
#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/SLA/RasterBase.hpp" // ISlaRasterizer

namespace Slic3r {

/// <summary>
/// Factory for create file rasterizer for the '.sl1' file format
/// </summary>
/// <param name="cfg">Printer configuration like resolution, dimension etc</param>
/// <returns>Copyable(for each layer) rasterizer</returns>
std::unique_ptr<ISlaRasterizer> create_sl1_rasterizer(const SLAPrintConfigView& cfg);

class SL1Reader: public SLAArchiveReader {
    SLAImportQuality m_quality = SLAImportQuality::Balanced;
    std::function<bool(int)> m_progr;
    std::string m_fname;
    
public:
    // If the profile is missing from the archive (older PS versions did not have
    // it), profile_out's initial value will be used as fallback. profile_out will be empty on
    // function return if the archive did not contain any profile.
    ConfigSubstitutions read(std::vector<ExPolygons> &slices,
                             DynamicPrintConfig      &profile_out) override;

    ConfigSubstitutions read(DynamicPrintConfig &profile) override;

    SL1Reader() = default;
    SL1Reader(const std::string       &fname,
              SLAImportQuality         quality,
              std::function<bool(int)> progr)
        : m_quality(quality), m_progr(progr), m_fname(fname)
    {}
};

struct RasterParams {
    sla::RasterBase::Trafo trafo; // Raster transformations
    coord_t        width, height; // scaled raster dimensions (not resolution)
    double         px_h, px_w;    // pixel dimensions
};

RasterParams get_raster_params(const DynamicPrintConfig &cfg);

void invert_raster_trafo(ExPolygons &                  expolys,
                         const sla::RasterBase::Trafo &trafo,
                         coord_t                       width,
                         coord_t                       height);

} // namespace Slic3r

#endif // ARCHIVETRAITS_HPP