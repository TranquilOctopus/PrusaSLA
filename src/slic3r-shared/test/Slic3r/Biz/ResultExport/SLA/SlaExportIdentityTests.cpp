#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/SlaFixture.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SL1.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SlaArchiveFormat.hpp"
#include "Slic3r/TestUtils/TestTempDir.hpp"

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/nowide/fstream.hpp>
#include <fstream>
#include <sstream>
#include "miniz.h"

namespace fs = boost::filesystem;

using Slic3r::Biz::Slicing::SLAResultData;
using Slic3r::Biz::PrintHost::Sla::store_sl1;
using Slic3r::Biz::PrintHost::Sla::SlaArchiveFormatRegistry;
using Slic3r::Biz::PrintHost::Sla::register_sla_archive_formats;
using Slic3r::Biz::Slicing::Sla::FileDataType;

static std::string read_file(const fs::path& path)
{
    boost::nowide::ifstream file(path);
    file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static std::vector<uint8_t> read_file_binary(const fs::path& path)
{
    boost::nowide::ifstream file(path.string(), std::ios::binary);
    file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

static bool files_equal(const fs::path& a, const fs::path& b)
{
    auto data_a = read_file_binary(a);
    auto data_b = read_file_binary(b);
    return data_a == data_b;
}

class Zip {
public:

    using FileData = std::vector<std::byte>;

    /** Load zip files to memory */
    Zip(const char* filename) {
        const auto mz_zip_deleter{[](mz_zip_archive* ptr){
            mz_zip_reader_end(ptr);
            delete ptr;
        }};
        const auto mz_zip_creator{[](){
            auto result{new mz_zip_archive};
            memset(result, 0, sizeof(*result));
            return result;
        }};

        const std::unique_ptr<mz_zip_archive, decltype(mz_zip_deleter)>
            zip_archive{mz_zip_creator(), mz_zip_deleter};

        if (!mz_zip_reader_init_file(zip_archive.get(), filename, 0)) {
            throw std::runtime_error{"Could not open zip file!"};
        }

        const mz_uint num_files{mz_zip_reader_get_num_files(zip_archive.get())};


        for (mz_uint i = 0; i < num_files; i++) {
            mz_zip_archive_file_stat file_stat;
            if (!mz_zip_reader_file_stat(zip_archive.get(), i, &file_stat)) {
                throw std::runtime_error{"Failed to read filename from zip!"};
            }

            const auto mz_file_data_deleter{[](void* ptr){
                mz_free(ptr);
            }};
            size_t file_data_size;
            const std::unique_ptr<void, decltype(mz_file_data_deleter)>
                file_data{mz_zip_reader_extract_to_heap(zip_archive.get(), i, &file_data_size, 0), mz_file_data_deleter};

            if (!file_data) {
                throw std::runtime_error{"Failed to read file from zip!"};
            }

            const auto data{static_cast<std::byte*>(file_data.get())};
            m_files.insert({file_stat.m_filename, FileData(data, data + file_data_size)});
        }
    }

    const std::map<std::string, FileData>& files() const {
        return m_files;
    }

    const std::vector<std::string> entry_names() const {
        std::vector<std::string> names;
        names.reserve(m_files.size());
        for (const auto& [name, _] : m_files) {
            names.push_back(name);
        }
        return names;
    }

private:
    std::map<std::string, FileData> m_files;
};

std::string to_string(const Zip::FileData& data) {
    return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

std::istringstream as_istream(const Zip::FileData& data) {
    return std::istringstream{to_string(data)};
}

std::vector<std::string> compare_files_by_lines(const Zip::FileData& a, const Zip::FileData& b) {
    std::istringstream a_stream{as_istream(a)};
    std::istringstream b_stream{as_istream(b)};
    std::vector<std::string> result;
    std::string line_a, line_b;
    std::vector<std::string> lines_a, lines_b;
    while (std::getline(a_stream, line_a)) {
        if (!line_a.empty()) lines_a.push_back(line_a);
    }
    while (std::getline(b_stream, line_b)) {
        if (!line_b.empty()) lines_b.push_back(line_b);
    }
    std::sort(lines_a.begin(), lines_a.end());
    std::sort(lines_b.begin(), lines_b.end());
    std::set_symmetric_difference(
        lines_a.begin(), lines_a.end(), lines_b.begin(), lines_b.end(), std::back_inserter(result)
    );
    return result;
}

static void remove_timestamp_lines(std::vector<std::string>& lines) {
    lines.erase(std::remove_if(lines.begin(), lines.end(),
        [](std::string s) {
            return s.rfind("fileCreationTimestamp", 0) == 0;
        }), lines.end());
}

TEST_CASE("SLA slicing determinism", "[export][sla][determinism]")
{
    Slic3r::Test::SlaSlicingFixture fixture;

    auto model = Slic3r::Test::generate_cubes(1, 5);
    auto config = Slic3r::Domain::ConfigPackSLA{};

    auto result1 = fixture.slice_sla_model(model, config);
    REQUIRE(result1 != nullptr);

    auto result2 = fixture.slice_sla_model(model, config);
    REQUIRE(result2 != nullptr);

    REQUIRE(result1->files.data.size() == result2->files.data.size());
    REQUIRE(result1->files.type == result2->files.type);
    REQUIRE(result1->layer_areas == result2->layer_areas);
    REQUIRE(result1->layer_peel_force == result2->layer_peel_force);

    for (size_t i = 0; i < result1->files.data.size(); ++i) {
        REQUIRE(result1->files.data[i] == result2->files.data[i]);
    }

    REQUIRE(result1->serialized_config.ini == result2->serialized_config.ini);
    REQUIRE(result1->serialized_config.json == result2->serialized_config.json);
    REQUIRE(result1->project_name == result2->project_name);
}

TEST_CASE("SL1 export byte identity: direct vs registry", "[export][sla][identity]")
{
    Slic3r::Test::SlaSlicingFixture fixture;

    auto model = Slic3r::Test::generate_cubes(1, 5);
    auto config = Slic3r::Domain::ConfigPackSLA{};

    auto sla_result = fixture.slice_sla_model(model, config);
    REQUIRE(sla_result != nullptr);

    Tests::TestTempDir temp_dir;

    fs::path direct_path = temp_dir.path() / "direct.sl1";
    fs::path registry_path = temp_dir.path() / "registry.sl1";

    // Export directly via store_sl1
    REQUIRE_NOTHROW(store_sl1(direct_path.string(), *sla_result));

    // Export via registry (find_by_file_data_type + store)
    register_sla_archive_formats();
    auto& registry = SlaArchiveFormatRegistry::instance();
    auto format = registry.find_by_file_data_type(sla_result->files.type);
    REQUIRE(format != nullptr);
    REQUIRE_NOTHROW(format->store(registry_path.string(), *sla_result));

    REQUIRE(fs::exists(direct_path));
    REQUIRE(fs::exists(registry_path));

    // SL1 is a zip containing config.ini with a timestamp (fileCreationTimestamp).
    // Compare unzipped entries using miniz.
    Zip direct_zip(direct_path.string().c_str());
    Zip registry_zip(registry_path.string().c_str());

    // Same entry names in the same order
    REQUIRE(direct_zip.entry_names() == registry_zip.entry_names());

    // Byte-identical content for every entry except config.ini
    for (const auto& entry_name : direct_zip.entry_names()) {
        const auto& direct_data = direct_zip.files().at(entry_name);
        const auto& registry_data = registry_zip.files().at(entry_name);

        if (entry_name == "config.ini") {
            // For config.ini, compare lines after dropping fileCreationTimestamp
            auto diff_lines = compare_files_by_lines(direct_data, registry_data);
            remove_timestamp_lines(diff_lines);
            INFO("config.ini diff: " << to_string(diff_lines));
            REQUIRE(diff_lines.empty());
        } else {
            // All other entries must be byte-identical
            REQUIRE(direct_data == registry_data);
        }
    }
}