#include "Slic3r/Biz/ResultExport/SLA/AnycubicSLA.hpp"

#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/Image.hpp"
#include "Slic3r/Biz/Algorithms/ImageUtils.hpp"
#include "Slic3r/Time.hpp"
#include "Slic3r/Utils.hpp"
#include "Slic3r/Version.hpp"
#include "libslic3r/SLAResult.hpp"

#include <LocalesUtils.hpp>
#include <sstream>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/algorithm/string.hpp>
#include "Slic3r/Log.hpp"

using namespace Slic3r::Biz::Slicing;
using Slic3r::Domain::EnumVectorWrapper;

namespace Slic3r::Biz::PrintHost::Sla {

namespace {

#define TAG_INTRO "ANYCUBIC\0\0\0\0"
#define TAG_HEADER "HEADER\0\0\0\0\0\0"
#define TAG_PREVIEW "PREVIEW\0\0\0\0\0"
#define TAG_LAYERS "LAYERDEF\0\0\0\0"

#define PREV_W 224
#define PREV_H 168
#define PREV_DPI 42

typedef struct anycubicsla_format_intro
{
    char          tag[12];
    std::uint32_t version;
    std::uint32_t area_num;
    std::uint32_t header_data_offset;
    std::uint32_t software_data_offset;
    std::uint32_t preview_data_offset;
    std::uint32_t layer_color_offset;
    std::uint32_t layer_data_offset;
    std::uint32_t extra_data_offset;
    std::uint32_t image_data_offset;
} anycubicsla_format_intro;

typedef struct anycubicsla_format_header
{
    char          tag[12];
    std::uint32_t payload_size;
    float         pixel_size_um;
    float         layer_height_mm;
    float         exposure_time_s;
    float         delay_before_exposure_s;
    float         bottom_exposure_time_s;
    float         bottom_layer_count;
    float         lift_distance_mm;
    float         lift_speed_mms;
    float         retract_speed_mms;
    float         volume_ml;
    std::uint32_t antialiasing;
    std::uint32_t res_x;
    std::uint32_t res_y;
    float         weight_g;
    float         price;
    std::uint32_t price_currency;
    std::uint32_t per_layer_override;
    std::uint32_t print_time_s;
    std::uint32_t transition_layer_count;
    std::uint32_t transition_layer_type;
} anycubicsla_format_header;

typedef struct anycubicsla_format_preview
{
    char          tag[12];
    std::uint32_t payload_size;
    std::uint32_t preview_w;
    std::uint32_t preview_dpi;
    std::uint32_t preview_h;
    std::uint8_t pixels[PREV_W * PREV_H * 2];
} anycubicsla_format_preview;

typedef struct anycubicsla_format_layers_header
{
    char          tag[12];
    std::uint32_t payload_size;
    std::uint32_t layer_count;
} anycubicsla_format_layers_header;

typedef struct anycubicsla_format_layer
{
    std::uint32_t image_offset;
    std::uint32_t image_size;
    float         lift_distance_mm;
    float         lift_speed_mms;
    float         exposure_time_s;
    float         layer_height_mm;
    float         layer44;
    float         layer48;
} anycubicsla_format_layer;

typedef struct anycubicsla_format_misc
{
    float bottom_layer_height_mm;
    float bottom_lift_distance_mm;
    float bottom_lift_speed_mms;
} anycubicsla_format_misc;

static void anycubicsla_write_int32(std::ofstream &out, std::uint32_t val)
{
    const char i1 = (val & 0xFF);
    const char i2 = (val >> 8) & 0xFF;
    const char i3 = (val >> 16) & 0xFF;
    const char i4 = (val >> 24) & 0xFF;

    out.write((const char *) &i1, 1);
    out.write((const char *) &i2, 1);
    out.write((const char *) &i3, 1);
    out.write((const char *) &i4, 1);
}

static void anycubicsla_write_float(std::ofstream &out, float val)
{
    std::uint32_t *f = (std::uint32_t *) &val;
    anycubicsla_write_int32(out, *f);
}

static void anycubicsla_write_intro(std::ofstream &out, anycubicsla_format_intro &i)
{
    out.write(TAG_INTRO, sizeof(i.tag));
    anycubicsla_write_int32(out, i.version);
    anycubicsla_write_int32(out, i.area_num);
    anycubicsla_write_int32(out, i.header_data_offset);
    anycubicsla_write_int32(out, i.software_data_offset);
    anycubicsla_write_int32(out, i.preview_data_offset);
    anycubicsla_write_int32(out, i.layer_color_offset);
    anycubicsla_write_int32(out, i.layer_data_offset);
    anycubicsla_write_int32(out, i.extra_data_offset);
    anycubicsla_write_int32(out, i.image_data_offset);
}

static void anycubicsla_write_header(std::ofstream &out, anycubicsla_format_header &h)
{
    out.write(TAG_HEADER, sizeof(h.tag));
    anycubicsla_write_int32(out, h.payload_size);
    anycubicsla_write_float(out, h.pixel_size_um);
    anycubicsla_write_float(out, h.layer_height_mm);
    anycubicsla_write_float(out, h.exposure_time_s);
    anycubicsla_write_float(out, h.delay_before_exposure_s);
    anycubicsla_write_float(out, h.bottom_exposure_time_s);
    anycubicsla_write_float(out, h.bottom_layer_count);
    anycubicsla_write_float(out, h.lift_distance_mm);
    anycubicsla_write_float(out, h.lift_speed_mms);
    anycubicsla_write_float(out, h.retract_speed_mms);
    anycubicsla_write_float(out, h.volume_ml);
    anycubicsla_write_int32(out, h.antialiasing);
    anycubicsla_write_int32(out, h.res_x);
    anycubicsla_write_int32(out, h.res_y);
    anycubicsla_write_float(out, h.weight_g);
    anycubicsla_write_float(out, h.price);
    anycubicsla_write_int32(out, h.price_currency);
    anycubicsla_write_int32(out, h.per_layer_override);
    anycubicsla_write_int32(out, h.print_time_s);
    anycubicsla_write_int32(out, h.transition_layer_count);
    anycubicsla_write_int32(out, h.transition_layer_type);
}

static void anycubicsla_write_preview(std::ofstream &out, anycubicsla_format_preview &p)
{
    out.write(TAG_PREVIEW, sizeof(p.tag));
    anycubicsla_write_int32(out, p.payload_size);
    anycubicsla_write_int32(out, p.preview_w);
    anycubicsla_write_int32(out, p.preview_dpi);
    anycubicsla_write_int32(out, p.preview_h);
    out.write((const char*) p.pixels, sizeof(p.pixels));
}

static void anycubicsla_write_layers_header(std::ofstream &out, anycubicsla_format_layers_header &h)
{
    out.write(TAG_LAYERS, sizeof(h.tag));
    anycubicsla_write_int32(out, h.payload_size);
    anycubicsla_write_int32(out, h.layer_count);
}

static void anycubicsla_write_layer(std::ofstream &out, anycubicsla_format_layer &l)
{
    anycubicsla_write_int32(out, l.image_offset);
    anycubicsla_write_int32(out, l.image_size);
    anycubicsla_write_float(out, l.lift_distance_mm);
    anycubicsla_write_float(out, l.lift_speed_mms);
    anycubicsla_write_float(out, l.exposure_time_s);
    anycubicsla_write_float(out, l.layer_height_mm);
    anycubicsla_write_float(out, l.layer44);
    anycubicsla_write_float(out, l.layer48);
}

static float get_cfg_value_f(const Domain::ConfigView &cfg, const std::string &key, const float &def = 0.f)
{
    if (cfg.has(key)) {
        if (auto opt = cfg.option(key))
            return opt->getFloat();
    }
    return def;
}

static int get_cfg_value_i(const Domain::ConfigView &cfg, const std::string &key, const int &def = 0)
{
    if (cfg.has(key)) {
        if (auto opt = cfg.option(key))
            return opt->getInt();
    }
    return def;
}

template<class T> void crop_value(T &val, T val_min, T val_max)
{
    if (val < val_min) {
        val = val_min;
    } else if (val > val_max) {
        val = val_max;
    }
}

static void fill_preview(anycubicsla_format_preview &p, const Domain::Images &thumbnails)
{
    p.preview_w    = PREV_W;
    p.preview_h    = PREV_H;
    p.preview_dpi  = PREV_DPI;
    p.payload_size = sizeof(p) - sizeof(p.tag) - sizeof(p.payload_size);

    std::memset(p.pixels, 0 , sizeof(p.pixels));
    if (!thumbnails.empty()) {
        std::uint32_t dst_index;
        std::uint32_t i = 0;
        size_t len;
        size_t pixel_x = 0;
        auto t = thumbnails[0];
        len = t.pixels.size();
        if (len != PREV_W * PREV_H * 4) {
            return;
        }
        dst_index = (PREV_W * (PREV_H - 1) * 2);
        while (i < len) {
            std::uint32_t pixel;
            std::uint32_t r = t.pixels[i++];
            std::uint32_t g = t.pixels[i++];
            std::uint32_t b = t.pixels[i++];
            i++;
            pixel = ((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3);
            p.pixels[dst_index++] = pixel & 0xFF;
            p.pixels[dst_index++] = (pixel >> 8) & 0xFF;
            pixel_x++;
            if (pixel_x == PREV_W) {
                pixel_x = 0;
                dst_index -= (PREV_W * 4);
            }
        }
    }
}

static void fill_header_and_misc(anycubicsla_format_header &h,
                                 anycubicsla_format_misc &m,
                                 const Domain::ConfigView &cfg,
                                 const Domain::SLA::PrintStatistics &stats,
                                 std::uint32_t layer_count)
{
    CNumericLocalesSetter locales_setter;

    float bottle_weight_g = cfg.get<double>("bottle_weight") * 1000.0f;
    float bottle_volume_ml = cfg.get<double>("bottle_volume");
    float bottle_cost = cfg.get<double>("bottle_cost");
    float material_density = bottle_weight_g / bottle_volume_ml;

    h.layer_height_mm        = get_cfg_value_f(cfg, "layer_height");
    m.bottom_layer_height_mm = get_cfg_value_f(cfg, "initial_layer_height");
    h.exposure_time_s        = get_cfg_value_f(cfg, "exposure_time");
    h.bottom_exposure_time_s = get_cfg_value_f(cfg, "initial_exposure_time");
    h.bottom_layer_count     = get_cfg_value_i(cfg, "faded_layers");
    if (layer_count < h.bottom_layer_count) {
        h.bottom_layer_count = layer_count;
    }
    h.res_x     = get_cfg_value_i(cfg, "display_pixels_x");
    h.res_y     = get_cfg_value_i(cfg, "display_pixels_y");

    h.volume_ml = (stats.objects_used_material + stats.support_used_material) / 1000.0f;
    h.weight_g  = h.volume_ml * material_density;
    h.price     = (h.volume_ml * bottle_cost) / bottle_volume_ml;
    h.price_currency = '$';
    h.antialiasing = 1;
    h.per_layer_override = 0;
    h.delay_before_exposure_s = 0.5f;
    crop_value(h.delay_before_exposure_s, 0.0f, 1000.0f);

    h.lift_distance_mm = 8.0f;
    crop_value(h.lift_distance_mm, 0.0f, 100.0f);

    m.bottom_lift_distance_mm = h.lift_distance_mm;
    h.lift_speed_mms = 2.0f;
    crop_value(h.lift_speed_mms, 0.1f, 20.0f);

    m.bottom_lift_speed_mms = h.lift_speed_mms;
    crop_value(m.bottom_lift_speed_mms, 0.1f, 20.0f);

    h.retract_speed_mms = 3.0f;
    crop_value(h.retract_speed_mms, 0.1f, 20.0f);

    h.print_time_s = static_cast<std::uint32_t>(
        (h.bottom_layer_count * h.bottom_exposure_time_s) +
        ((layer_count - h.bottom_layer_count) * h.exposure_time_s) +
        (layer_count * h.lift_distance_mm / h.retract_speed_mms) +
        (layer_count * h.lift_distance_mm / h.lift_speed_mms) +
        (layer_count * h.delay_before_exposure_s)
    );

    h.payload_size = sizeof(h) - sizeof(h.tag) - sizeof(h.payload_size);

    std::string archive_format = cfg.get<std::string>("sla_archive_format");
    boost::algorithm::to_lower(archive_format);
    
    float pixel_size_um = 50.0f;
    if (archive_format == "pwmo") {
        pixel_size_um = 50.0f;  // Photon Mono: 50um
    } else if (archive_format == "pwmx") {
        pixel_size_um = 35.0f;  // Photon Mono X: 35um (6.08" 2560x1440 -> 2560px in 90mm = 35.1um)
    } else if (archive_format == "pwms") {
        pixel_size_um = 35.0f;  // Photon Mono SE: 35um
    }
    h.pixel_size_um = pixel_size_um;
    h.transition_layer_count = 0;
    h.transition_layer_type = 0;
}

} // namespace

void store_anycubic(const std::string& file_path, const Biz::Slicing::SLAResultData& data)
{
    const auto& stats = *data.print_statistics;
    const Domain::ConfigView& cfg = data.config;
    std::uint32_t layer_count = static_cast<std::uint32_t>(data.files.data.size());

    anycubicsla_format_intro         intro = {};
    anycubicsla_format_header        header = {};
    anycubicsla_format_preview       preview = {};
    anycubicsla_format_layers_header layers_header = {};
    anycubicsla_format_misc          misc = {};
    std::vector<uint8_t>      layer_images;

    constexpr std::uint16_t ANYCUBIC_SLA_FORMAT_VERSION_1 = 1;
    intro.version             = ANYCUBIC_SLA_FORMAT_VERSION_1;
    intro.area_num            = 4;
    intro.header_data_offset  = sizeof(intro);
    intro.preview_data_offset = sizeof(intro) + sizeof(header);
    intro.layer_data_offset   = intro.preview_data_offset + sizeof(preview);
    intro.image_data_offset = intro.layer_data_offset +
                              sizeof(layers_header) +
                              (sizeof(anycubicsla_format_layer) * layer_count);
    intro.software_data_offset = 0;
    intro.layer_color_offset = 0;
    intro.extra_data_offset = 0;

    fill_header_and_misc(header, misc, cfg, stats, layer_count);
    fill_preview(preview, data.thumbnails);

    try {
        std::ofstream out;
        out.open(file_path, std::ios::binary | std::ios::out | std::ios::trunc);
        anycubicsla_write_intro(out, intro);
        anycubicsla_write_header(out, header);
        anycubicsla_write_preview(out, preview);

        layers_header.payload_size = intro.image_data_offset - intro.layer_data_offset -
                        sizeof(layers_header.tag)  - sizeof(layers_header.payload_size);
        layers_header.layer_count = layer_count;
        anycubicsla_write_layers_header(out, layers_header);

        layer_images.reserve(layer_count * 32768);
        std::uint32_t image_offset = intro.image_data_offset;
        
        for (std::uint32_t i = 0; i < layer_count; ++i) {
            anycubicsla_format_layer l = {};
            l.image_offset = image_offset;
            l.image_size = static_cast<std::uint32_t>(data.files.data[i].size());
            
            if (i < header.bottom_layer_count) {
                l.exposure_time_s = header.bottom_exposure_time_s;
                l.layer_height_mm = misc.bottom_layer_height_mm;
                l.lift_distance_mm = misc.bottom_lift_distance_mm;
                l.lift_speed_mms = misc.bottom_lift_speed_mms;
            } else {
                l.exposure_time_s = header.exposure_time_s;
                l.layer_height_mm = header.layer_height_mm;
                l.lift_distance_mm = header.lift_distance_mm;
                l.lift_speed_mms = header.lift_speed_mms;
            }
            l.layer44 = 0.0f;
            l.layer48 = 0.0f;
            
            image_offset += l.image_size;
            anycubicsla_write_layer(out, l);
            
            const char* img_start = reinterpret_cast<const char*>(data.files.data[i].data());
            const char* img_end = img_start + data.files.data[i].size();
            std::copy(img_start, img_end, std::back_inserter(layer_images));
        }
        
        const char* img_buffer = reinterpret_cast<const char*>(layer_images.data());
        out.write(img_buffer, layer_images.size());
        out.close();
    } catch(std::exception& e) {
        SPDLOG_ERROR("Anycubic export failed: {}", e.what());
        throw;
    }
}

} // namespace Slic3r::Biz::PrintHost::Sla