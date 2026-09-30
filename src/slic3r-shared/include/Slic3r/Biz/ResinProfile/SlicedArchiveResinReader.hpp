#pragma once

#include "Slic3r/Biz/ResinProfile/IResinProfileReader.hpp"

#include <cstddef>

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
 * A sliced job of a real model is tens or hundreds of megabytes of layer images, and none
 * of them is needed here. The reader therefore reads its container lazily: it is picked by
 * the extension the user chose plus the central directory of the zip, which names every
 * entry without reading any of them, and it extracts the two ini entries and nothing else.
 * The registry is told about that (reads_container_lazily) and does not cap the file size
 * for it; the sizes that matter are capped here, MAX_INI_ENTRY_SIZE for one entry and
 * MAX_ENTRIES_SCANNED for the entries walked through, both checked against what the
 * central directory declares, so an entry that promises gigabytes is refused before a
 * buffer is allocated for it.
 *
 * Opening a zip reads the whole central directory and builds an index of one entry per
 * file, so the entries and the size of that directory are capped before the open, out of the
 * end-of-central-directory record (MAX_ENTRIES_SCANNED and MAX_CENTRAL_DIRECTORY_SIZE). A
 * zip that declares more than either of them is refused with both numbers in the error,
 * before miniz is asked for the memory it would take.
 *
 * sniff() stays as it is: it is how a small archive of this kind is recognised when the
 * registry does content sniffing, and where a file is picked by the user the .sl1/.sl1s
 * extension is checked as well.
 */
class SlicedArchiveResinReader : public IResinProfileReader
{
public:
    SlicedArchiveResinReader() = default;

    std::string format_id() const override { return "sliced-archive"; }

    bool sniff(const std::string& head) const override;

    /// @brief The archive is opened and its two ini entries are extracted, not the file read.
    bool reads_container_lazily() const override { return true; }

    /// @brief A .sl1 or .sl1s that is a zip, read from the end of the file without opening it. read()
    /// says what an archive over the caps is, so that a refusal with both numbers in it reaches the
    /// user instead of a file no reader recognised.
    bool sniff_path(const boost::filesystem::path& path) const override;

    tl::expected<ForeignResinProfile, std::string> read(const boost::filesystem::path& path) const override;

    /// @brief Largest ini entry that is extracted from an archive (1 MB). The two metadata
    /// files of a job are a few kilobytes, so this is generous; it is a ceiling for an
    /// untrusted archive, not a limit a real one reaches.
    static constexpr std::size_t MAX_INI_ENTRY_SIZE = 1024 * 1024;

    /// @brief Largest number of zip entries the central directory may declare and this reader looks
    /// through (16384). A job has one entry per layer, so this is a print of 16 thousand layers at
    /// 0.05 mm, while a zip crafted to make the reader busy is refused instead. It is checked before
    /// the archive is opened, because the open is what reads the directory and indexes it.
    static constexpr unsigned MAX_ENTRIES_SCANNED = 16384;

    /// @brief Largest central directory this reader opens (4 MB). An entry of it is 46 bytes and a
    /// name, so 16384 entries of a sliced job come to well under a megabyte; what is left over is
    /// the room a real archive needs and an archive crafted to be expensive does not get.
    static constexpr std::size_t MAX_CENTRAL_DIRECTORY_SIZE = 4 * 1024 * 1024;
};

} // namespace Slic3r::Biz::ResinProfile
