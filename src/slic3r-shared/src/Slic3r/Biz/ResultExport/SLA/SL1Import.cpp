#include "Slic3r/Biz/ResultExport/SLA/SL1Import.hpp"

#include "Slic3r/Biz/Algorithms/MiniZWrapper.hpp"
#include "Slic3r/Biz/Algorithms/PNGReadWrite.hpp"
#include "libslic3r/SLALayersToMesh.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <boost/algorithm/string.hpp>

#include <miniz.h>

namespace Slic3r::Biz::PrintHost::Sla {

namespace {

using IniMap = std::map<std::string, std::string>;

// The metadata files of an SL1 archive. Every other entry but the thumbnails is a
// layer image, named after the job directory and numbered from 00000 upwards.
const char *const CONFIG_INI  = "config.ini";
const char *const PROFILE_INI = "prusaslicer.ini";

// A zip opened for reading, closed again when it goes out of scope.
class ZipReader
{
public:
    explicit ZipReader(const std::string &fname)
        : m_ok(Algorithms::open_zip_reader(&m_zip.arch, fname))
    {
    }
    ~ZipReader()
    {
        if (m_ok)
            Algorithms::close_zip_reader(&m_zip.arch);
    }

    ZipReader(const ZipReader &)            = delete;
    ZipReader &operator=(const ZipReader &) = delete;

    bool           ok() const { return m_ok; }
    mz_zip_archive &archive() { return m_zip.arch; }

private:
    Algorithms::MZ_Archive m_zip;
    bool                   m_ok = false;
};

tl::expected<std::string, std::string> extract_entry(mz_zip_archive                 &arch,
                                                     const mz_zip_archive_file_stat &stat)
{
    if (stat.m_uncomp_size == 0)
        return std::string{};

    std::string buf(size_t(stat.m_uncomp_size), '\0');
    if (!mz_zip_reader_extract_to_mem(&arch, stat.m_file_index, buf.data(), buf.size(), 0))
        return tl::make_unexpected(
            fmt::format("Cannot extract {} from the SL1 archive.", stat.m_filename));

    return std::move(buf);
}

// A minimal ini reader: `key = value` lines, `[section]` headers, ';' and '#'
// comments and surrounding whitespace. Enough for the two ini files of an SL1
// archive, and it does not need the legacy config machinery.
IniMap parse_ini(const std::string &text)
{
    IniMap map;

    std::istringstream in{text};
    std::string        line;
    while (std::getline(in, line)) {
        line = boost::algorithm::trim(line);
        if (line.empty() || line.front() == ';' || line.front() == '#' || line.front() == '[')
            continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        std::string key = boost::algorithm::trim(line.substr(0, eq));
        if (key.empty())
            continue;

        map[key] = boost::algorithm::trim(line.substr(eq + 1));
    }

    return map;
}

// The ini files are written by the app itself, but read them with the classic
// locale anyway so that a comma decimal separator of the running system cannot
// change the parsed geometry.
template<typename T> std::optional<T> get_value(const IniMap &ini, const char *key)
{
    const auto it = ini.find(key);
    if (it == ini.end() || it->second.empty())
        return std::nullopt;

    std::istringstream in{it->second};
    in.imbue(std::locale::classic());

    T value{};
    in >> value;
    if (in.fail())
        return std::nullopt;

    return value;
}

std::optional<std::string> get_string(const IniMap &ini, const char *key)
{
    const auto it = ini.find(key);
    if (it == ini.end() || it->second.empty())
        return std::nullopt;

    return it->second;
}

std::optional<bool> get_bool(const IniMap &ini, const char *key)
{
    const auto it = ini.find(key);
    if (it == ini.end() || it->second.empty())
        return std::nullopt;

    std::string value = it->second;
    boost::algorithm::to_lower(value);
    if (value == "1" || value == "true" || value == "yes")
        return true;
    if (value == "0" || value == "false" || value == "no")
        return false;

    return std::nullopt;
}

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
import_sl1_archive(const boost::filesystem::path &path, std::function<bool()> stop)
{
    ZipReader zip{path.string()};
    if (!zip.ok())
        return tl::make_unexpected(
            fmt::format("{} cannot be opened as an SL1 (zip) archive.", path.string()));

    IniMap                    config_ini, slicing_ini;
    std::vector<std::pair<std::string, mz_uint>> layers;
    bool                      has_config = false, has_profile = false;

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
            auto text = extract_entry(zip.archive(), stat);
            if (!text)
                return tl::make_unexpected(text.error());

            IniMap    &ini = lname == CONFIG_INI ? config_ini : slicing_ini;
            bool     &seen = lname == CONFIG_INI ? has_config : has_profile;
            const IniMap parsed = parse_ini(*text);
            seen                 = true;
            for (const auto &[key, value] : parsed)
                ini.insert_or_assign(key, value);
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

    const IniMap &profile = result.profile;

    sla::LayersToMeshParams params;

    const auto display_width  = get_value<double>(profile, "display_width");
    const auto display_height = get_value<double>(profile, "display_height");
    if (!display_width || *display_width <= 0.)
        return tl::make_unexpected(missing("display_width"));
    if (!display_height || *display_height <= 0.)
        return tl::make_unexpected(missing("display_height"));
    params.display_width_mm  = *display_width;
    params.display_height_mm = *display_height;

    const auto pixels_x = get_value<int>(profile, "display_pixels_x");
    const auto pixels_y = get_value<int>(profile, "display_pixels_y");
    if (!pixels_x || *pixels_x < 2)
        return tl::make_unexpected(missing("display_pixels_x"));
    if (!pixels_y || *pixels_y < 2)
        return tl::make_unexpected(missing("display_pixels_y"));

    // The size of a pixel as the reader has always derived it: the display size
    // spread over the pixels between the first and the last one.
    params.pixel_width_mm  = *display_width / (*pixels_x - 1);
    params.pixel_height_mm = *display_height / (*pixels_y - 1);

    params.mirror_x = get_bool(profile, "display_mirror_x").value_or(false);
    params.mirror_y = get_bool(profile, "display_mirror_y").value_or(false);

    // An absent orientation means the usual landscape display.
    const std::string orientation = get_string(profile, "display_orientation").value_or("");
    params.portrait               = boost::algorithm::iequals(orientation, "portrait");

    // config.ini carries the layer height for the files written before the
    // profile was embedded, so it is the fallback.
    auto layer_height = get_value<double>(profile, "layer_height");
    if (!layer_height)
        layer_height = get_value<double>(profile, "layerHeight");
    if (!layer_height || *layer_height <= 0.)
        return tl::make_unexpected(missing("layer_height"));
    params.layer_height_mm = *layer_height;

    // An initial layer height of zero means the same as the layer height.
    params.first_layer_height_mm = get_value<double>(profile, "initial_layer_height").value_or(0.);
    if (params.first_layer_height_mm <= 0.)
        params.first_layer_height_mm = params.layer_height_mm;

    std::vector<sla::GrayLayerImage> decoded;
    decoded.reserve(layers.size());
    for (const auto &[name, index] : layers) {
        if (stop && stop())
            return Sl1ImportResult{};

        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip.archive(), index, &stat))
            return tl::make_unexpected(
                fmt::format("Cannot find the layer image {} in the archive.", name));

        auto data = extract_entry(zip.archive(), stat);
        if (!data)
            return tl::make_unexpected(data.error());

        auto layer = decode_layer(*data);
        if (!layer)
            return tl::make_unexpected(fmt::format("Layer image {}: {}", name, layer.error()));

        decoded.push_back(std::move(*layer));
    }

    result.mesh = sla::layers_to_mesh(decoded, params, stop);

    return result;
}

} // namespace Slic3r::Biz::PrintHost::Sla
