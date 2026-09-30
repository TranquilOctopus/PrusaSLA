#include "Slic3r/Biz/ArchiveIni.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <boost/nowide/fstream.hpp>

namespace Slic3r::Biz {

namespace {

// The three records at the end of a zip, by their four byte signatures. The end-of-central-
// directory record holds the entry count and the size of the directory; the zip64 locator points at
// the zip64 end-of-central-directory record, which holds the same numbers when the 16 and 32 bit
// fields of the ordinary record are saturated.
constexpr std::uint32_t EOCD_SIGNATURE          = 0x06054b50;
constexpr std::uint32_t ZIP64_LOCATOR_SIGNATURE = 0x07064b50;
constexpr std::uint32_t ZIP64_EOCD_SIGNATURE    = 0x06064b50;

constexpr std::size_t EOCD_SIZE          = 22;
constexpr std::size_t ZIP64_LOCATOR_SIZE = 20;
constexpr std::size_t ZIP64_EOCD_SIZE    = 56;

// A zip comment may be 65535 bytes, so the last record of a zip is at most this far from the end of
// the file. Anything past it cannot be found without reading the whole file, which is the point.
constexpr std::size_t MAX_TAIL_BYTES = 0xffff + EOCD_SIZE;

// The offsets of the numbers the end-of-central-directory record holds, and of the same numbers in
// the zip64 record, which are 64 bit wide and further into the record.
constexpr std::size_t EOCD_ENTRIES_OFS          = 10;
constexpr std::size_t EOCD_DIRECTORY_SIZE_OFS   = 12;
constexpr std::size_t EOCD_DIRECTORY_OFFSET_OFS = 16;
constexpr std::size_t EOCD_COMMENT_SIZE_OFS     = 20;

constexpr std::size_t ZIP64_LOCATOR_RECORD_OFS        = 8;
constexpr std::size_t ZIP64_EOCD_ENTRIES_OFS          = 32;
constexpr std::size_t ZIP64_EOCD_DIRECTORY_SIZE_OFS   = 40;
constexpr std::size_t ZIP64_EOCD_DIRECTORY_OFFSET_OFS = 48;

// The values that mean "this number is in the zip64 record instead".
constexpr std::size_t SATURATED_U16 = 0xffff;
constexpr std::size_t SATURATED_U32 = 0xffffffff;

unsigned char byte_at(std::string_view text, std::size_t at)
{
    return static_cast<unsigned char>(text[at]);
}

std::uint16_t le16(std::string_view text, std::size_t at)
{
    return static_cast<std::uint16_t>(byte_at(text, at) | (byte_at(text, at + 1) << 8));
}

std::uint32_t le32(std::string_view text, std::size_t at)
{
    return static_cast<std::uint32_t>(byte_at(text, at)) | (static_cast<std::uint32_t>(byte_at(text, at + 1)) << 8)
           | (static_cast<std::uint32_t>(byte_at(text, at + 2)) << 16)
           | (static_cast<std::uint32_t>(byte_at(text, at + 3)) << 24);
}

std::uint64_t le64(std::string_view text, std::size_t at)
{
    return static_cast<std::uint64_t>(le32(text, at)) | (static_cast<std::uint64_t>(le32(text, at + 4)) << 32);
}

/// Fill the numbers of @p info from the zip64 end-of-central-directory record, which the ordinary
/// record is followed by (through its locator) when its own numbers are saturated.
void read_zip64_directory_info(
    std::string_view tail,
    std::size_t       file_size,
    std::size_t       eocd_at,
    ZipDirectoryInfo &info
)
{
    if (eocd_at < ZIP64_LOCATOR_SIZE)
        return;
    const std::size_t locator = eocd_at - ZIP64_LOCATOR_SIZE;
    if (le32(tail, locator) != ZIP64_LOCATOR_SIGNATURE)
        return;

    // The zip64 record is named by an offset into the whole file, and it sits just before the
    // locator, so it is in the tail as long as the tail reaches that far back.
    const std::uint64_t record = le64(tail, locator + ZIP64_LOCATOR_RECORD_OFS);
    if (record > file_size || file_size - record > tail.size())
        return;
    const std::size_t at = tail.size() - static_cast<std::size_t>(file_size - record);
    if (at + ZIP64_EOCD_SIZE > tail.size() || le32(tail, at) != ZIP64_EOCD_SIGNATURE)
        return;

    info.entries          = static_cast<std::size_t>(le64(tail, at + ZIP64_EOCD_ENTRIES_OFS));
    info.directory_bytes  = static_cast<std::size_t>(le64(tail, at + ZIP64_EOCD_DIRECTORY_SIZE_OFS));
    info.directory_offset = static_cast<std::size_t>(le64(tail, at + ZIP64_EOCD_DIRECTORY_OFFSET_OFS));
}

/// The end-of-central-directory record as it is in @p fname, or nothing when the file has none.
std::optional<ZipDirectoryInfo> read_zip_directory_info_of_file(const std::string& fname)
{
    boost::nowide::ifstream in{fname, std::ios::binary};
    if (!in.is_open())
        return std::nullopt;

    in.seekg(0, std::ios::end);
    const std::streamoff end = in.tellg();
    if (end <= 0)
        return std::nullopt;
    const std::size_t file_size = static_cast<std::size_t>(end);

    const std::size_t tail_size = std::min(file_size, MAX_TAIL_BYTES);
    in.seekg(static_cast<std::streamoff>(file_size - tail_size), std::ios::beg);
    std::string tail(tail_size, '\0');
    in.read(tail.data(), static_cast<std::streamsize>(tail_size));
    tail.resize(static_cast<std::size_t>(in.gcount()));
    if (tail.size() != tail_size)
        return std::nullopt;

    return read_zip_directory_info(tail, file_size);
}

/// Whether the archive stays inside @p limits, and the reason it does not when it does not. An
/// archive whose record is not where a zip puts it is left to the open, which refuses it or accepts
/// it on its own terms: what is capped here is what the open would cost, not what it may be.
bool within_limits(const std::string& fname, const ZipLimits& limits, std::string& error)
{
    // No caps, no read: a caller that opens an archive it wrote itself pays nothing for this.
    if (limits.max_entries == 0 && limits.max_directory_bytes == 0)
        return true;

    const std::optional<ZipDirectoryInfo> info = read_zip_directory_info_of_file(fname);
    if (!info || !info->found)
        return true;

    if (limits.max_entries > 0 && info->entries > limits.max_entries) {
        error = fmt::format(
            "the archive declares {} entries, more than the {} that are read from one.",
            info->entries,
            limits.max_entries
        );
        return false;
    }
    if (limits.max_directory_bytes > 0 && info->directory_bytes > limits.max_directory_bytes) {
        error = fmt::format(
            "the central directory of the archive declares {} bytes, more than the {} that are read from one.",
            info->directory_bytes,
            limits.max_directory_bytes
        );
        return false;
    }
    return true;
}

} // anonymous namespace

std::optional<ZipDirectoryInfo> read_zip_directory_info(std::string_view tail, std::size_t file_size)
{
    if (tail.size() < EOCD_SIZE || file_size < EOCD_SIZE)
        return std::nullopt;

    // From the last position a whole record fits back to the start of the tail: the record of a zip
    // is the last thing in the file but its comment, so a later one that reaches the end of the
    // file is the record and an earlier one is a coincidence.
    for (std::size_t at = tail.size() - EOCD_SIZE + 1; at-- > 0;) {
        if (le32(tail, at) != EOCD_SIGNATURE)
            continue;
        if (at + EOCD_SIZE + le16(tail, at + EOCD_COMMENT_SIZE_OFS) != tail.size())
            continue;

        ZipDirectoryInfo info;
        info.found            = true;
        info.entries          = le16(tail, at + EOCD_ENTRIES_OFS);
        info.directory_bytes  = le32(tail, at + EOCD_DIRECTORY_SIZE_OFS);
        info.directory_offset = le32(tail, at + EOCD_DIRECTORY_OFFSET_OFS);
        if (info.entries == SATURATED_U16 || info.directory_bytes == SATURATED_U32
            || info.directory_offset == SATURATED_U32)
        {
            read_zip64_directory_info(tail, file_size, at, info);
        }
        return info;
    }
    return std::nullopt;
}

bool is_zip_file(const std::string& fname)
{
    return read_zip_directory_info_of_file(fname).has_value();
}

ArchiveIniMap parse_archive_ini(const std::string &text)
{
    ArchiveIniMap map;

    std::istringstream in{text};
    std::string        line;
    while (std::getline(in, line)) {
        line = boost::algorithm::trim_copy(line);
        if (line.empty() || line.front() == ';' || line.front() == '#' || line.front() == '[')
            continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        std::string key = boost::algorithm::trim_copy(line.substr(0, eq));
        if (key.empty())
            continue;

        map[key] = boost::algorithm::trim_copy(line.substr(eq + 1));
    }

    return map;
}

std::optional<std::string> get_ini_string(const ArchiveIniMap &ini, const char *key)
{
    const auto it = ini.find(key);
    if (it == ini.end())
        return std::nullopt;

    std::string value = boost::algorithm::trim_copy(it->second);
    if (value.empty())
        return std::nullopt;

    return value;
}

std::optional<bool> get_ini_bool(const ArchiveIniMap &ini, const char *key)
{
    const auto it = ini.find(key);
    if (it == ini.end() || it->second.empty())
        return std::nullopt;

    std::string value = it->second;
    boost::algorithm::to_lower(value);
    if (value == "1" || value == "true" || value == "yes")
        return true;
    if (value == "0" || value == "false" || value == "no")
        return false;

    return std::nullopt;
}

ZipReader::ZipReader(const std::string &fname, const ZipLimits &limits)
{
    // What the archive declares is checked before miniz is asked to read the central directory,
    // because that read is what costs the memory the caps are there to bound.
    if (!within_limits(fname, limits, m_error))
        return;
    m_ok = Algorithms::open_zip_reader(&m_zip.arch, fname);
}

ZipReader::~ZipReader()
{
    if (m_ok)
        Algorithms::close_zip_reader(&m_zip.arch);
}

tl::expected<std::string, std::string> read_zip_entry(mz_zip_archive                 &arch,
                                                      const mz_zip_archive_file_stat &stat,
                                                      std::size_t                     max_bytes)
{
    if (stat.m_uncomp_size == 0)
        return std::string{};

    if (max_bytes > 0 && stat.m_uncomp_size > max_bytes)
        return tl::make_unexpected(
            fmt::format("{} is declared {} bytes uncompressed, over the {} bytes that are read from an archive entry.",
                        stat.m_filename, stat.m_uncomp_size, max_bytes));

    std::string buf(size_t(stat.m_uncomp_size), '\0');
    if (!mz_zip_reader_extract_to_mem(&arch, stat.m_file_index, buf.data(), buf.size(), 0))
        return tl::make_unexpected(
            fmt::format("Cannot extract {} from the archive.", stat.m_filename));

    return std::move(buf);
}

} // namespace Slic3r::Biz
