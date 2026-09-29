#include "Slic3r/Biz/ResinProfile/ResinProfileReaderRegistry.hpp"

#include "Slic3r/Biz/ResinProfile/ChituboxCfgReader.hpp"
#include "Slic3r/Biz/ResinProfile/SlicedArchiveResinReader.hpp"

#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r::Biz::ResinProfile {

void ResinProfileReaderRegistry::register_reader(std::unique_ptr<IResinProfileReader> reader)
{
    m_readers.push_back(std::move(reader));
}

tl::expected<ForeignResinProfile, std::string> ResinProfileReaderRegistry::read_file(const boost::filesystem::path& path) const
{
    // Check file size first
    boost::system::error_code ec;
    const auto file_size = boost::filesystem::file_size(path, ec);
    if (ec) {
        return tl::make_unexpected(std::string("Cannot get file size: ") + ec.message());
    }
    if (file_size > static_cast<boost::uintmax_t>(MAX_FILE_SIZE)) {
        return tl::make_unexpected("File too large (max 8 MB)");
    }

    // Read first SNIFF_BYTES for format detection
    boost::filesystem::ifstream file(path, std::ios::binary);
    if (!file) {
        return tl::make_unexpected("Failed to open file for reading");
    }

    std::string head;
    head.resize(SNIFF_BYTES);
    file.read(head.data(), static_cast<std::streamsize>(SNIFF_BYTES));
    head.resize(static_cast<size_t>(file.gcount()));

    // Find matching reader
    for (const auto& reader : m_readers) {
        if (reader->sniff(head)) {
            return reader->read(path);
        }
    }

    return tl::make_unexpected("Unrecognized resin profile format");
}

void register_resin_profile_readers(ResinProfileReaderRegistry& registry)
{
    registry.register_reader(std::make_unique<ChituboxCfgReader>());
    // The .sl1 and .sl1s of every Prusa machine, and later the archives of the other
    // writers of M5.3.
    registry.register_reader(std::make_unique<SlicedArchiveResinReader>());
}

} // namespace Slic3r::Biz::ResinProfile
