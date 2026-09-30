#pragma once

#include "Slic3r/Biz/Algorithms/MiniZWrapper.hpp" // IWYU pragma: keep

#include <cstddef>
#include <cstdint>
#include <locale>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <boost/algorithm/string.hpp>

#include <miniz.h>
#include <tl/expected.hpp>

namespace Slic3r::Biz {

/// @brief The `key = value` pairs of an ini file, in the spelling of the file.
using ArchiveIniMap = std::map<std::string, std::string>;

/// @brief Parse a `key = value` ini text: `[section]` headers, ';' and '#' comments and
/// surrounding whitespace are skipped. Enough for the metadata files of an SLA archive,
/// and it does not need the legacy config machinery.
ArchiveIniMap parse_archive_ini(const std::string &text);

/// @brief Read a value the way the ini files of an archive are written: with the classic
/// locale, so that a comma decimal separator of the running system cannot change it.
template<typename T> std::optional<T> parse_ini_value(const std::string &text)
{
    if (text.empty())
        return std::nullopt;

    std::istringstream in{text};
    in.imbue(std::locale::classic());

    T value{};
    in >> value;
    if (in.fail())
        return std::nullopt;

    return value;
}

/// @brief Read one value of an ini map, or nothing if the key is missing or unreadable.
template<typename T> std::optional<T> get_ini_value(const ArchiveIniMap &ini, const char *key)
{
    const auto it = ini.find(key);
    if (it == ini.end())
        return std::nullopt;

    return parse_ini_value<T>(it->second);
}

/// @brief Read a string value of an ini map, whitespace included, trimmed.
std::optional<std::string> get_ini_string(const ArchiveIniMap &ini, const char *key);

/// @brief Read a boolean value of an ini map, where 1/true/yes and 0/false/no are both
/// accepted, as the archive writers and readers do.
std::optional<bool> get_ini_bool(const ArchiveIniMap &ini, const char *key);

/// @brief What an archive is allowed to declare before it is opened.
///
/// Opening a zip through miniz reads its whole central directory into memory and builds an index of
/// one entry per file, so what the archive declares there is what the open costs - a cap applied
/// after the open has been paid for. Both numbers are in the end-of-central-directory record, which
/// is read from the tail of the file, so a caller that sets them is protected before miniz is asked
/// to allocate anything.
struct ZipLimits
{
    /// @brief Largest number of entries the central directory may declare, 0 for no cap.
    std::size_t max_entries = 0;
    /// @brief Largest central directory, in bytes, 0 for no cap.
    std::size_t max_directory_bytes = 0;
};

/// @brief The end-of-central-directory record of a zip, which is what an open costs.
struct ZipDirectoryInfo
{
    /// @brief Whether the record was found at all. An archive that has none is not a zip as far as
    /// this is concerned, and the open itself says so.
    bool found{false};
    /// @brief Number of entries of the central directory, from the zip64 record when the 16 bit
    /// count of the record is saturated.
    std::size_t entries = 0;
    /// @brief Size of the central directory in bytes, which is what miniz allocates for it.
    std::size_t directory_bytes = 0;
    /// @brief Where the central directory starts in the file.
    std::size_t directory_offset = 0;
};

/**
 * @brief Read the end-of-central-directory record of a zip out of the tail of its file.
 *
 * The record is the last thing in a zip file apart from a comment, so it is looked for from the end
 * of @p tail backwards and only a record whose comment length reaches the end of the file is taken:
 * a signature further in is a coincidence in a comment or in the data of an entry, and a zip whose
 * record is not where it belongs is one miniz has to be left to judge. A zip64 record is followed
 * when the 16 bit count or the 32 bit sizes of the record are saturated, which is the only way an
 * archive can declare more than miniz reads.
 *
 * Nothing is evaluated and nothing is allocated for the file itself, so this is safe to call on a
 * file of any size, and on one that is not a zip at all.
 *
 * @param tail The last bytes of the file, at most 64 kB plus the record for a zip to be found in
 *             it, whatever the file weighs.
 * @param file_size Size of the whole file, which the zip64 offsets are counted from.
 * @return Nothing when there is no record of that shape in the tail.
 */
std::optional<ZipDirectoryInfo> read_zip_directory_info(std::string_view tail, std::size_t file_size);

/**
 * @brief Whether @p fname is a zip, decided from the end of the file alone.
 *
 * The end-of-central-directory record is the last thing in a zip but its comment, so a file that
 * has one is a zip and a file that has none is not, and neither answer costs reading the file or
 * opening it. This is what a caller sniffs a file with: which format it is in is a different
 * question from what opening it would cost, which is what ZipLimits is for.
 */
bool is_zip_file(const std::string& fname);

/// @brief A zip opened for reading, closed again when it goes out of scope.
class ZipReader
{
public:
    /// @param limits What the archive may declare before it is opened, see ZipLimits. The default
    ///        opens any archive, which is what a caller that reads an archive it wrote itself wants.
    explicit ZipReader(const std::string& fname, const ZipLimits& limits = {});
    ~ZipReader();

    ZipReader(const ZipReader &)            = delete;
    ZipReader &operator=(const ZipReader &) = delete;

    bool           ok() const { return m_ok; }
    mz_zip_archive &archive() { return m_zip.arch; }

    /// @brief Why the archive was not opened, empty when it was. Only the caps say anything here;
    /// an archive that is not a zip is a failure of the open itself, which says so in the return.
    const std::string &error() const { return m_error; }

private:
    Algorithms::MZ_Archive m_zip;
    bool                   m_ok = false;
    std::string            m_error;
};

/// @brief Read one entry of an open archive into memory.
/// @param max_bytes Largest uncompressed size accepted, 0 for no limit. The size is the one
/// the central directory declares, so an entry that claims more is refused before anything
/// is allocated for it, which is what an entry of a zip bomb is for.
tl::expected<std::string, std::string> read_zip_entry(mz_zip_archive                 &arch,
                                                      const mz_zip_archive_file_stat &stat,
                                                      std::size_t                     max_bytes = 0);

} // namespace Slic3r::Biz
