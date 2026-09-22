// Copyright (c) Prusa Research 2020 - 2023 Tomáš Mészáros @tamasmeszaros, Pavel Mikuš @Godrak, Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv
//
// PrusaSlicer is released under the terms of the AGPLv3 or higher

#include "SLAArchiveReader.hpp"
#include "SL1.hpp"
#include "SL1_SVG.hpp"
#include "libslic3r/I18N.hpp"

#include "libslic3r/SlicesToTriangleMesh.hpp"

#include <boost/filesystem/path.hpp>
#include <boost/algorithm/string.hpp>

#include <array>
#include <set>

namespace Slic3r {

std::unique_ptr<SLAArchiveReader> SLAArchiveReader::create(
    const std::string       &fname,
    const std::string       &format_id,
    SLAImportQuality         quality,
    const ProgrFn & progr)
{
    // M5.1a: The old SLAArchiveFormatRegistry was removed. For now, we only support SL1/SL1S.
    // M5.1b will restore a proper format registry for readers.
    // We create the SL1Reader directly based on file extension.
    
    std::string ext = boost::filesystem::path(fname).extension().string();
    boost::algorithm::to_lower(ext);
    
    if (!ext.empty() && ext.front() == '.')
        ext.erase(ext.begin());
    
    // Supported extensions for SL1 format
    static const std::set<std::string> sl1_extensions = {"sl1", "sl1s", "zip"};
    static const std::set<std::string> sl1svg_extensions = {"sl1svg"};
    
    if (sl1_extensions.count(ext) || format_id == "SL1") {
        return std::make_unique<SL1Reader>(fname, quality, progr);
    }
    if (sl1svg_extensions.count(ext) || format_id == "SL1_SVG") {
        // SL1_SVG uses the same reader logic as SL1 for reading (both are zip archives with PNG/SVG layers)
        return std::make_unique<SL1Reader>(fname, quality, progr);
    }
    
    return nullptr;
}

struct SliceParams { double layerh = 0., initial_layerh = 0.; };

static SliceParams get_slice_params(const DynamicPrintConfig &cfg)
{
    auto *opt_layerh = cfg.option<ConfigOptionFloat>("layer_height");
    auto *opt_init_layerh = cfg.option<ConfigOptionFloat>("initial_layer_height");

    if (!opt_layerh || !opt_init_layerh)
        throw MissingProfileError("Invalid SL1 / SL1S file");

    return SliceParams{opt_layerh->getFloat(), opt_init_layerh->getFloat()};
}

ConfigSubstitutions import_sla_archive(const std::string       &zipfname,
                                       const std::string       &format_id,
                                       indexed_triangle_set    &out,
                                       DynamicPrintConfig      &profile,
                                       SLAImportQuality         quality,
                                       const ProgrFn & progr)
{
    ConfigSubstitutions ret;

    if (auto reader = SLAArchiveReader::create(zipfname, format_id, quality, progr)) {
        std::vector<ExPolygons> slices;
        ret = reader->read(slices, profile);

        SliceParams slicp = get_slice_params(profile);

        if (!slices.empty())
            out = slices_to_mesh(slices, 0, slicp.layerh, slicp.initial_layerh);

    } else {
        throw ReaderUnimplementedError("Reader unimplemented");
    }

    return ret;
}

ConfigSubstitutions import_sla_archive(const std::string  &zipfname,
                                       const std::string  &format_id,
                                       DynamicPrintConfig &out)
{
    ConfigSubstitutions ret;

    if (auto reader = SLAArchiveReader::create(zipfname, format_id)) {
        ret = reader->read(out);
    } else {
        throw ReaderUnimplementedError("Reader unimplemented");
    }

    return ret;
}

} // namespace Slic3r