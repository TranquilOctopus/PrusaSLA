#pragma once

#include "Slic3r/Biz/ResinProfile/IResinProfileReader.hpp"
#include "Slic3r/Biz/ResinProfile/ForeignResinProfile.hpp"
#include <boost/filesystem/path.hpp>
#include <memory>
#include <string>
#include <tl/expected.hpp>
#include <vector>

namespace Slic3r::Biz::ResinProfile {

/**
 * @brief Registry of resin profile readers.
 * Picks the correct reader by calling sniff() on the first 4 KB of the file.
 */
class ResinProfileReaderRegistry
{
public:
    ResinProfileReaderRegistry() = default;

    /// @brief Register a reader. The registry takes ownership.
    void register_reader(std::unique_ptr<IResinProfileReader> reader);

    /// @brief Read a resin profile, auto-detecting the format from file content.
    /// Reads up to 4 KB for sniffing, then delegates to the matching reader.
    /// @return ForeignResinProfile on success, error message on failure (no reader matched, file too large, I/O error, etc.).
    tl::expected<ForeignResinProfile, std::string> read_file(const boost::filesystem::path& path) const;

    /// @brief Maximum file size allowed for reading (8 MB).
    static constexpr std::size_t MAX_FILE_SIZE = 8 * 1024 * 1024;

    /// @brief Number of bytes read for format sniffing (4 KB).
    static constexpr std::size_t SNIFF_BYTES = 4 * 1024;

private:
    std::vector<std::unique_ptr<IResinProfileReader>> m_readers;
};

} // namespace Slic3r::Biz::ResinProfile