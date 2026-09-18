#ifndef SLAARCHIVEFORMAT_HPP
#define SLAARCHIVEFORMAT_HPP

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>

#include "libslic3r/SLAResult.hpp"

namespace Slic3r::Biz::PrintHost::Sla {

class ISlaArchiveFormat
{
public:
    virtual ~ISlaArchiveFormat() = default;

    /// Unique format identifier (e.g., "SL1", "SL1_SVG")
    virtual std::string name() const = 0;

    /// Human-readable description
    virtual std::string description() const = 0;

    /// Supported file extensions (without leading dot, e.g., {"sl1", "sl1s", "zip"})
    virtual std::vector<std::string> extensions() const = 0;

    /// The file data type this format produces
    virtual Slic3r::Biz::Slicing::Sla::FileDataType file_data_type() const = 0;


    /// Write the slicing result to a file
    /// Throws exception on failure (no space, no privilege, etc.)
    virtual void store(const std::string& file_path, const Biz::Slicing::SLAResultData& data) const = 0;
};

/// Registry for SLA archive formats
class SlaArchiveFormatRegistry
{
public:
    using CreatorFn = std::function<std::unique_ptr<ISlaArchiveFormat>()>;

    static SlaArchiveFormatRegistry& instance();

    /// Register a format creator
    void register_format(const std::string& name, CreatorFn creator);

    /// Get a format by name (case-insensitive), returns nullptr if not found
    std::unique_ptr<ISlaArchiveFormat> get(const std::string& name) const;

    /// Get all registered format names
    std::vector<std::string> names() const;

    /// Find a format by file extension (case-insensitive), returns nullptr if not found
    std::unique_ptr<ISlaArchiveFormat> find_by_extension(const std::string& ext) const;

private:
    SlaArchiveFormatRegistry() = default;
    struct Entry {
        CreatorFn creator;
        std::vector<std::string> extensions;
    };
    std::map<std::string, Entry, std::less<>> m_formats;
};

/// Register built-in SLA archive formats (SL1, SL1_SVG)
void register_sla_archive_formats();

} // namespace Slic3r::Biz::PrintHost::Sla

#endif // SLAARCHIVEFORMAT_HPP
