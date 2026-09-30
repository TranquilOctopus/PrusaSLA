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

    /// @brief Whether this reader reads a container (an archive) lazily: it opens the
    /// container, extracts the few entries it needs and never holds the whole file in
    /// memory. The registry then leaves the size of the file to the reader, which caps what
    /// it extracts, instead of refusing a file over MAX_FILE_SIZE before it sniffs anything.
    /// A reader of a plain text file keeps the default and is held to that ceiling.
    virtual bool reads_container_lazily() const { return false; }

    /// @brief Whether the path alone names a file of this format, for a reader that reads
    /// its container lazily. The extension is the user's choice and the container says what
    /// it holds, so the head of the file is not needed and a big file is not read into it.
    /// @return true if this reader should handle the file.
    virtual bool sniff_path(const boost::filesystem::path& path) const { return false; }

    /// @brief Read a resin profile from the given file path.
    /// @return ForeignResinProfile on success, error message string on failure.
    virtual tl::expected<ForeignResinProfile, std::string> read(const boost::filesystem::path& path) const = 0;
};

} // namespace Slic3r::Biz::ResinProfile