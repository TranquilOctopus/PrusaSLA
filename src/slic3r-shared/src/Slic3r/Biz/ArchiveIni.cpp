#include "Slic3r/Biz/ArchiveIni.hpp"

#include <fmt/format.h>

namespace Slic3r::Biz {

ArchiveIniMap parse_archive_ini(const std::string &text)
{
    ArchiveIniMap map;

    std::istringstream in{text};
    std::string        line;
    while (std::getline(in, line)) {
        line = boost::algorithm::trim(line);
        if (line.empty() || line.front() == ';' || line.front() == '#' || line.front() == '[')
            continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        std::string key = boost::algorithm::trim(line.substr(0, eq));
        if (key.empty())
            continue;

        map[key] = boost::algorithm::trim(line.substr(eq + 1));
    }

    return map;
}

std::optional<std::string> get_ini_string(const ArchiveIniMap &ini, const char *key)
{
    const auto it = ini.find(key);
    if (it == ini.end())
        return std::nullopt;

    std::string value = boost::algorithm::trim(it->second);
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

ZipReader::ZipReader(const std::string &fname)
    : m_ok(Algorithms::open_zip_reader(&m_zip.arch, fname))
{
}

ZipReader::~ZipReader()
{
    if (m_ok)
        Algorithms::close_zip_reader(&m_zip.arch);
}

tl::expected<std::string, std::string> read_zip_entry(mz_zip_archive                 &arch,
                                                      const mz_zip_archive_file_stat &stat)
{
    if (stat.m_uncomp_size == 0)
        return std::string{};

    std::string buf(size_t(stat.m_uncomp_size), '\0');
    if (!mz_zip_reader_extract_to_mem(&arch, stat.m_file_index, buf.data(), buf.size(), 0))
        return tl::make_unexpected(
            fmt::format("Cannot extract {} from the archive.", stat.m_filename));

    return std::move(buf);
}

} // namespace Slic3r::Biz
