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
#include <array>
#include <algorithm>

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

// Missing keys, or values that are neither double nor int, fall back to `def`.
static float get_cfg_value_f(const Domain::ConfigView &cfg, const std::string &key, const float &def = 0.f)
{
    const auto it = cfg.values().find(key);
    if (it == cfg.values().end())
        return def;
    if (it->second.holds_alternative<double>())
        return float(it->second.get<double>());
    if (it->second.holds_alternative<int>())
        return float(it->second.get<int>());
    return def;
}

static int get_cfg_value_i(const Domain::ConfigView &cfg, const std::string &key, const int &def = 0)
{
    const auto it = cfg.values().find(key);
    if (it == cfg.values().end())
        return def;
    if (it->second.holds_alternative<int>())
        return it->second.get<int>();
    if (it->second.holds_alternative<double>())
        return int(it->second.get<double>());
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

namespace {

constexpr std::uint32_t PM5_FORMAT_VERSION = 517;
constexpr std::uint32_t PM5_AREA_NUM = 9;
constexpr std::uint32_t PM5_PREVIEW_W = 224;
constexpr std::uint32_t PM5_PREVIEW_H = 168;
constexpr std::uint32_t PM5_PREVIEW_DPI = 120;
constexpr std::uint32_t PM5_LAYER_COLOR_LEVELS = 16;
constexpr std::uint32_t PM5_LAYERDEF_ENTRY_SIZE = 32;
constexpr std::uint32_t PM5_HEADER_PAYLOAD_SIZE = 92;
constexpr std::uint32_t PM5_PREVIEW_DECLARED_SIZE = 75292;
constexpr std::uint32_t PM5_EXTRA_DECLARED_SIZE = 24;
constexpr std::uint32_t PM5_MACHINE_DECLARED_SIZE = 156;
constexpr std::uint32_t PM5_MODEL_DECLARED_SIZE = 0;

// Each literal is 12 characters plus the terminating NUL, so the arrays are 13 bytes; C++ rejects
// [12] ("array bounds overflow"), unlike C. Every write passes an explicit length of 12.
constexpr char PM5_TAG_INTRO[] = "ANYCUBIC\0\0\0\0";
constexpr char PM5_TAG_HEADER[] = "HEADER\0\0\0\0\0\0";
constexpr char PM5_TAG_PREVIEW[] = "PREVIEW\0\0\0\0\0";
constexpr char PM5_TAG_LAYERDEF[] = "LAYERDEF\0\0\0\0";
constexpr char PM5_TAG_EXTRA[] = "EXTRA\0\0\0\0\0\0\0";
constexpr char PM5_TAG_MACHINE[] = "MACHINE\0\0\0\0\0";
constexpr char PM5_TAG_MODEL[] = "MODEL\0\0\0\0\0\0\0";
static_assert(sizeof(PM5_TAG_INTRO) == 13 && sizeof(PM5_TAG_LAYERDEF) == 13);

const std::uint8_t PM5_COLOR_TABLE[16] = {
    0x0F, 0x1F, 0x2F, 0x3F, 0x4F, 0x5F, 0x6F, 0x7F,
    0x8F, 0x9F, 0xAF, 0xBF, 0xCF, 0xDF, 0xEF, 0xFF
};

static uint32_t count_lit_pixels_pw0(const uint8_t* data, size_t size) {
    uint32_t lit_count = 0;
    size_t i = 0;
    while (i < size) {
        uint8_t byte = data[i++];
        uint8_t grey = byte >> 4;
        uint8_t run_low = byte & 0x0F;
        uint32_t run_len;
        if (grey == 0x0 || grey == 0xF) {
            if (i >= size) break;
            run_len = (static_cast<uint32_t>(run_low) << 8) | data[i++];
        } else {
            run_len = run_low;
        }
        if (grey != 0) {
            lit_count += run_len;
        }
    }
    return lit_count;
}

static void write_string_padded(std::ofstream& out, const std::string& str, size_t size) {
    size_t len = std::min(str.size(), size);
    out.write(str.c_str(), len);
    if (len < size) {
        std::vector<char> pad(size - len, '\0');
        out.write(pad.data(), size - len);
    }
}

} // namespace

void store_pm5(const std::string& file_path, const Biz::Slicing::SLAResultData& data)
{
    if (!data.print_statistics.has_value()) {
        throw std::runtime_error("Cannot write a .pm5 file: the slicing result has no print statistics.");
    }
    const auto& stats = *data.print_statistics;
    const Domain::ConfigView& cfg = data.config;
    std::uint32_t layer_count = static_cast<std::uint32_t>(data.files.data.size());

    // Compute values from config
    float pixel_size_um = 0.0f;
    {
        float display_w = get_cfg_value_f(cfg, "display_width");
        int res_x = get_cfg_value_i(cfg, "display_pixels_x");
        if (display_w > 0 && res_x > 0) {
            pixel_size_um = (display_w * 1000.0f) / res_x; // mm to um
        } else {
            pixel_size_um = 19.0f; // fallback for M5
        }
    }

    float layer_height_mm = get_cfg_value_f(cfg, "layer_height");
    float initial_layer_height_mm = get_cfg_value_f(cfg, "initial_layer_height");
    float exposure_time_s = get_cfg_value_f(cfg, "exposure_time");
    float initial_exposure_time_s = get_cfg_value_f(cfg, "initial_exposure_time");
    std::uint32_t bottom_layer_count = static_cast<std::uint32_t>(get_cfg_value_i(cfg, "faded_layers"));
    if (layer_count < bottom_layer_count) {
        bottom_layer_count = layer_count;
    }
    std::uint32_t res_x = static_cast<std::uint32_t>(get_cfg_value_i(cfg, "display_pixels_x"));
    std::uint32_t res_y = static_cast<std::uint32_t>(get_cfg_value_i(cfg, "display_pixels_y"));

    float bottle_weight_g = get_cfg_value_f(cfg, "bottle_weight") * 1000.0f;
    float bottle_volume_ml = get_cfg_value_f(cfg, "bottle_volume");
    float bottle_cost = get_cfg_value_f(cfg, "bottle_cost");
    float material_density = (bottle_volume_ml > 0) ? (bottle_weight_g / bottle_volume_ml) : 1.0f;

    float volume_ml = (stats.objects_used_material + stats.support_used_material) / 1000.0f;
    float weight_g = volume_ml * material_density;
    float price = (bottle_volume_ml > 0) ? (volume_ml * bottle_cost / bottle_volume_ml) : 0.0f;

    std::uint32_t print_time_s = static_cast<std::uint32_t>(
        (bottom_layer_count * initial_exposure_time_s) +
        ((layer_count - bottom_layer_count) * exposure_time_s) +
        (layer_count * 8.0f / 3.0f) + // lift_distance / retract_speed (using defaults)
        (layer_count * 8.0f / 6.0f) + // lift_distance / lift_speed
        (layer_count * 0.5f) // delay_before_exposure
    );

    float display_width_mm = get_cfg_value_f(cfg, "display_width");
    float display_height_mm = get_cfg_value_f(cfg, "display_height");
    float max_print_height_mm = get_cfg_value_f(cfg, "max_print_height");

    // Bounding box from result data
    float bbox_min[3] = {0, 0, 0};
    float bbox_max[3] = {0, 0, 0};
    bool has_bbox = false;
    if (!data.files.data.empty()) {
        // The slices are in data.slices, but we don't have direct access here.
        // Check if there's bounding box info in the result.
        // For now, write zeros as per spec: "If the result data has no bounding box, write zeros"
    }

    // Open file
    std::ofstream out;
    out.open(file_path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Failed to open file for writing: " + file_path);
    }

    // Record positions for the 9 address table entries
    // Order in address table: header, software, preview, color_table, layerdef, extra, machine, first_layer, model
    std::streamoff addr_header = 0;
    std::streamoff addr_software = 0;
    std::streamoff addr_preview = 0;
    std::streamoff addr_color_table = 0;
    std::streamoff addr_layerdef = 0;
    std::streamoff addr_extra = 0;
    std::streamoff addr_machine = 0;
    std::streamoff addr_first_layer = 0;
    std::streamoff addr_model = 0;

    // Write intro (placeholder, will seek back to fill)
    std::streamoff intro_pos = out.tellp();
    out.write(PM5_TAG_INTRO, 12);
    anycubicsla_write_int32(out, PM5_FORMAT_VERSION);
    anycubicsla_write_int32(out, PM5_AREA_NUM);
    // 9 addresses - write zeros for now
    for (int i = 0; i < 9; ++i) {
        anycubicsla_write_int32(out, 0);
    }

    // HEADER section
    addr_header = static_cast<std::streamoff>(out.tellp());
    out.write(PM5_TAG_HEADER, 12);
    anycubicsla_write_int32(out, PM5_HEADER_PAYLOAD_SIZE);
    anycubicsla_write_float(out, pixel_size_um);
    anycubicsla_write_float(out, layer_height_mm);
    anycubicsla_write_float(out, exposure_time_s);
    anycubicsla_write_float(out, 0.5f); // delay_before_exposure_s
    anycubicsla_write_float(out, initial_exposure_time_s);
    anycubicsla_write_float(out, static_cast<float>(bottom_layer_count));
    anycubicsla_write_float(out, 8.0f); // lift_height_mm
    anycubicsla_write_float(out, 6.0f); // lift_speed
    anycubicsla_write_float(out, 6.0f); // retract_speed (sample value, likely)
    anycubicsla_write_float(out, volume_ml);
    anycubicsla_write_int32(out, PM5_LAYER_COLOR_LEVELS);
    anycubicsla_write_int32(out, res_x);
    anycubicsla_write_int32(out, res_y);
    anycubicsla_write_float(out, weight_g);
    anycubicsla_write_float(out, price);
    anycubicsla_write_int32(out, '$');
    anycubicsla_write_int32(out, 0); // per_layer_override
    anycubicsla_write_int32(out, print_time_s);
    anycubicsla_write_int32(out, 10); // transition_layer_count
    anycubicsla_write_int32(out, 0); // transition_type
    anycubicsla_write_int32(out, 0); // unknown
    anycubicsla_write_int32(out, 0x00030000); // unknown
    anycubicsla_write_int32(out, 10); // unknown

    // PREVIEW section
    addr_preview = static_cast<std::streamoff>(out.tellp());
    out.write(PM5_TAG_PREVIEW, 12);
    anycubicsla_write_int32(out, PM5_PREVIEW_DECLARED_SIZE);
    anycubicsla_write_int32(out, PM5_PREVIEW_W);
    anycubicsla_write_int32(out, PM5_PREVIEW_DPI);
    anycubicsla_write_int32(out, PM5_PREVIEW_H);
    
    // Generate preview pixels (RGB565, bottom-up)
    std::vector<uint8_t> preview_pixels(PM5_PREVIEW_W * PM5_PREVIEW_H * 2, 0);
    if (!data.thumbnails.empty()) {
        const auto& t = data.thumbnails[0];
        if (t.pixels.size() == PM5_PREVIEW_W * PM5_PREVIEW_H * 4) {
            size_t dst_index = PM5_PREVIEW_W * (PM5_PREVIEW_H - 1) * 2;
            size_t pixel_x = 0;
            for (size_t i = 0; i < t.pixels.size(); i += 4) {
                uint32_t r = t.pixels[i];
                uint32_t g = t.pixels[i + 1];
                uint32_t b = t.pixels[i + 2];
                uint32_t pixel = ((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3);
                preview_pixels[dst_index++] = pixel & 0xFF;
                preview_pixels[dst_index++] = (pixel >> 8) & 0xFF;
                pixel_x++;
                if (pixel_x == PM5_PREVIEW_W) {
                    pixel_x = 0;
                    dst_index -= PM5_PREVIEW_W * 4;
                }
            }
        }
    }
    out.write(reinterpret_cast<const char*>(preview_pixels.data()), preview_pixels.size());
    // 16 zero bytes after pixel data
    std::array<uint8_t, 16> preview_padding = {0};
    out.write(reinterpret_cast<const char*>(preview_padding.data()), preview_padding.size());

    // Layer image colour table (no section header)
    addr_color_table = static_cast<std::streamoff>(out.tellp());
    anycubicsla_write_int32(out, 0); // use full greyscale = off
    anycubicsla_write_int32(out, PM5_LAYER_COLOR_LEVELS);
    out.write(reinterpret_cast<const char*>(PM5_COLOR_TABLE), 16);
    anycubicsla_write_int32(out, 0);

    // LAYERDEF section
    addr_layerdef = static_cast<std::streamoff>(out.tellp());
    out.write(PM5_TAG_LAYERDEF, 12);
    // Payload size: 4 (layer_count) + layer_count * 32
    std::uint32_t layerdef_payload_size = 4 + layer_count * PM5_LAYERDEF_ENTRY_SIZE;
    anycubicsla_write_int32(out, layerdef_payload_size);
    anycubicsla_write_int32(out, layer_count);

    // We need to compute layer offsets and lit pixel counts
    // First, collect all layer data sizes and lit counts
    std::vector<std::uint32_t> layer_sizes(layer_count);
    std::vector<std::uint32_t> layer_lit_counts(layer_count);
    std::uint64_t total_layer_data_size = 0;
    for (std::uint32_t i = 0; i < layer_count; ++i) {
        layer_sizes[i] = static_cast<std::uint32_t>(data.files.data[i].size());
        layer_lit_counts[i] = count_lit_pixels_pw0(data.files.data[i].data(), data.files.data[i].size());
        total_layer_data_size += layer_sizes[i];
    }

    // First layer image offset will be after MACHINE and software block
    // But we don't know those sizes yet. We'll write layerdef entries with placeholder offsets,
    // then come back and fix them, OR compute all offsets upfront.
    // Let's compute all section sizes first to determine offsets.

    // For now, write placeholder layer entries, then fix later
    std::vector<std::streamoff> layer_entry_positions(layer_count);
    // Write placeholder layer entries
    for (std::uint32_t i = 0; i < layer_count; ++i) {
        layer_entry_positions[i] = static_cast<std::streamoff>(out.tellp());
        // Placeholder: offset=0, size, lift params, exposure, layer_height, lit_count, 0
        anycubicsla_write_int32(out, 0); // image_offset (placeholder)
        anycubicsla_write_int32(out, layer_sizes[i]);
        anycubicsla_write_float(out, 8.0f); // lift height, mm (sample value, not yet from config)
        anycubicsla_write_float(out, 6.0f); // lift speed (sample value, not yet from config)
        anycubicsla_write_float(out, i < bottom_layer_count ? initial_exposure_time_s : exposure_time_s);
        // Only the first layer is sliced at initial_layer_height; every other layer, bottom layers
        // included, is sliced at layer_height. The printer moves Z by this value, so giving the
        // bottom layers initial_layer_height would stretch them whenever the two differ.
        anycubicsla_write_float(out, i == 0 ? initial_layer_height_mm : layer_height_mm);
        anycubicsla_write_int32(out, layer_lit_counts[i]);
        anycubicsla_write_int32(out, 0);
    }

    // EXTRA section
    addr_extra = static_cast<std::streamoff>(out.tellp());
    out.write(PM5_TAG_EXTRA, 12);
    anycubicsla_write_int32(out, PM5_EXTRA_DECLARED_SIZE);
    // Sample values: u32 2, then floats 5, 2, 3, 3, 3, 4, then u32 2, then floats 2, 2, 2, 6, 4, 6
    anycubicsla_write_int32(out, 2);
    anycubicsla_write_float(out, 5.0f);
    anycubicsla_write_float(out, 2.0f);
    anycubicsla_write_float(out, 3.0f);
    anycubicsla_write_float(out, 3.0f);
    anycubicsla_write_float(out, 3.0f);
    anycubicsla_write_float(out, 4.0f);
    anycubicsla_write_int32(out, 2);
    anycubicsla_write_float(out, 2.0f);
    anycubicsla_write_float(out, 2.0f);
    anycubicsla_write_float(out, 2.0f);
    anycubicsla_write_float(out, 6.0f);
    anycubicsla_write_float(out, 4.0f);
    anycubicsla_write_float(out, 6.0f);
    // Note: declared 24 bytes but actual span is 56 bytes before MACHINE.
    // The above writes 4 + 6*4 + 4 + 6*4 = 4 + 24 + 4 + 24 = 56 bytes payload.
    // But declared size is 24. We write the actual bytes as in sample.

    // MACHINE section
    addr_machine = static_cast<std::streamoff>(out.tellp());
    out.write(PM5_TAG_MACHINE, 12);
    anycubicsla_write_int32(out, PM5_MACHINE_DECLARED_SIZE);
    // Printer name (96 bytes)
    write_string_padded(out, "Anycubic Photon Mono M5", 96);
    // Image format name (16 bytes). The zeros that follow "pw0Img" in the sample are this field's
    // padding, not separate fields; MACHINE's body is 140 bytes, and the software block starts
    // right after it.
    write_string_padded(out, "pw0Img", 16);
    anycubicsla_write_int32(out, 16);
    anycubicsla_write_int32(out, 7);
    anycubicsla_write_float(out, display_width_mm);
    anycubicsla_write_float(out, display_height_mm);
    anycubicsla_write_float(out, max_print_height_mm);
    anycubicsla_write_int32(out, PM5_FORMAT_VERSION);
    // 4 bytes: 01 47 63 00
    std::array<uint8_t, 4> machine_unknown = {0x01, 0x47, 0x63, 0x00};
    out.write(reinterpret_cast<const char*>(machine_unknown.data()), 4);

    // Software block (no section header)
    addr_software = static_cast<std::streamoff>(out.tellp());
    // Measured in the sample: a 32-byte NUL-padded name, a u32 equal to the whole block's length
    // (164), then 128 bytes of strings. Those strings run together inside three padded areas
    // (version and build date in the first 32 bytes, platform and libraries in the next 64, the
    // OpenGL profile in the last 32), so their exact field boundaries are not known. We keep the
    // same three areas and the same total, with our own identity.
    constexpr std::uint32_t PM5_SOFTWARE_BLOCK_SIZE = 164;
    write_string_padded(out, SLIC3R_APP_NAME, 32);
    anycubicsla_write_int32(out, PM5_SOFTWARE_BLOCK_SIZE);
    write_string_padded(out, std::string(SLIC3R_VERSION) + " " + __DATE__, 32);
    write_string_padded(out, "win-x64", 64);
    write_string_padded(out, "", 32);

    // MODEL section. In the sample it sits after the software block and BEFORE the layer images,
    // and spans 48 bytes: name, declared length 0, six bounding-box floats, then 8 zero bytes.
    addr_model = static_cast<std::streamoff>(out.tellp());
    out.write(PM5_TAG_MODEL, 12);
    anycubicsla_write_int32(out, PM5_MODEL_DECLARED_SIZE);
    // 6 floats: bbox min x, y, z, max x, y, z (zeros when the result carries no bounding box)
    for (int i = 0; i < 3; ++i) {
        anycubicsla_write_float(out, bbox_min[i]);
    }
    for (int i = 0; i < 3; ++i) {
        anycubicsla_write_float(out, bbox_max[i]);
    }
    anycubicsla_write_int32(out, 0);
    anycubicsla_write_int32(out, 0);

    // Layer image data, last
    addr_first_layer = static_cast<std::streamoff>(out.tellp());
    for (std::uint32_t i = 0; i < layer_count; ++i) {
        out.write(reinterpret_cast<const char*>(data.files.data[i].data()),
                  static_cast<std::streamsize>(data.files.data[i].size()));
    }

    // Now go back and fill in the layer entry offsets
    std::streamoff current_layer_offset = addr_first_layer;
    for (std::uint32_t i = 0; i < layer_count; ++i) {
        out.seekp(layer_entry_positions[i]);
        anycubicsla_write_int32(out, static_cast<std::uint32_t>(current_layer_offset));
        current_layer_offset += static_cast<std::streamoff>(layer_sizes[i]);
    }

    // Finally, go back and fill the intro address table
    out.seekp(intro_pos + 12 + 4 + 4); // after tag, version, area_num
    anycubicsla_write_int32(out, static_cast<std::uint32_t>(addr_header));
    anycubicsla_write_int32(out, static_cast<std::uint32_t>(addr_software));
    anycubicsla_write_int32(out, static_cast<std::uint32_t>(addr_preview));
    anycubicsla_write_int32(out, static_cast<std::uint32_t>(addr_color_table));
    anycubicsla_write_int32(out, static_cast<std::uint32_t>(addr_layerdef));
    anycubicsla_write_int32(out, static_cast<std::uint32_t>(addr_extra));
    anycubicsla_write_int32(out, static_cast<std::uint32_t>(addr_machine));
    anycubicsla_write_int32(out, static_cast<std::uint32_t>(addr_first_layer));
    anycubicsla_write_int32(out, static_cast<std::uint32_t>(addr_model));

    out.close();
}

} // namespace Slic3r::Biz::PrintHost::Sla