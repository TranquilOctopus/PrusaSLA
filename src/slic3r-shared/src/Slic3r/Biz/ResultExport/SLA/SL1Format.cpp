#include "Slic3r/Biz/ResultExport/SLA/SlaArchiveFormat.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SL1.hpp"
#include <libslic3r/SlicingStatus.hpp>
#include "Slic3r/Biz/ResultExport/SLA/AnycubicSLA.hpp"
#include "Slic3r/Biz/ResultExport/SLA/GooSLA.hpp"
#include "Slic3r/Biz/ResultExport/SLA/CtbSLA.hpp"

#include <vector>
#include <memory>
#include <mutex>
#include <string>
#include <stdexcept>
#include <utility>

namespace Slic3r::Biz::PrintHost::Sla {

class SL1Format : public ISlaArchiveFormat
{
public:
    std::string name() const override { return "SL1"; }
    std::string description() const override { return "Prusa SL1/SL1S format"; }
    std::vector<std::string> extensions() const override { return {"sl1", "sl1s", "zip"}; }
    Slic3r::Biz::Slicing::Sla::FileDataType file_data_type() const override { return Slic3r::Biz::Slicing::Sla::FileDataType::sl1_png; }

    void store(const std::string& file_path, const Biz::Slicing::SLAResultData& data) const override
    {
        store_sl1(file_path, data);
    }
};

class SL1SVGFormat : public ISlaArchiveFormat
{
public:
    std::string name() const override { return "SL1_SVG"; }
    std::string description() const override { return "Prusa SL1/SL1S SVG format"; }
    std::vector<std::string> extensions() const override { return {"sl1svg"}; }
    Slic3r::Biz::Slicing::Sla::FileDataType file_data_type() const override { return Slic3r::Biz::Slicing::Sla::FileDataType::sl1_svg; }

    void store(const std::string& file_path, const Biz::Slicing::SLAResultData& data) const override
    {
        store_sl1(file_path, data);
    }
};

class AnycubicFormat : public ISlaArchiveFormat
{
public:
    std::string name() const override { return "Anycubic"; }
    std::string description() const override { return "Anycubic Photon Mono format"; }
    std::vector<std::string> extensions() const override { return {"pwmo", "pwmx", "pwms"}; }
    Slic3r::Biz::Slicing::Sla::FileDataType file_data_type() const override { return Slic3r::Biz::Slicing::Sla::FileDataType::anycubic; }

    void store(const std::string& file_path, const Biz::Slicing::SLAResultData& data) const override
    {
        store_anycubic(file_path, data);
    }
};

// The Photon Workshop container of the Photon Mono M5 family, one registry entry per printer: the
// same writer with a different machine name and file extension, and its own file data type, which
// is what the engine's raster path and the export both key on. Only the .pm5 entry has been
// compared with a file Photon Workshop wrote; the .pm5s and .pm7 files are experimental and
// unverified (doc/sla-fork/formats/pm5.md).
class PhotonWorkshopFormat : public ISlaArchiveFormat
{
public:
    PhotonWorkshopFormat(std::string name, std::string description, std::string extension,
                         Slic3r::Biz::Slicing::Sla::FileDataType type, PmWorkshopFormat variant)
        : m_name(std::move(name))
        , m_description(std::move(description))
        , m_extension(std::move(extension))
        , m_type(type)
        , m_variant(variant)
    {
    }

    std::string name() const override { return m_name; }
    std::string description() const override { return m_description; }
    std::vector<std::string> extensions() const override { return {m_extension}; }
    Slic3r::Biz::Slicing::Sla::FileDataType file_data_type() const override { return m_type; }

    void store(const std::string& file_path, const Biz::Slicing::SLAResultData& data) const override
    {
        store_pm_workshop(file_path, data, m_variant);
    }

private:
    std::string m_name;
    std::string m_description;
    std::string m_extension;
    Slic3r::Biz::Slicing::Sla::FileDataType m_type;
    PmWorkshopFormat m_variant;
};

std::unique_ptr<ISlaArchiveFormat> make_photon_workshop_format(
    std::string name,
    std::string description,
    std::string extension,
    Slic3r::Biz::Slicing::Sla::FileDataType type,
    PmWorkshopFormat variant
)
{
    return std::make_unique<PhotonWorkshopFormat>(
        std::move(name),
        std::move(description),
        std::move(extension),
        type,
        variant
    );
}

class GooFormat : public ISlaArchiveFormat
{
public:
    std::string name() const override { return "Goo"; }
    std::string description() const override { return "Elegoo GOO format"; }
    std::vector<std::string> extensions() const override { return {"goo"}; }
    Slic3r::Biz::Slicing::Sla::FileDataType file_data_type() const override { return Slic3r::Biz::Slicing::Sla::FileDataType::goo; }

    void store(const std::string& file_path, const Biz::Slicing::SLAResultData& data) const override
    {
        store_goo(file_path, data);
    }
};

// Chitubox .ctb, the unencrypted v2/v3 container. No printer profile selects it yet (there is no
// Chitubox-sliced sample to check the layout against), so it is reachable through the registry and
// the format picker, and only a preset that sets sla_archive_format: ctb by hand would slice for it.
class CtbFormat : public ISlaArchiveFormat
{
public:
    std::string name() const override { return "CTB"; }
    std::string description() const override { return "Chitubox CTB format (unencrypted v3)"; }
    std::vector<std::string> extensions() const override { return {"ctb"}; }
    Slic3r::Biz::Slicing::Sla::FileDataType file_data_type() const override { return Slic3r::Biz::Slicing::Sla::FileDataType::ctb; }

    void store(const std::string& file_path, const Biz::Slicing::SLAResultData& data) const override
    {
        store_ctb(file_path, data);
    }
};

void register_sla_archive_formats()
{
    static std::once_flag once;
    std::call_once(once, [] {
        auto& registry = SlaArchiveFormatRegistry::instance();
        registry.register_format("SL1", []() { return std::make_unique<SL1Format>(); });
        registry.register_format("SL1_SVG", []() { return std::make_unique<SL1SVGFormat>(); });
        registry.register_format("Anycubic", []() { return std::make_unique<AnycubicFormat>(); });
        registry.register_format("PM5", []() { return make_photon_workshop_format(
            "PM5",
            "Anycubic Photon Mono M5 PM5 format",
            "pm5",
            Slic3r::Biz::Slicing::Sla::FileDataType::pm5,
            PmWorkshopFormat::pm5
        ); });
        registry.register_format("PM5S", []() { return make_photon_workshop_format(
            "PM5S",
            "Anycubic Photon Mono M5s PM5S format (experimental, unverified)",
            "pm5s",
            Slic3r::Biz::Slicing::Sla::FileDataType::pm5s,
            PmWorkshopFormat::pm5s
        ); });
        registry.register_format("PM7", []() { return make_photon_workshop_format(
            "PM7",
            "Anycubic Photon Mono M7 Pro PM7 format (experimental, unverified)",
            "pm7",
            Slic3r::Biz::Slicing::Sla::FileDataType::pm7,
            PmWorkshopFormat::pm7
        ); });
        registry.register_format("Goo", []() { return std::make_unique<GooFormat>(); });
        registry.register_format("CTB", []() { return std::make_unique<CtbFormat>(); });
    });
}

} // namespace Slic3r::Biz::PrintHost::Sla
