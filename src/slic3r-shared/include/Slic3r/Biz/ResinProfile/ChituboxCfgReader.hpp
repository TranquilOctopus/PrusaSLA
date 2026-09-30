#pragma once

#include "Slic3r/Biz/ResinProfile/IResinProfileReader.hpp"

namespace Slic3r::Biz::ResinProfile {

/**
 * @brief Reader for Chitubox .cfg resin profile files.
 *
 * Chitubox .cfg is a flat text file of "key:value" lines, e.g. "normalExposureTime:2.5".
 * The extension .cfg is generic, so format detection is done by content sniffing
 * for distinctive keys such as "normalExposureTime".
 *
 * Every key is kept verbatim in ForeignResinProfile::raw_values and the settings this fork maps are
 * filled into its material, the rest is left to the mapper. There is no vendor or brand key among
 * them: nothing in the reader's key table or in the mapping table names one, and the format notes of
 * M3.2 (doc/sla-fork/formats/chitubox-cfg.md) do not exist yet, so a .cfg is read without a vendor
 * and ResinMaterialSettings::material_vendor stays empty rather than guessing at a spelling.
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