#include "Slic3r/Biz/ResultExport/SLA/SL1.hpp"
#include "Slic3r/Biz/ResultExport/SLA/Zipper.hpp"

#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/Image.hpp"
#include "Slic3r/Domain/SlaLayerHeight.hpp"
#include "Slic3r/Biz/Algorithms/ImageUtils.hpp"
#include "Slic3r/Time.hpp"
#include "Slic3r/Utils.hpp"
#include "Slic3r/Version.hpp"
#include "Slic3r/Biz/Algorithms/MiniZWrapper.hpp" // IWYU pragma: keep
#include "libslic3r/SLAResult.hpp"

#include <LocalesUtils.hpp>
#include <sstream>
#include <Slic3r/Log.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/algorithm/string.hpp>
#include <regex>
#include <iomanip>
#include <nlohmann/json.hpp>

using namespace Slic3r::Biz::Slicing;
using json = nlohmann::json;
using ConfMap = std::map<std::string, std::string>;
using Slic3r::Domain::EnumVectorWrapper;

namespace Slic3r::Biz::PrintHost::Sla {

namespace {

std::string to_ini(const ConfMap &m)
{
    std::string ret;
    for (auto &param : m)
        ret += param.first + " = " + param.second + "\n";

    return ret;
}

// The bottom layer count is the one setting of a job that neither file of an SL1 archive has
// a key for: the firmware's config.ini has one exposure for the first layer and one for the
// rest, and the legacy serialization behind prusaslicer.ini has no bottom_layer_count key at
// all. So the writer states it under the name of the setting itself in the embedded profile,
// which is the spelling the resin reader (SlicedArchiveResinReader) looks for first, and
// numBottom in config.ini, next to the other counts, for the reader of a file with no
// embedded profile. A printer reads neither: the SL1 format has no bottom layer count
// (doc/sla-fork/formats/sl1.md).
const std::string bottom_layer_opt{"bottom_layer_count"};
const std::string bottom_layer_printer_opt{"numBottom"};

/// Whether an ini text already states a key. The key has to start a line, so a comment or a
/// value that happens to contain the name does not count as one.
bool ini_has_key(const std::string &ini, const std::string &key)
{
    // One leading newline, so that the first line of the text is a line like any other.
    const std::string text = "\n" + ini;
    const std::string needle = "\n" + key;

    for (size_t pos = text.find(needle); pos != std::string::npos;
         pos = text.find(needle, pos + 1)) {
        const size_t after = pos + needle.size();
        if (after < text.size()) {
            const char sep = text[after];
            if (sep == ' ' || sep == '\t' || sep == '=' || sep == '\r')
                return true;
        }
    }

    return false;
}

/// An ini text with one more `key = value` line, unless it states the key already.
std::string with_ini_key(const std::string &ini, const std::string &key, const std::string &value)
{
    if (ini_has_key(ini, key))
        return ini;

    std::string ret = ini;
    if (!ret.empty() && ret.back() != '\n')
        ret += '\n';
    ret += key + " = " + value + "\n";
    return ret;
}

const std::vector<std::string> ms_opts{
    "delay_before_exposure",
    "delay_after_exposure",
    "tilt_down_offset_delay",
    "tilt_up_offset_delay",
    "tilt_down_delay",
    "tilt_up_delay",
};

const std::string tower_hop_height_opt{
   "tower_hop_height"
};

const std::vector<std::string> speed_opts{
    "tower_speed",
    "tilt_down_initial_speed",
    "tilt_down_finish_speed",
    "tilt_up_initial_speed",
    "tilt_up_finish_speed",
};

const std::string use_tilt_opt{"use_tilt"};

const std::vector<std::string> count_opts{
    "tilt_down_offset_steps",
    "tilt_down_cycles",
    "tilt_up_offset_steps",
    "tilt_up_cycles"
};

std::string tilt_options_to_json(const Domain::ConfigView& cfg, const ConfMap& iniconf)
{
    json below_node;
    json above_node;

    const double ms_coeff{1e3};
    for (const std::string& key : ms_opts) {
        const auto values = cfg.get<std::vector<double>>(key);
        const std::string insert_key{key + "_ms"};     
        below_node[insert_key] = static_cast<int>(ms_coeff * values.at(0));
        above_node[insert_key] = static_cast<int>(ms_coeff * values.at(1));
    }

    {
        const auto values = cfg.get<std::vector<double>>(tower_hop_height_opt);
        const double nm_coeff{1e6};
        const std::string insert_key{tower_hop_height_opt + "_nm"};
        below_node[insert_key] = static_cast<int>(nm_coeff * values.at(0));
        above_node[insert_key] = static_cast<int>(nm_coeff * values.at(1));
    }

    for (const std::string& key : speed_opts) {
        const auto values = cfg.get<Domain::EnumVectorWrapper>(key).get_strings();
        const std::string insert_key{boost::replace_all_copy(key, "_speed", "_profile")};
        below_node[insert_key] = values.at(0);
        above_node[insert_key] = values.at(1);
    }

    {
        const auto values = cfg.get<std::vector<bool>>(use_tilt_opt);
        below_node[use_tilt_opt] = values.at(0);
        above_node[use_tilt_opt] = values.at(1);
    }

    for (const std::string& key : count_opts) {
        const auto values = cfg.get<std::vector<int>>(key);
        below_node[key] = values.at(0);
        above_node[key] = values.at(1);
    }

    json profile_node;
    profile_node["area_fill"] = cfg.get<double>("area_fill");
    profile_node["below_area_fill"] = below_node;
    profile_node["above_area_fill"] = above_node;

    json root;

    for (const auto& param : iniconf) {
        root[param.first] = param.second;
    }

    root["version"] = "1";
    root["exposure_profile"] = profile_node;

    return root.dump(-1);
}


static std::string serialize(const double value)
{
    std::ostringstream ss;
    if (std::isfinite(value))
        ss << value;
    else if (std::isnan(value)) {
        throw std::runtime_error("Serializing NaN");
    } else
        throw std::runtime_error("Serializing invalid number");
    return ss.str();
}

void fill_iniconf(ConfMap &m, const Domain::ConfigView &cfg, const Domain::SLA::PrintStatistics &stats) {
    using Domain::SLAMaterialSpeed;
    using Domain::SLAMaterialSpeed::slamsSlow;
    using Domain::SLAMaterialSpeed::slamsFast;

    CNumericLocalesSetter locales_setter; // for to_string
    m["layerHeight"]    = serialize(Domain::sla_effective_layer_height(cfg));
    // The .sl1 format has one exposure for the whole print and one for its first layer, so the
    // raft interface exposure (raft_interface_exposure) has nowhere to go here and is left unused.
    m["expTime"]        = serialize(cfg.get<double>("exposure_time"));
    m["expTimeFirst"]   = serialize(cfg.get<double>("initial_exposure_time"));
    const Domain::SLAMaterialSpeed mps = cfg.get<Domain::SLAMaterialSpeed>("material_print_speed");
    m["expUserProfile"] = mps == slamsSlow ? "1" : mps == slamsFast ? "0" : "2";

    // TODO commented out, until we know how to reference the settings
    //m["materialName"]   = cfg.get<std::string>("sla_material_settings_id");
    m["printerModel"]   = cfg.get<std::string>("printer_model");
    m["printerVariant"] = cfg.get<std::string>("printer_variant");
    //m["printerProfile"] = cfg.get<std::string>("printer_settings_id");
    //m["printProfile"]   = cfg.get<std::string>("sla_print_settings_id");
    m["fileCreationTimestamp"] = Utils::utc_timestamp();
    m["prusaSlicerVersion"]    = SLIC3R_BUILD_ID;

    // Set statistics values to the printer
    double used_material = (stats.objects_used_material +
                            stats.support_used_material) / 1000;
    m["usedMaterial"] = std::to_string(used_material);
    m["numFade"]      = std::to_string(stats.count_faded_layers);
    // numFade is the transition (fade) count, which is a different thing: the exposure is
    // faded in over it, and it is not the count the bottom_* settings cover. The count the
    // print used is this fork's numBottom, which the firmware has no key for.
    m[bottom_layer_printer_opt] = std::to_string(Domain::sla_bottom_layer_count(cfg));
    m["numSlow"]                = std::to_string(stats.slow_layers_count);
    m["numFast"]                = std::to_string(stats.fast_layers_count);
    m["printTime"]              = std::to_string(stats.estimated_print_time);
    m["hollow"] = stats.hollowing_enable ? "1" : "0";
    m["action"] = "print";
}

static void write_thumbnail(Zipper &zipper, const Domain::Image &data)
{
    size_t png_size = 0;

    void  *png_data = tdefl_write_image_to_png_file_in_memory_ex(
         (const void *) data.pixels.data(), data.width(), data.height(), 4,
         &png_size, MZ_DEFAULT_LEVEL, 1);

    if (png_data != nullptr) {
        zipper.add_entry("thumbnail/thumbnail" + std::to_string(data.width()) +
                             "x" + std::to_string(data.height()) + ".png",
                         static_cast<const std::uint8_t *>(png_data),
                         png_size);

        mz_free(png_data);
    }
}
}

void store_sl1(const std::string& file_path, const Slicing::SLAResultData& data)
{
    std::string layer_extension = ".png";
    Zipper::e_compression compression = Zipper::FAST_COMPRESSION;
    if (data.files.type == Slicing::Sla::FileDataType::sl1_svg){
        layer_extension = ".svg";
        compression = Zipper::TIGHT_COMPRESSION;
    }

    Zipper zipper{file_path, compression};
    std::string project = data.project_name.empty() ?
        boost::filesystem::path(zipper.get_filename()).stem().string() :
        data.project_name;

    const auto& stats = *data.print_statistics;

    const Biz::Slicing::SerializedConfig& serialized_config{data.serialized_config};
    const Domain::ConfigView& print_config{data.config};

    ConfMap iniconf;
    fill_iniconf(iniconf, print_config, stats);

    iniconf["jobDir"] = project;

    // The embedded profile keeps every key the legacy serialization has and gains the one it
    // has no key for, under the name of the setting, so the count the print used is in the
    // archive under both spellings. It is the value fill_iniconf() just wrote rather than a
    // second reading of the config, so the two files cannot state different counts.
    const std::string profile_ini =
        with_ini_key(serialized_config.ini, bottom_layer_opt, iniconf.at(bottom_layer_printer_opt));

    try {
        zipper.add_entry("config.ini");
        zipper << to_ini(iniconf);
        zipper.add_entry("config.json");
        zipper << tilt_options_to_json(print_config, iniconf);

        zipper.add_entry("prusaslicer.ini");
        zipper << profile_ini;
        zipper.add_entry("prusaslicer.json");
        zipper << serialized_config.json;


        size_t i = 0;
        for (const Slicing::Sla::FileData& rst : data.files.data) {
            std::string imgname = project + string_printf("%.5d", i++) + layer_extension;
            zipper.add_entry(imgname.c_str(), rst.data(), rst.size());
        }

        for (const Domain::Image& data : data.thumbnails)
            if (Biz::Algorithms::ImageUtils::is_valid(data))
                write_thumbnail(zipper, data);

        zipper.finalize();
    } catch(std::exception& e) {
        SPDLOG_ERROR("{}", e.what());
        // Rethrow the exception
        throw;
    }
}

} // namespace Slic3r::Biz::PrintHost::Sla
