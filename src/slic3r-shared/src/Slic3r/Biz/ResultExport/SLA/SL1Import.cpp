#include "Slic3r/Biz/ResultExport/SLA/SL1Import.hpp"

#include "Slic3r/Biz/ArchiveIni.hpp"
#include "Slic3r/Biz/Algorithms/PNGReadWrite.hpp"
#include "libslic3r/SLALayersToMesh.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <boost/algorithm/string.hpp>

#include <miniz.h>

namespace Slic3r::Biz::PrintHost::Sla {

namespace {

// The metadata files of an SL1 archive. Every other entry but the thumbnails is a
// layer image, named after the job directory and numbered from 00000 upwards.
const char *const CONFIG_INI  = "config.ini";
const char *const PROFILE_INI = "prusaslicer.ini";

using Slic3r::Biz::ArchiveIniMap;
using Slic3r::Biz::get_ini_bool;
using Slic3r::Biz::get_ini_string;
using Slic3r::Biz::get_ini_value;
using Slic3r::Biz::parse_archive_ini;
using Slic3r::Biz::read_zip_entry;
using Slic3r::Biz::ZipReader;

// The share of the reported progress the decoding of the layer images takes: inflating and
// decoding a few thousand pngs is the bulk of the time, tracing them into a mesh the rest.
constexpr double k_decode_share = 2. / 3.;

bool ends_with(const std::string &str, const std::string &suffix)
{
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Decode one layer image to 8 bit gray. The SL1 rasterizer writes plain gray pngs,
// anything else is converted here.
tl::expected<sla::GrayLayerImage, std::string> decode_layer(const std::string &png_data)
{
    sla::GrayLayerImage layer;

    png::ImageGreyscale gray;
    if (png::decode_png(png::ReadBuf{png_data.data(), png_data.size()}, gray)) {
        layer.width  = gray.cols;
        layer.height = gray.rows;
        layer.pixels = std::move(gray.buf);
        return std::move(layer);
    }

    // Not a gray png. The simplified libpng reader gives us rgba whatever the
    // source was, so collapse the color channels to luminance.
    std::vector<unsigned char> rgba;
    unsigned                   width = 0, height = 0;
    if (!png::decode_png(png_data, rgba, width, height) || rgba.size() < size_t(width) * height * 4)
        return tl::make_unexpected("Cannot decode the png image.");

    layer.width  = width;
    layer.height = height;
    layer.pixels.resize(size_t(width) * height);
    for (size_t i = 0; i < layer.pixels.size(); ++i) {
        const unsigned char *px = rgba.data() + i * 4;
        layer.pixels[i] = static_cast<uint8_t>((77u * px[0] + 150u * px[1] + 29u * px[2]) >> 8);
    }

    return std::move(layer);
}

} // namespace

tl::expected<Sl1ImportResult, std::string>
import_sl1_archive(const boost::filesystem::path &path, std::function<bool()> stop,
                   std::function<void(double)> progress)
{
    ZipReader zip{path.string()};
    if (!zip.ok())
        return tl::make_unexpected(
            fmt::format("{} cannot be opened as an SL1 (zip) archive.", path.string()));

    ArchiveIniMap                                config_ini, slicing_ini;
    std::vector<std::pair<std::string, mz_uint>> layers;
    bool                                        has_config = false, has_profile = false;

    const mz_uint num_entries = mz_zip_reader_get_num_files(&zip.archive());
    for (mz_uint i = 0; i < num_entries; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip.archive(), i, &stat))
            continue;

        const std::string name{stat.m_filename};
        std::string       lname = name;
        boost::algorithm::to_lower(lname);

        // The thumbnails are png images too, and are of no use here.
        if (lname.find("thumbnail") != std::string::npos)
            continue;

        if (lname == CONFIG_INI || lname == PROFILE_INI) {
            auto text = read_zip_entry(zip.archive(), stat);
            if (!text)
                return tl::make_unexpected(text.error());

            ArchiveIniMap         &ini    = lname == CONFIG_INI ? config_ini : slicing_ini;
            bool                  &seen   = lname == CONFIG_INI ? has_config : has_profile;
            const ArchiveIniMap    parsed  = parse_archive_ini(*text);
            for (const auto &[key, value] : parsed)
                ini.insert_or_assign(key, value);
            seen = true;
            continue;
        }

        if (ends_with(lname, ".png"))
            layers.emplace_back(name, i);
    }

    if (!has_config)
        return tl::make_unexpected(fmt::format("{} has no {} in it.", path.string(), CONFIG_INI));
    if (!has_profile)
        return tl::make_unexpected(fmt::format("{} has no {} in it.", path.string(), PROFILE_INI));
    if (layers.empty())
        return tl::make_unexpected(fmt::format("{} contains no layer images.", path.string()));

    // The writer numbers the layer images, so sorting them by name gives the
    // order of the layers from the bed upwards.
    std::sort(layers.begin(), layers.end(),
              [](const auto &lhs, const auto &rhs) { return lhs.first < rhs.first; });

    Sl1ImportResult result;
    // prusaslicer.ini holds the profile the layers were rendered for and wins
    // over the printer's config.ini where the two share a key.
    result.profile = std::move(slicing_ini);
    result.profile.insert(config_ini.begin(), config_ini.end());

    const auto missing = [&path](const char *key) {
        return fmt::format(
            "{} is not a usable SL1 archive: the profile has no {} key.", path.string(), key);
    };

    const ArchiveIniMap &profile = result.profile;

    sla::LayersToMeshParams params;

    const auto display_width  = get_ini_value<double>(profile, "display_width");
    const auto display_height = get_ini_value<double>(profile, "display_height");
    if (!display_width || *display_width <= 0.)
        return tl::make_unexpected(missing("display_width"));
    if (!display_height || *display_height <= 0.)
        return tl::make_unexpected(missing("display_height"));
    params.display_width_mm  = *display_width;
    params.display_height_mm = *display_height;

    const auto pixels_x = get_ini_value<int>(profile, "display_pixels_x");
    const auto pixels_y = get_ini_value<int>(profile, "display_pixels_y");
    if (!pixels_x || *pixels_x < 2)
        return tl::make_unexpected(missing("display_pixels_x"));
    if (!pixels_y || *pixels_y < 2)
        return tl::make_unexpected(missing("display_pixels_y"));

    // The size of a pixel as the reader has always derived it: the display size
    // spread over the pixels between the first and the last one.
    params.pixel_width_mm  = *display_width / (*pixels_x - 1);
    params.pixel_height_mm = *display_height / (*pixels_y - 1);

    params.mirror_x = get_ini_bool(profile, "display_mirror_x").value_or(false);
    params.mirror_y = get_ini_bool(profile, "display_mirror_y").value_or(false);

    // An absent orientation means the usual landscape display.
    const std::string orientation = get_ini_string(profile, "display_orientation").value_or("");
    params.portrait               = boost::algorithm::iequals(orientation, "portrait");

    // config.ini carries the layer height for the files written before the
    // profile was embedded, so it is the fallback.
    auto layer_height = get_ini_value<double>(profile, "layer_height");
    if (!layer_height)
        layer_height = get_ini_value<double>(profile, "layerHeight");
    if (!layer_height || *layer_height <= 0.)
        return tl::make_unexpected(missing("layer_height"));
    params.layer_height_mm = *layer_height;

    // An initial layer height of zero means the same as the layer height.
    params.first_layer_height_mm = get_ini_value<double>(profile, "initial_layer_height").value_or(0.);
    if (params.first_layer_height_mm <= 0.)
        params.first_layer_height_mm = params.layer_height_mm;

    std::vector<sla::GrayLayerImage> decoded;
    decoded.reserve(layers.size());
    for (const auto &[name, index] : layers) {
        if (progress)
            progress(decoded.size() * k_decode_share / double(layers.size()));
        if (stop && stop())
            return Sl1ImportResult{};

        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip.archive(), index, &stat))
            return tl::make_unexpected(
                fmt::format("Cannot find the layer image {} in the archive.", name));

        auto data = read_zip_entry(zip.archive(), stat);
        if (!data)
            return tl::make_unexpected(data.error());

        auto layer = decode_layer(*data);
        if (!layer)
            return tl::make_unexpected(fmt::format("Layer image {}: {}", name, layer.error()));

        decoded.push_back(std::move(*layer));
    }

    result.mesh = sla::layers_to_mesh(decoded, params, stop, [progress](double share) {
        if (progress)
            progress(k_decode_share + (1. - k_decode_share) * share);
    });

    if (progress)
        progress(1.);

    return result;
}

} // namespace Slic3r::Biz::PrintHost::Sla
