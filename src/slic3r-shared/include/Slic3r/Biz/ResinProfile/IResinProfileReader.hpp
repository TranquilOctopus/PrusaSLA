#pragma once

#include "Slic3r/Biz/ResinProfile/ForeignResinProfile.hpp"
#include <boost/filesystem/path.hpp>
#include <string>
#include <tl/expected.hpp>

namespace Slic3r::Biz::ResinProfile {

/**
 * @brief Abstract interface for reading a foreign resin profile format.
 * Each implementation handles one file format (e.g. Chitubox .cfg, Lychee .lgs, etc.).
 */
class IResinProfileReader
{
public:
    virtual ~IResinProfileReader() = default;

    /// @brief Return a unique format identifier, e.g. "chitubox-cfg".
    virtual std::string format_id() const = 0;

    /// @brief Content-based sniffing: decide if the given file head matches this format.
    /// The .cfg extension is generic, so readers must inspect the first bytes.
    /// @param head First ~4 KB of the file (or entire file if smaller).
    /// @return true if this reader should handle the file.
    virtual bool sniff(const std::string& head) const = 0;

    /// @brief Read a resin profile from the given file path.
    /// @return ForeignResinProfile on success, error message string on failure.
    virtual tl::expected<ForeignResinProfile, std::string> read(const boost::filesystem::path& path) const = 0;
};

} // namespace Slic3r::Biz::ResinProfile