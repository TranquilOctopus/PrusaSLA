#include "Slic3r/Biz/ResultExport/SLA/CtbSLA.hpp"

#include "Slic3r/Biz/ResultExport/SLA/SlaAntiAliasing.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/Image.hpp"
#include "Slic3r/Domain/SlaLayerHeight.hpp"
#include "Slic3r/Domain/SLA/PrintTime.hpp"
#include "Slic3r/Version.hpp"
#include "libslic3r/SLAResult.hpp"

#include <LocalesUtils.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

#include "Slic3r/Log.hpp"

using namespace Slic3r::Biz::Slicing;

namespace Slic3r::Biz::PrintHost::Sla {

namespace {

// doc/sla-fork/formats/ctb.md is the field-by-field description of everything written here, and it
// marks what is unverified: no Chitubox-sliced sample has been compared against this writer yet.
constexpr std::uint32_t CTB_VERSION = 3;

constexpr std::size_t CTB_SOFTWARE_VERSION_LEN = 25;
constexpr std::size_t CTB_SOFTWARE_LEN = 25;
constexpr std::size_t CTB_TIMESTAMP_LEN = 20;
constexpr std::size_t CTB_PRINTER_NAME_LEN = 32;
constexpr std::size_t CTB_PRICE_UNIT_LEN = 8;

constexpr std::size_t CTB_PREVIEW_COLOR_W = 800;
constexpr std::size_t CTB_PREVIEW_COLOR_H = 600;
constexpr std::size_t CTB_PREVIEW_COLOR_BPP = 3;
constexpr std::size_t CTB_PREVIEW_GRAY_W = 400;
constexpr std::size_t CTB_PREVIEW_GRAY_H = 300;
constexpr std::size_t CTB_PREVIEW_GRAY_BPP = 1;
constexpr std::size_t CTB_PREVIEW_COUNT = 2;

void write_u32(std::ofstream& out, std::uint32_t value)
{
    const char bytes[4] = {
        char(value & 0xFF), char((value >> 8) & 0xFF), char((value >> 16) & 0xFF), char((value >> 24) & 0xFF)};
    out.write(bytes, 4);
}

void write_f32(std::ofstream& out, float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    write_u32(out, bits);
}

void write_string(std::ofstream& out, const std::string& text, std::size_t len)
{
    std::string padded = text.substr(0, len);
    padded.resize(len, '\0');
    out.write(padded.data(), static_cast<std::streamsize>(len));
}

// The layer definitions and the header are written in 0.001 of their unit, which is the precision
// the integer fields of this container give: 0.001 mm (um) and 0.001 s (ms).
std::uint32_t scaled(float value)
{
    const double scaled_value = double(value) * 1000.0;
    return std::uint32_t(std::lround(std::max(0.0, scaled_value)));
}

// ConfigView::get asserts when a key is missing, so look these up through values().
std::string get_cfg_value_s(const Domain::ConfigView& cfg, const std::string& key, const std::string& def = {})
{
    const auto it = cfg.values().find(key);
    if (it == cfg.values().end() || !it->second.holds_alternative<std::string>())
        return def;
    return it->second.get<std::string>();
}

bool get_cfg_value_b(const Domain::ConfigView& cfg, const std::string& key, bool def = false)
{
    const auto it = cfg.values().find(key);
    if (it == cfg.values().end() || !it->second.holds_alternative<bool>())
        return def;
    return it->second.get<bool>();
}

float get_cfg_value_f(const Domain::ConfigView& cfg, const std::string& key, const float& def = 0.f)
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

int get_cfg_value_i(const Domain::ConfigView& cfg, const std::string& key, const int& def = 0)
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

// A separation setting is missing from a config view that does not carry the resin, and the SLA
// definition of every distance and speed below is 0, which would write a printer that does not
// separate the layers at all, so a non-positive value falls back to the value this format is
// written with when nothing is set. A delay of 0 is a real value and is kept.
float get_cfg_value_f_pos(const Domain::ConfigView& cfg, const std::string& key, float def)
{
    const float value = get_cfg_value_f(cfg, key, def);
    return value > 0.f ? value : def;
}

// The light PWM fields are 0-255, the range of the light_pwm settings, so the value is taken as it
// is and only clamped to what the field can carry.
std::uint32_t get_cfg_pwm(const Domain::ConfigView& cfg, const std::string& key, std::uint32_t def = 255)
{
    const int value = get_cfg_value_i(cfg, key, int(def));
    return std::uint32_t(value < 0 ? 0 : (value > 255 ? 255 : value));
}

// A preview is a 6-word sub-header followed by width * height * bytes-per-pixel raw bytes. The
// printer's own screen shows the first preview, the second is the thumbnail a file browser shows.
// The image is resampled with nearest neighbour, so a preview never depends on the thumbnail size
// the app happened to render; without a thumbnail the block is left black.
void write_preview(
    std::ofstream& out, std::size_t width, std::size_t height, std::size_t bytes_per_pixel, bool gray,
    const Domain::Images& thumbnails
)
{
    write_u32(out, 0); // reserved
    write_u32(out, std::uint32_t(width));
    write_u32(out, std::uint32_t(height));
    write_u32(out, std::uint32_t(bytes_per_pixel));
    write_u32(out, 0); // x offset in the display
    write_u32(out, 0); // y offset in the display
    write_u32(out, gray ? 1u : 0u);

    std::vector<std::uint8_t> pixels(width * height * bytes_per_pixel, 0);

    if (!thumbnails.empty()) {
        const Domain::Image& thumbnail = thumbnails.front();
        std::size_t source_bpp = 0;
        if (thumbnail.format() == Domain::PixelFormat::RGBA8)
            source_bpp = 4;
        else if (thumbnail.format() == Domain::PixelFormat::RGB8)
            source_bpp = 3;

        const std::size_t source_w = std::max(0, thumbnail.width());
        const std::size_t source_h = std::max(0, thumbnail.height());
        if (source_bpp > 0 && source_w > 0 && source_h > 0
            && thumbnail.pixels.size() >= source_w * source_h * source_bpp) {
            for (std::size_t y = 0; y < height; ++y) {
                const std::size_t source_row = y * source_h / height;
                for (std::size_t x = 0; x < width; ++x) {
                    const std::size_t source_col = x * source_w / width;
                    const std::uint8_t* source =
                        thumbnail.pixels.data() + (source_row * source_w + source_col) * source_bpp;
                    std::uint8_t* target = pixels.data() + (y * width + x) * bytes_per_pixel;
                    if (gray) {
                        // The luminance of the pixel, the same weighting the .goo preview uses.
                        target[0] = std::uint8_t((source[0] * 77 + source[1] * 151 + source[2] * 28) >> 8);
                    } else {
                        target[0] = source[0];
                        target[1] = source[1];
                        target[2] = source[2];
                    }
                }
            }
        }
    }

    out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
}

} // namespace

void store_ctb(const std::string& file_path, const Biz::Slicing::SLAResultData& data)
{
    const Domain::ConfigView& cfg = data.config;
    const std::uint32_t layer_count = static_cast<std::uint32_t>(data.files.data.size());

    // The print statistics are optional in the result data, and a caller that has none still gets
    // a file, with the material fields at 0.
    const double used_material = data.print_statistics
        ? data.print_statistics->objects_used_material + data.print_statistics->support_used_material
        : 0.;

    const float layer_height = float(Domain::sla_effective_layer_height(cfg));
    // The engine slices the first layer thicker when the resin states its own initial layer height.
    const float initial_layer_height = get_cfg_value_f(cfg, "initial_layer_height");
    const float first_layer_height = initial_layer_height > 0.f ? initial_layer_height : layer_height;
    const float normal_exposure = get_cfg_value_f(cfg, "exposure_time");
    const float bottom_exposure = get_cfg_value_f(cfg, "initial_exposure_time");

    std::uint32_t bottom_layers = std::uint32_t(std::max(0, Domain::sla_bottom_layer_count(cfg)));
    bottom_layers = std::min(bottom_layers, layer_count);

    // The raft interface is the band of layers at the top of the raft that carries an exposure of
    // its own. Every layer definition of this container holds an exposure of its own, so it can
    // carry one, and no interface leaves every layer the exposure it had before.
    const Domain::RaftInterface raft_interface = Domain::sla_raft_interface(cfg, int(layer_count));
    const std::uint32_t interface_layers = std::uint32_t(raft_interface.count());

    // The waits are seconds, which is the unit of the wait_* settings, and the light-off time is
    // the only delay this container keeps on its own.
    const float light_off_time = 0.5f;
    const float before_lift_time = get_cfg_value_f(cfg, "wait_before_lift");
    const float after_lift_time = get_cfg_value_f(cfg, "wait_after_lift");
    const float after_retract_time = get_cfg_value_f(cfg, "wait_after_retract");
    const float bottom_before_lift_time = get_cfg_value_f(cfg, "bottom_wait_before_lift");
    const float bottom_after_lift_time = get_cfg_value_f(cfg, "bottom_wait_after_lift");
    const float bottom_after_retract_time = get_cfg_value_f(cfg, "bottom_wait_after_retract");

    // Distances are mm and speeds mm/s, the units the settings are defined in. There is no setting
    // for a retract distance, so the plate returns over the distance it was lifted.
    const float lift_distance = get_cfg_value_f_pos(cfg, "lift_height", 6.0f);
    const float lift_speed = get_cfg_value_f_pos(cfg, "lift_speed", 2.0f);
    const float retract_distance = lift_distance;
    const float retract_speed = get_cfg_value_f_pos(cfg, "retract_speed", 3.0f);
    const float bottom_lift_distance = get_cfg_value_f_pos(cfg, "bottom_lift_height", 6.0f);
    const float bottom_lift_speed = get_cfg_value_f_pos(cfg, "bottom_lift_speed", 2.0f);
    const float bottom_retract_distance = bottom_lift_distance;
    const float bottom_retract_speed = get_cfg_value_f_pos(cfg, "bottom_retract_speed", 3.0f);

    const std::uint32_t transition_layers = std::uint32_t(std::max(0, Domain::sla_effective_faded_layers(cfg)));
    const std::uint32_t light_pwm = get_cfg_pwm(cfg, "light_pwm");
    const std::uint32_t bottom_light_pwm = get_cfg_pwm(cfg, "bottom_light_pwm");

    // The print time the header carries is the shared MSLA estimate (M1.11c): the exposure, the
    // light-off waits and the lift/retract separation of every layer, summed, so the bottom_* waits
    // and distances, the raft interface exposure inside the band and the second stage of the
    // separation all count. It is rounded to the nearest second, as it was when this writer summed
    // the same terms in whole milliseconds itself. See doc/sla-fork/profiling/print-time.md.
    const std::uint32_t print_time_s = std::uint32_t(
        Domain::sla_estimate_print_time(cfg, int(layer_count)).total_s + 0.5);

    const float bottle_weight_g = get_cfg_value_f(cfg, "bottle_weight") * 1000.0f;
    const float bottle_volume_ml = get_cfg_value_f(cfg, "bottle_volume");
    const float bottle_cost = get_cfg_value_f(cfg, "bottle_cost");
    const float material_density = bottle_volume_ml > 0.f ? bottle_weight_g / bottle_volume_ml : 1.0f;
    const float total_volume = float(used_material / 1000.0);
    const float total_weight = total_volume * material_density;
    const float total_price = bottle_volume_ml > 0.f ? total_volume * bottle_cost / bottle_volume_ml : 0.f;

    try {
        std::ofstream out;
        out.open(file_path, std::ios::binary | std::ios::out | std::ios::trunc);

        write_u32(out, CTB_VERSION);
        write_u32(out, 0); // reserved
        write_u32(out, 0); // reserved
        write_string(out, SLIC3R_VERSION, CTB_SOFTWARE_VERSION_LEN);
        write_string(out, "", 7); // padding after the software version
        write_string(out, "PrusaSlicer", CTB_SOFTWARE_LEN);
        write_string(out, "", 7); // padding after the software name

        CNumericLocalesSetter locales_setter;
        char timestamp[64] = {};
        const std::time_t now = std::time(nullptr);
        const std::tm* local = std::localtime(&now);
        if (local != nullptr)
            std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", local);
        write_string(out, timestamp, CTB_TIMESTAMP_LEN);
        write_string(out, "", 4); // padding after the time stamp

        write_string(out, get_cfg_value_s(cfg, "printer_name"), CTB_PRINTER_NAME_LEN);
        write_u32(out, 0); // padding after the printer name

        write_u32(out, std::uint32_t(get_cfg_value_i(cfg, "display_pixels_x")));
        write_u32(out, std::uint32_t(get_cfg_value_i(cfg, "display_pixels_y")));
        write_f32(out, 0.f); // x offset of the display in the printer, mm
        write_f32(out, 0.f); // y offset of the display in the printer, mm
        write_u32(out, get_cfg_value_b(cfg, "display_mirror_x") ? 1u : 0u);
        write_u32(out, get_cfg_value_b(cfg, "display_mirror_y") ? 1u : 0u);
        write_u32(out, std::uint32_t(CTB_PREVIEW_COUNT));

        write_preview(out, CTB_PREVIEW_COLOR_W, CTB_PREVIEW_COLOR_H, CTB_PREVIEW_COLOR_BPP, false, data.thumbnails);
        write_preview(out, CTB_PREVIEW_GRAY_W, CTB_PREVIEW_GRAY_H, CTB_PREVIEW_GRAY_BPP, true, data.thumbnails);

        write_f32(out, layer_height);
        write_f32(out, normal_exposure);
        write_f32(out, bottom_exposure);
        write_u32(out, bottom_layers);
        // The transition (burn-in) count is the one header field that has to know about the
        // interface, because the printer is told to fade the exposure from the first layer to the
        // normal one: the interface is another block of layers it has to fade over. Its exact
        // meaning is still unverified (see ctb.md), so this is the reading that fits both, and the
        // band is counted the way Domain::sla_raft_interface() counts it, so the two cannot
        // disagree. Without an interface the field is the transition layer count as before.
        write_u32(out, std::min(layer_count, transition_layers + interface_layers));
        write_f32(out, light_off_time);
        write_f32(out, before_lift_time);
        write_f32(out, after_lift_time);
        write_f32(out, after_retract_time);
        write_f32(out, lift_distance);
        write_f32(out, lift_speed);
        write_f32(out, retract_distance);
        write_f32(out, retract_speed);
        write_f32(out, bottom_before_lift_time);
        write_f32(out, bottom_after_lift_time);
        write_f32(out, bottom_after_retract_time);
        write_f32(out, bottom_lift_distance);
        write_f32(out, bottom_lift_speed);
        write_f32(out, bottom_retract_distance);
        write_f32(out, bottom_retract_speed);
        write_u32(out, bottom_light_pwm);
        write_u32(out, light_pwm);
        write_u32(out, 0); // advance mode
        write_u32(out, print_time_s);
        write_f32(out, total_volume);
        write_f32(out, total_weight);
        write_f32(out, total_price);
        write_string(out, "USD", CTB_PRICE_UNIT_LEN);
        // The word says whether the file is anti-aliased, and a gamma_correction of 0 thresholded
        // the raster to a binary image, so it follows that setting (M4.13b). Unverified like the
        // rest of this container: no Chitubox-sliced sample has been compared against the writer.
        write_u32(out, sla_raster_anti_aliased(cfg) ? SLA_AA_LEVEL_ANTI_ALIASED
                                                    : SLA_AA_LEVEL_BINARY);
        write_u32(out, 0); // reserved

        // Every layer definition comes before the layer images, in print order. A definition is 12
        // words, see ctb.md.
        for (std::uint32_t i = 0; i < layer_count; ++i) {
            const bool bottom = i < bottom_layers;
            const float height_um = i == 0 ? first_layer_height : layer_height;
            // The height reached after this layer, which is what the printer lifts to. The first
            // layer is the thicker one when the resin sets its own initial layer height, so the sum
            // starts from that and adds the layer height for every layer after it.
            const float total_height_um = first_layer_height + float(i) * layer_height;

            // The interface layers are exposed like the rest of the print unless the interface
            // brings an exposure of its own, and are separated like them.
            const float layer_exposure = bottom
                ? bottom_exposure
                : float(raft_interface.layer_exposure_s(i, normal_exposure));

            write_u32(out, scaled(height_um));
            write_u32(out, scaled(layer_exposure));
            write_u32(out, scaled(bottom ? bottom_before_lift_time : before_lift_time));
            write_u32(out, scaled(bottom ? bottom_after_lift_time : after_lift_time));
            write_u32(out, scaled(bottom ? bottom_after_retract_time : after_retract_time));
            write_u32(out, scaled(bottom ? bottom_lift_distance : lift_distance));
            write_u32(out, scaled(bottom ? bottom_retract_distance : retract_distance));
            write_u32(out, 0); // layer volume, mm3: not computed per layer
            write_u32(out, scaled(total_height_um));
            write_u32(out, std::uint32_t(data.files.data[i].size()));
            // The key of the layer image. 0 means the image is stored as it is, which is what the
            // unencrypted v2/v3 container is; the encrypted v4/v5 files are not written here.
            write_u32(out, 0);
            write_u32(out, 0); // reserved
        }

        for (std::uint32_t i = 0; i < layer_count; ++i) {
            const char* image = reinterpret_cast<const char*>(data.files.data[i].data());
            out.write(image, static_cast<std::streamsize>(data.files.data[i].size()));
        }

        write_u32(out, 0); // end of file

        out.close();
    } catch (std::exception& e) {
        SPDLOG_ERROR("Ctb export failed: {}", e.what());
        throw;
    }
}

} // namespace Slic3r::Biz::PrintHost::Sla
