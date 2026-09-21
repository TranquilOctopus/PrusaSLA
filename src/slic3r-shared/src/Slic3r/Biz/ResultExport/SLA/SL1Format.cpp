#include "Slic3r/Biz/ResultExport/SLA/SlaArchiveFormat.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SL1.hpp"
#include <libslic3r/SlicingStatus.hpp>
#include "Slic3r/Biz/ResultExport/SLA/AnycubicSLA.hpp"
#include "Slic3r/Biz/ResultExport/SLA/GooSLA.hpp"

#include <vector>
#include <memory>
#include <mutex>
#include <string>
#include <stdexcept>

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

class PM5Format : public ISlaArchiveFormat
{
public:
    std::string name() const override { return "PM5"; }
    std::string description() const override { return "Anycubic PM5 format (not implemented)"; }
    std::vector<std::string> extensions() const override { return {"pm5"}; }
    Slic3r::Biz::Slicing::Sla::FileDataType file_data_type() const override { return Slic3r::Biz::Slicing::Sla::FileDataType::pm5; }

    void store(const std::string& file_path, const Biz::Slicing::SLAResultData& data) const override
    {
        throw Biz::Slicing::Exception{
            Biz::Slicing::Error{Biz::Slicing::ErrorCode::UnsupportedOutputFormat}
        };
    }
};

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

void register_sla_archive_formats()
{
    static std::once_flag once;
    std::call_once(once, [] {
        auto& registry = SlaArchiveFormatRegistry::instance();
        registry.register_format("SL1", []() { return std::make_unique<SL1Format>(); });
        registry.register_format("SL1_SVG", []() { return std::make_unique<SL1SVGFormat>(); });
        registry.register_format("Anycubic", []() { return std::make_unique<AnycubicFormat>(); });
        registry.register_format("PM5", []() { return std::make_unique<PM5Format>(); });
        registry.register_format("Goo", []() { return std::make_unique<GooFormat>(); });
    });
}

} // namespace Slic3r::Biz::PrintHost::Sla
