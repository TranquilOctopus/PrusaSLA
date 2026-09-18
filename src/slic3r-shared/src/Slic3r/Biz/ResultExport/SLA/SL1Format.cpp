#include "Slic3r/Biz/ResultExport/SLA/SlaArchiveFormat.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SL1.hpp"

#include <libslic3r/Format/SL1.hpp>
#include <libslic3r/Format/SL1_SVG.hpp>

#include <vector>
#include <memory>
#include <string>

namespace Slic3r::Biz::PrintHost::Sla {

class SL1Format : public ISlaArchiveFormat
{
public:
    std::string name() const override { return "SL1"; }
    std::string description() const override { return "Prusa SL1/SL1S format"; }
    std::vector<std::string> extensions() const override { return {"sl1", "sl1s", "zip"}; }
    Slic3r::Biz::Slicing::Sla::FileDataType file_data_type() const override { return Slic3r::Biz::Slicing::Sla::FileDataType::sl1_png; }

    std::unique_ptr<ISlaRasterizer> create_rasterizer(const SLAPrintConfigView& cfg) const override
    {
        return Slic3r::create_sl1_rasterizer(cfg);
    }

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

    std::unique_ptr<ISlaRasterizer> create_rasterizer(const SLAPrintConfigView& cfg) const override
    {
        return Slic3r::create_sl1_svg_rasterizer(cfg);
    }

    void store(const std::string& file_path, const Biz::Slicing::SLAResultData& data) const override
    {
        store_sl1(file_path, data);
    }
};

void register_sla_archive_formats()
{
    auto& registry = SlaArchiveFormatRegistry::instance();
    registry.register_format("SL1", []() { return std::make_unique<SL1Format>(); });
    registry.register_format("SL1_SVG", []() { return std::make_unique<SL1SVGFormat>(); });
}

} // namespace Slic3r::Biz::PrintHost::Sla
