#pragma once

#include "Slic3r/Biz/Algorithms/MiniZWrapper.hpp" // IWYU pragma: keep

#include <cstddef>
#include <locale>
#include <map>
#include <optional>
#include <sstream>
#include <string>
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

/// @brief A zip opened for reading, closed again when it goes out of scope.
class ZipReader
{
public:
    explicit ZipReader(const std::string &fname);
    ~ZipReader();

    ZipReader(const ZipReader &)            = delete;
    ZipReader &operator=(const ZipReader &) = delete;

    bool           ok() const { return m_ok; }
    mz_zip_archive &archive() { return m_zip.arch; }

private:
    Algorithms::MZ_Archive m_zip;
    bool                   m_ok = false;
};

/// @brief Read one entry of an open archive into memory.
/// @param max_bytes Largest uncompressed size accepted, 0 for no limit. The size is the one
/// the central directory declares, so an entry that claims more is refused before anything
/// is allocated for it, which is what an entry of a zip bomb is for.
tl::expected<std::string, std::string> read_zip_entry(mz_zip_archive                 &arch,
                                                      const mz_zip_archive_file_stat &stat,
                                                      std::size_t                     max_bytes = 0);

} // namespace Slic3r::Biz
