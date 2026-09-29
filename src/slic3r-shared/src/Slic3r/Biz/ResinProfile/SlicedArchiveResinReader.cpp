#include "Slic3r/Biz/ResinProfile/SlicedArchiveResinReader.hpp"

#include "Slic3r/Biz/ArchiveIni.hpp"
#include "Slic3r/Biz/ResinProfile/ForeignResinProfile.hpp"

#include <fmt/format.h>

#include <initializer_list>
#include <optional>
#include <string>
#include <utility>

#include <boost/algorithm/string.hpp>

#include <miniz.h>

namespace Slic3r::Biz::ResinProfile {

namespace {

using Slic3r::Biz::ArchiveIniMap;
using Slic3r::Biz::parse_ini_value;
using Slic3r::Biz::parse_archive_ini;
using Slic3r::Biz::read_zip_entry;
using Slic3r::Biz::ZipReader;

const char *const CONFIG_INI  = "config.ini";
const char *const PROFILE_INI = "prusaslicer.ini";

// Every local file header of a zip starts with these four bytes, and the name of the entry
// follows it verbatim, so the metadata of the first entries is visible in a file head.
const std::string ZIP_LOCAL_HEADER{"PK\x03\x04", 4};

// One setting, spelled as the archive may spell it. The keys are looked up in the order
// they are listed: the profile the layers were rendered for (prusaslicer.ini) comes first,
// the printer config of the job (config.ini) second, and a key that is in neither file just
// leaves the setting empty.
const char *const EXPOSURE_KEYS[]             = {"exposure_time", "expTime"};
const char *const INITIAL_EXPOSURE_KEYS[]     = {"initial_exposure_time", "expTimeFirst"};
const char *const LAYER_HEIGHT_KEYS[]         = {"layer_height", "layerHeight"};
const char *const INITIAL_LAYER_HEIGHT_KEYS[] = {"initial_layer_height", "initialLayerHeight"};
// An SL1 job has no bottom layers of its own: the tank is exposed layer by layer from the
// bottom up, and the count only turns up in a profile that names it.
const char *const BOTTOM_LAYER_KEYS[]     = {"bottom_layer_count"};
const char *const FADED_LAYER_KEYS[]      = {"faded_layers", "numFade"};
const char *const SLOW_LAYER_KEYS[]       = {"numSlow"};
const char *const FAST_LAYER_KEYS[]       = {"numFast"};
const char *const MATERIAL_NAME_KEYS[]    = {"sla_material_settings_id", "materialName"};
const char *const MATERIAL_VENDOR_KEYS[]  = {"material_vendor", "materialVendor"};
const char *const PRINTER_HINT_KEYS[]     = {"printer_model", "printerModel", "printerVariant"};

/// Read a value of type T from the first of the keys the file uses. A key that is there but
/// does not hold a readable value is a warning, not a value.
template<typename T>
std::optional<T> read_value(ForeignResinProfile &profile, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const auto it = profile.raw_values.find(key);
        if (it == profile.raw_values.end())
            continue;

        if (const auto value = parse_ini_value<T>(it->second))
            return value;

        profile.warnings.push_back(fmt::format("Ignoring '{}': '{}' is not a number.", key, it->second));
        return std::nullopt;
    }

    return std::nullopt;
}

/// Read a whole text value, spaces and all, from the first of the keys the file uses.
std::optional<std::string> read_text(ForeignResinProfile &profile, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const auto it = profile.raw_values.find(key);
        if (it == profile.raw_values.end())
            continue;

        const std::string value = boost::algorithm::trim_copy(it->second);
        if (!value.empty())
            return value;

        profile.warnings.push_back(fmt::format("Ignoring '{}': it holds no value.", key));
        return std::nullopt;
    }

    return std::nullopt;
}

} // anonymous namespace

bool SlicedArchiveResinReader::sniff(const std::string &head) const
{
    if (head.size() < ZIP_LOCAL_HEADER.size() || head.compare(0, ZIP_LOCAL_HEADER.size(), ZIP_LOCAL_HEADER) != 0)
        return false;

    // The entry names of an SL1 archive follow the zip magic right away, config.ini first.
    // A zip that is not one (a .3mf, a .cfgx) is left for a reader of its own format.
    return boost::algorithm::ifind(head, std::string(CONFIG_INI)) != head.end();
}

tl::expected<ForeignResinProfile, std::string> SlicedArchiveResinReader::read(const boost::filesystem::path &path) const
{
    ZipReader zip{path.string()};
    if (!zip.ok())
        return tl::make_unexpected(
            fmt::format("{} cannot be opened as a sliced archive (.sl1 or .sl1s).", path.string()));

    ArchiveIniMap config_ini, profile_ini;
    bool          has_config = false, has_profile = false;

    const mz_uint num_entries = mz_zip_reader_get_num_files(&zip.archive());
    for (mz_uint i = 0; i < num_entries; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip.archive(), i, &stat))
            continue;

        std::string lname{stat.m_filename};
        boost::algorithm::to_lower(lname);
        if (lname != CONFIG_INI && lname != PROFILE_INI)
            continue;

        auto text = read_zip_entry(zip.archive(), stat);
        if (!text)
            return tl::make_unexpected(text.error());

        ArchiveIniMap &ini  = lname == CONFIG_INI ? config_ini : profile_ini;
        bool          &seen = lname == CONFIG_INI ? has_config : has_profile;
        for (const auto &[key, value] : parse_archive_ini(*text))
            ini.insert_or_assign(key, value);
        seen = true;
    }

    // Either file names the settings of the job, so one of them is enough. Neither means
    // this is a zip of something else, and there is nothing to import from it.
    if (!has_config && !has_profile)
        return tl::make_unexpected(
            fmt::format("{} holds no {}, so it has no material settings to import.", path.string(), CONFIG_INI));

    ForeignResinProfile profile;
    profile.source_format = format_id();
    profile.source_path   = path.string();

    // Both files keep their own spelling of every key, so nothing is lost by taking
    // config.ini first and the embedded profile over it. The key lists above resolve the
    // settings that the two name differently.
    profile.raw_values = std::move(config_ini);
    for (const auto &[key, value] : profile_ini)
        profile.raw_values.insert_or_assign(key, value);

    profile.printer_hint = read_text(profile, PRINTER_HINT_KEYS);

    ResinMaterialSettings &material = profile.material;
    material.exposure_time_s         = read_value<double>(profile, EXPOSURE_KEYS);
    material.initial_exposure_time_s = read_value<double>(profile, INITIAL_EXPOSURE_KEYS);
    material.layer_height_mm         = read_value<double>(profile, LAYER_HEIGHT_KEYS);
    material.initial_layer_height_mm = read_value<double>(profile, INITIAL_LAYER_HEIGHT_KEYS);
    material.bottom_layer_count      = read_value<int>(profile, BOTTOM_LAYER_KEYS);
    material.faded_layer_count       = read_value<int>(profile, FADED_LAYER_KEYS);
    material.slow_layer_count        = read_value<int>(profile, SLOW_LAYER_KEYS);
    material.fast_layer_count        = read_value<int>(profile, FAST_LAYER_KEYS);
    material.material_name           = read_text(profile, MATERIAL_NAME_KEYS);
    material.material_vendor         = read_text(profile, MATERIAL_VENDOR_KEYS);

    if (!material.exposure_time_s && !material.initial_exposure_time_s && !material.layer_height_mm)
        profile.warnings.push_back(
            "The archive names no exposure time and no layer height, so the material settings are incomplete.");

    return profile;
}

} // namespace Slic3r::Biz::ResinProfile
