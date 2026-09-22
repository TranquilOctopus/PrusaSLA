#pragma once

#include "Slic3r/Biz/ResinProfile/IResinProfileReader.hpp"

namespace Slic3r::Biz::ResinProfile {

/**
 * @brief Reader for Chitubox .cfg resin profile files.
 * 
 * Chitubox .cfg is a flat text file of "key:value" lines, e.g. "normalExposureTime:2.5".
 * The extension .cfg is generic, so format detection is done by content sniffing
 * for distinctive keys such as "normalExposureTime".
 */
class ChituboxCfgReader : public IResinProfileReader
{
public:
    ChituboxCfgReader() = default;

    std::string format_id() const override { return "chitubox-cfg"; }

    bool sniff(const std::string& head) const override;

    tl::expected<ForeignResinProfile, std::string> read(const boost::filesystem::path& path) const override;
};

} // namespace Slic3r::Biz::ResinProfile