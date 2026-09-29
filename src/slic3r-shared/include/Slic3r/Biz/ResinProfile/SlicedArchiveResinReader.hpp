#pragma once

#include "Slic3r/Biz/ResinProfile/IResinProfileReader.hpp"

namespace Slic3r::Biz::ResinProfile {

/**
 * @brief Reader for the material settings of a sliced Prusa archive, .sl1 and .sl1s.
 *
 * Such an archive is a zip holding a `config.ini` with the printer and print settings of
 * the job and, since the profile was embedded, a `prusaslicer.ini` with the whole
 * configuration. The two name the same setting differently (`expTime` and `exposure_time`),
 * so the reader maps both spellings into the ResinMaterialSettings of the profile and keeps
 * every key of both files in raw_values for the mapper (M3.5) to deal with.
 *
 * The extension is not a reliable enough hint on its own: a .sl1 is a zip, as are .3mf and
 * the Chitubox formats M3.12 will read, so sniff() looks for the zip magic plus a config.ini
 * entry. Both archive writers put that entry first, which is what puts its name into the
 * head the registry sniffs. Where a file is picked by the user the .sl1/.sl1s extension is
 * checked as well.
 *
 * Note that the registry refuses a file over 8 MB before it sniffs anything, which a sliced
 * job with many layers goes past. Only the ini entries are read here, so lifting that
 * ceiling is a matter of the size limit alone, not of this reader.
 */
class SlicedArchiveResinReader : public IResinProfileReader
{
public:
    SlicedArchiveResinReader() = default;

    std::string format_id() const override { return "sliced-archive"; }

    bool sniff(const std::string& head) const override;

    tl::expected<ForeignResinProfile, std::string> read(const boost::filesystem::path& path) const override;
};

} // namespace Slic3r::Biz::ResinProfile
