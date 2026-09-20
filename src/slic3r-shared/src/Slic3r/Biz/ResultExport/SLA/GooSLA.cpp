#include "Slic3r/Biz/ResultExport/SLA/GooSLA.hpp"

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

#define GOO_ENDING "\x00\x00\x00\x07\x00\x00\x00\x44\x4C\x50\x00"
#define GOO_MAGIC_TAG "\x07\x00\x00\x00\x44\x4C\x50\x00"
#define GOO_DELIMITER "\x0D\x0A"

#define PREV_SMALL_W 116
#define PREV_SMALL_H 116
#define PREV_BIG_W 290
#define PREV_BIG_H 290

#pragma pack(push, 1)
typedef struct goo_header_info
{
    char version[4];
    char magic_tag[8];
    char software_info[32];
    char software_version[24];
    char file_time[24];
    char printer_name[32];
    char printer_type[32];
    char profile_name[32];
    int16_t anti_aliasing_level;
    int16_t grey_level;
    int16_t blur_level;
    uint16_t small_preview[PREV_SMALL_W * PREV_SMALL_H];
    char delimiter1[2];
    uint16_t big_preview[PREV_BIG_W * PREV_BIG_H];
    char delimiter2[2];
    int32_t total_layers;
    int16_t x_resolution;
    int16_t y_resolution;
    uint8_t x_mirror;
    uint8_t y_mirror;
    float x_size_platform;
    float y_size_platform;
    float z_size_platform;
    float layer_thickness;
    float common_exposure_time;
    uint8_t exposure_delay_mode;
    float turn_off_time;
    float bottom_before_lift_time;
    float bottom_after_lift_time;
    float bottom_after_retract_time;
    float before_lift_time;
    float after_lift_time;
    float after_retract_time;
    float bottom_exposure_time;
    int32_t bottom_layers;
    float bottom_lift_distance;
    float bottom_lift_speed;
    float lift_distance;
    float lift_speed;
    float bottom_retract_distance;
    float bottom_retract_speed;
    float retract_distance;
    float retract_speed;
    float bottom_second_lift_distance;
    float bottom_second_lift_speed;
    float second_lift_distance;
    float second_lift_speed;
    float bottom_second_retract_distance;
    float bottom_second_retract_speed;
    float second_retract_distance;
    float second_retract_speed;
    int16_t bottom_light_pwm;
    int16_t light_pwm;
    uint8_t advance_mode;
    int32_t printing_time;
    float total_volume;
    float total_weight;
    float total_price;
    char price_unit[8];
    int32_t layer_content_offset;
    uint8_t gray_scale_level;
    int16_t transition_layers;
} goo_header_info;

typedef struct goo_layer_def
{
    int16_t pause_flag;
    float pause_position_z;
    float layer_position_z;
    float layer_exposure_time;
    float layer_off_time;
    float before_lift_time;
    float after_lift_time;
    float after_retract_time;
    float lift_distance;
    float lift_speed;
    float second_lift_distance;
    float second_lift_speed;
    float retract_distance;
    float retract_speed;
    float second_retract_distance;
    float second_retract_speed;
    int16_t light_pwm;
    char delimiter[2];
    int32_t data_size;
} goo_layer_def;
#pragma pack(pop)

static void write_be_int16(std::ofstream &out, int16_t val)
{
    uint8_t b1 = (val >> 8) & 0xFF;
    uint8_t b0 = val & 0xFF;
    out.write((const char*)&b1, 1);
    out.write((const char*)&b0, 1);
}

static void write_be_int32(std::ofstream &out, int32_t val)
{
    uint8_t b3 = (val >> 24) & 0xFF;
    uint8_t b2 = (val >> 16) & 0xFF;
    uint8_t b1 = (val >> 8) & 0xFF;
    uint8_t b0 = val & 0xFF;
    out.write((const char*)&b3, 1);
    out.write((const char*)&b2, 1);
    out.write((const char*)&b1, 1);
    out.write((const char*)&b0, 1);
}

static void write_be_float(std::ofstream &out, float val)
{
    union { float f; uint32_t u; } converter;
    converter.f = val;
    write_be_int32(out, converter.u);
}

static void write_string_padded(std::ofstream &out, const std::string &str, size_t len)
{
    std::string s = str;
    if (s.size() > len) s.resize(len);
    s.resize(len, '\0');
    out.write(s.data(), len);
}

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

static void fill_preview(uint16_t *pixels, int w, int h, const Domain::Images &thumbnails)
{
    std::memset(pixels, 0, w * h * sizeof(uint16_t));
    if (!thumbnails.empty()) {
        auto t = thumbnails[0];
        size_t len = t.pixels.size();
        if (len != static_cast<size_t>(w * h * 4)) {
            return;
        }
        size_t dst_index = (w * (h - 1) * 2);
        size_t i = 0;
        int pixel_x = 0;
        while (i < len) {
            uint32_t r = t.pixels[i++];
            uint32_t g = t.pixels[i++];
            uint32_t b = t.pixels[i++];
            i++;
            uint16_t pixel = ((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3);
            reinterpret_cast<uint8_t*>(pixels)[dst_index++] = pixel & 0xFF;
            reinterpret_cast<uint8_t*>(pixels)[dst_index++] = (pixel >> 8) & 0xFF;
            pixel_x++;
            if (pixel_x == w) {
                pixel_x = 0;
                dst_index -= (w * 4);
            }
        }
    }
}

} // namespace

void store_goo(const std::string& file_path, const Biz::Slicing::SLAResultData& data)
{
    const auto& stats = *data.print_statistics;
    const Domain::ConfigView& cfg = data.config;
    uint32_t layer_count = static_cast<uint32_t>(data.files.data.size());

    goo_header_info header = {};
    std::vector<uint8_t> layer_images;

    std::string version_str = "V3.0";
    std::memcpy(header.version, version_str.data(), std::min<size_t>(4, version_str.size()));
    std::memcpy(header.magic_tag, GOO_MAGIC_TAG, 8);

    std::string sw_info = "PrusaSlicer";
    std::memcpy(header.software_info, sw_info.data(), std::min<size_t>(32, sw_info.size()));

    std::string sw_ver = SLIC3R_VERSION;
    std::memcpy(header.software_version, sw_ver.data(), std::min<size_t>(24, sw_ver.size()));

    CNumericLocalesSetter locales_setter;
    char time_buf[64];
    time_t now = time(nullptr);
    tm *ltm = localtime(&now);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", ltm);
    std::memcpy(header.file_time, time_buf, std::min<size_t>(24, strlen(time_buf)));

    std::string printer_name = cfg.get<std::string>("printer_name");
    std::memcpy(header.printer_name, printer_name.data(), std::min<size_t>(32, printer_name.size()));

    std::string printer_type = cfg.get<std::string>("printer_vendor");
    std::memcpy(header.printer_type, printer_type.data(), std::min<size_t>(32, printer_type.size()));

    std::string profile_name = cfg.get<std::string>("sla_material_profile_id");
    std::memcpy(header.profile_name, profile_name.data(), std::min<size_t>(32, profile_name.size()));

    header.anti_aliasing_level = 1;
    header.grey_level = 4;
    header.blur_level = 0;

    fill_preview(header.small_preview, PREV_SMALL_W, PREV_SMALL_H, data.thumbnails);
    std::memcpy(header.delimiter1, GOO_DELIMITER, 2);

    fill_preview(header.big_preview, PREV_BIG_W, PREV_BIG_H, data.thumbnails);
    std::memcpy(header.delimiter2, GOO_DELIMITER, 2);

    header.total_layers = layer_count;
    header.x_resolution = get_cfg_value_i(cfg, "display_pixels_x");
    header.y_resolution = get_cfg_value_i(cfg, "display_pixels_y");

    header.x_mirror = cfg.get<bool>("display_mirror_x") ? 1 : 0;
    header.y_mirror = cfg.get<bool>("display_mirror_y") ? 1 : 0;

    double dw = cfg.get<double>("display_width");
    double dh = cfg.get<double>("display_height");
    header.x_size_platform = static_cast<float>(dw);
    header.y_size_platform = static_cast<float>(dh);
    header.z_size_platform = cfg.get<double>("printer_build_height");

    header.layer_thickness = get_cfg_value_f(cfg, "layer_height");
    header.common_exposure_time = get_cfg_value_f(cfg, "exposure_time");
    header.exposure_delay_mode = 1;
    header.turn_off_time = 0.5f;

    header.bottom_before_lift_time = get_cfg_value_f(cfg, "initial_lift_distance", 6.0f) / get_cfg_value_f(cfg, "initial_lift_speed", 2.0f);
    header.bottom_after_lift_time = header.bottom_before_lift_time;
    header.bottom_after_retract_time = 0.0f;
    header.before_lift_time = get_cfg_value_f(cfg, "lift_distance", 6.0f) / get_cfg_value_f(cfg, "lift_speed", 2.0f);
    header.after_lift_time = header.before_lift_time;
    header.after_retract_time = 0.0f;
    header.bottom_exposure_time = get_cfg_value_f(cfg, "initial_exposure_time");
    header.bottom_layers = get_cfg_value_i(cfg, "faded_layers");
    if (layer_count < static_cast<uint32_t>(header.bottom_layers)) {
        header.bottom_layers = layer_count;
    }

    header.bottom_lift_distance = get_cfg_value_f(cfg, "initial_lift_distance", 6.0f);
    header.bottom_lift_speed = get_cfg_value_f(cfg, "initial_lift_speed", 2.0f);
    header.lift_distance = get_cfg_value_f(cfg, "lift_distance", 6.0f);
    header.lift_speed = get_cfg_value_f(cfg, "lift_speed", 2.0f);
    header.bottom_retract_distance = get_cfg_value_f(cfg, "initial_retract_distance", 6.0f);
    header.bottom_retract_speed = get_cfg_value_f(cfg, "initial_retract_speed", 3.0f);
    header.retract_distance = get_cfg_value_f(cfg, "retract_distance", 6.0f);
    header.retract_speed = get_cfg_value_f(cfg, "retract_speed", 3.0f);

    header.bottom_second_lift_distance = 0.0f;
    header.bottom_second_lift_speed = 0.0f;
    header.second_lift_distance = 0.0f;
    header.second_lift_speed = 0.0f;
    header.bottom_second_retract_distance = 0.0f;
    header.bottom_second_retract_speed = 0.0f;
    header.second_retract_distance = 0.0f;
    header.second_retract_speed = 0.0f;

    header.bottom_light_pwm = 255;
    header.light_pwm = 255;
    header.advance_mode = 0;

    float print_time = 0.0f;
    for (uint32_t i = 0; i < layer_count; ++i) {
        if (i < static_cast<uint32_t>(header.bottom_layers)) {
            print_time += header.bottom_exposure_time;
            print_time += header.bottom_before_lift_time + header.bottom_after_lift_time + header.bottom_after_retract_time;
        } else {
            print_time += header.common_exposure_time;
            print_time += header.before_lift_time + header.after_lift_time + header.after_retract_time;
        }
    }
    header.printing_time = static_cast<int32_t>(print_time);

    float bottle_weight_g = cfg.get<double>("bottle_weight") * 1000.0;
    float bottle_volume_ml = cfg.get<double>("bottle_volume");
    float bottle_cost = cfg.get<double>("bottle_cost");
    float material_density = (bottle_volume_ml > 0) ? (bottle_weight_g / bottle_volume_ml) : 1.0f;

    header.total_volume = (stats.objects_used_material + stats.support_used_material) / 1000.0f;
    header.total_weight = header.total_volume * material_density;
    header.total_price = (bottle_volume_ml > 0) ? (header.total_volume * bottle_cost / bottle_volume_ml) : 0.0f;

    std::string price_unit = "USD";
    std::memcpy(header.price_unit, price_unit.data(), std::min<size_t>(8, price_unit.size()));

    header.layer_content_offset = sizeof(goo_header_info);
    header.gray_scale_level = 1;
    header.transition_layers = 0;

    try {
        std::ofstream out;
        out.open(file_path, std::ios::binary | std::ios::out | std::ios::trunc);

        out.write(header.version, 4);
        out.write(header.magic_tag, 8);
        out.write(header.software_info, 32);
        out.write(header.software_version, 24);
        out.write(header.file_time, 24);
        out.write(header.printer_name, 32);
        out.write(header.printer_type, 32);
        out.write(header.profile_name, 32);
        write_be_int16(out, header.anti_aliasing_level);
        write_be_int16(out, header.grey_level);
        write_be_int16(out, header.blur_level);

        for (int i = 0; i < PREV_SMALL_W * PREV_SMALL_H; ++i) {
            write_be_int16(out, header.small_preview[i]);
        }
        out.write(header.delimiter1, 2);

        for (int i = 0; i < PREV_BIG_W * PREV_BIG_H; ++i) {
            write_be_int16(out, header.big_preview[i]);
        }
        out.write(header.delimiter2, 2);

        write_be_int32(out, header.total_layers);
        write_be_int16(out, header.x_resolution);
        write_be_int16(out, header.y_resolution);
        out.write((const char*)&header.x_mirror, 1);
        out.write((const char*)&header.y_mirror, 1);
        write_be_float(out, header.x_size_platform);
        write_be_float(out, header.y_size_platform);
        write_be_float(out, header.z_size_platform);
        write_be_float(out, header.layer_thickness);
        write_be_float(out, header.common_exposure_time);
        out.write((const char*)&header.exposure_delay_mode, 1);
        write_be_float(out, header.turn_off_time);
        write_be_float(out, header.bottom_before_lift_time);
        write_be_float(out, header.bottom_after_lift_time);
        write_be_float(out, header.bottom_after_retract_time);
        write_be_float(out, header.before_lift_time);
        write_be_float(out, header.after_lift_time);
        write_be_float(out, header.after_retract_time);
        write_be_float(out, header.bottom_exposure_time);
        write_be_int32(out, header.bottom_layers);
        write_be_float(out, header.bottom_lift_distance);
        write_be_float(out, header.bottom_lift_speed);
        write_be_float(out, header.lift_distance);
        write_be_float(out, header.lift_speed);
        write_be_float(out, header.bottom_retract_distance);
        write_be_float(out, header.bottom_retract_speed);
        write_be_float(out, header.retract_distance);
        write_be_float(out, header.retract_speed);
        write_be_float(out, header.bottom_second_lift_distance);
        write_be_float(out, header.bottom_second_lift_speed);
        write_be_float(out, header.second_lift_distance);
        write_be_float(out, header.second_lift_speed);
        write_be_float(out, header.bottom_second_retract_distance);
        write_be_float(out, header.bottom_second_retract_speed);
        write_be_float(out, header.second_retract_distance);
        write_be_float(out, header.second_retract_speed);
        write_be_int16(out, header.bottom_light_pwm);
        write_be_int16(out, header.light_pwm);
        out.write((const char*)&header.advance_mode, 1);
        write_be_int32(out, header.printing_time);
        write_be_float(out, header.total_volume);
        write_be_float(out, header.total_weight);
        write_be_float(out, header.total_price);
        out.write(header.price_unit, 8);
        write_be_int32(out, header.layer_content_offset);
        out.write((const char*)&header.gray_scale_level, 1);
        write_be_int16(out, header.transition_layers);

        size_t image_data_start = out.tellp();

        layer_images.reserve(layer_count * 32768);

        for (uint32_t i = 0; i < layer_count; ++i) {
            goo_layer_def layer_def = {};

            layer_def.pause_flag = 0;
            layer_def.pause_position_z = 0.0f;
            layer_def.layer_position_z = static_cast<float>((i + 1) * header.layer_thickness);

            if (i < static_cast<uint32_t>(header.bottom_layers)) {
                layer_def.layer_exposure_time = header.bottom_exposure_time;
                layer_def.layer_off_time = header.turn_off_time;
                layer_def.before_lift_time = header.bottom_before_lift_time;
                layer_def.after_lift_time = header.bottom_after_lift_time;
                layer_def.after_retract_time = header.bottom_after_retract_time;
                layer_def.lift_distance = header.bottom_lift_distance;
                layer_def.lift_speed = header.bottom_lift_speed;
                layer_def.retract_distance = header.bottom_retract_distance;
                layer_def.retract_speed = header.bottom_retract_speed;
            } else {
                layer_def.layer_exposure_time = header.common_exposure_time;
                layer_def.layer_off_time = header.turn_off_time;
                layer_def.before_lift_time = header.before_lift_time;
                layer_def.after_lift_time = header.after_lift_time;
                layer_def.after_retract_time = header.after_retract_time;
                layer_def.lift_distance = header.lift_distance;
                layer_def.lift_speed = header.lift_speed;
                layer_def.retract_distance = header.retract_distance;
                layer_def.retract_speed = header.retract_speed;
            }

            layer_def.second_lift_distance = header.second_lift_distance;
            layer_def.second_lift_speed = header.second_lift_speed;
            layer_def.second_retract_distance = header.second_retract_distance;
            layer_def.second_retract_speed = header.second_retract_speed;
            layer_def.light_pwm = header.light_pwm;
            std::memcpy(layer_def.delimiter, GOO_DELIMITER, 2);
            layer_def.data_size = static_cast<int32_t>(data.files.data[i].size());

            write_be_int16(out, layer_def.pause_flag);
            write_be_float(out, layer_def.pause_position_z);
            write_be_float(out, layer_def.layer_position_z);
            write_be_float(out, layer_def.layer_exposure_time);
            write_be_float(out, layer_def.layer_off_time);
            write_be_float(out, layer_def.before_lift_time);
            write_be_float(out, layer_def.after_lift_time);
            write_be_float(out, layer_def.after_retract_time);
            write_be_float(out, layer_def.lift_distance);
            write_be_float(out, layer_def.lift_speed);
            write_be_float(out, layer_def.second_lift_distance);
            write_be_float(out, layer_def.second_lift_speed);
            write_be_float(out, layer_def.retract_distance);
            write_be_float(out, layer_def.retract_speed);
            write_be_float(out, layer_def.second_retract_distance);
            write_be_float(out, layer_def.second_retract_speed);
            write_be_int16(out, layer_def.light_pwm);
            out.write(layer_def.delimiter, 2);
            write_be_int32(out, layer_def.data_size);

            const char* img_start = reinterpret_cast<const char*>(data.files.data[i].data());
            const char* img_end = img_start + data.files.data[i].size();
            out.write(img_start, data.files.data[i].size());

            out.write(GOO_DELIMITER, 2);
        }

        out.write(GOO_ENDING, 11);
        out.close();
    } catch(std::exception& e) {
        SPDLOG_ERROR("Goo export failed: {}", e.what());
        throw;
    }
}

} // namespace Slic3r::Biz::PrintHost::Sla