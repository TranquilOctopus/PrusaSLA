// The model manifest of the benchmark corpus, shared by the benchmark harness (M0.13) and the
// local island coverage test (M4.3c) so that both read the same folder the same way.
//
// A manifest of the corpus, in the folder named by SLA_BENCH_DIR or by SLA_LOCAL_SAMPLES:
//
//     models:
//       - {id: bm01, file: "5.stl", category: miniature}
//
// A model is named by its `id` and by nothing else. The corpus is not redistributable, so a file
// name and a folder must never reach a report, not even inside the message of a loader: what is
// left of both stays inside ModelSpec, and describe_error() scrubs them out of a message.
//
// The header only, no .cpp: the helpers are small, and the struct descriptions have to be visible
// in both test translation units, which is what the STRUCT_DESC_SIMPLE below is for.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Format/OBJ.hpp"
#include "Slic3r/Biz/Format/STL.hpp"
#include "Slic3r/Biz/Yaml/Yaml.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"

namespace Bench {

// The manifest is optional per field, so a model without a category is still runnable. `id` and
// `file` are required: an id is what the reports call the model and a file is what gets loaded.
struct ManifestEntry
{
    std::string                id;
    std::string                file;
    std::optional<std::string> category;
};

struct Manifest
{
    std::vector<ManifestEntry> models;
};

} // namespace Bench

// The struct descriptions have to come before the first parse_struct_unwrap<Manifest> below: a
// specialization may not be declared after it has already been instantiated.
STRUCT_DESC_SIMPLE(Bench::ManifestEntry, id, file, category);
STRUCT_DESC_SIMPLE(Bench::Manifest, models);

namespace Bench {

/// One model of the corpus. file_name and folder stay with the harness: only id and category
/// reach a report.
struct ModelSpec
{
    std::string id;
    std::string category;
    std::string file_name;
    std::string folder;
    bool        from_test_data = false;
};

inline std::string trim(const std::string& text)
{
    const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    auto       first    = text.begin();
    while (first != text.end() && is_space(*first)) ++first;
    auto last = text.end();
    while (last != first && is_space(*(last - 1))) --last;
    return std::string(first, last);
}

inline std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}

inline std::string lower_extension(const std::filesystem::path& path)
{
    return lower(path.extension().string());
}

/// The models of a manifest.yaml, in the order of the file. Ids have to be unique, they are what
/// the JSON and the reports key on.
inline std::vector<ModelSpec> read_manifest(const std::filesystem::path& path, const std::string& folder)
{
    namespace Yaml = Slic3r::Biz::Yaml;

    Manifest manifest;
    try {
        const Yaml::YamlAdapter::Document doc = Yaml::parse_file(path.string().c_str());
        manifest                              = Yaml::parse_struct_unwrap<Manifest>(doc);
    } catch (const std::exception& e) {
        FAIL("manifest.yaml: " << e.what());
    }

    std::vector<ModelSpec> models;
    models.reserve(manifest.models.size());
    for (const ManifestEntry& entry : manifest.models) {
        ModelSpec spec;
        spec.id        = trim(entry.id);
        spec.category  = entry.category ? trim(*entry.category) : std::string{};
        spec.file_name = trim(entry.file);
        spec.folder    = folder;
        if (spec.category.empty()) spec.category = "uncategorized";

        // Catch2 has no && and no || inside an assertion, so one check per line.
        REQUIRE_FALSE(spec.id.empty());
        REQUIRE_FALSE(spec.file_name.empty());
        models.push_back(std::move(spec));
    }

    return models;
}

inline Slic3r::Domain::TriangleMesh load_model_file(const std::filesystem::path& path)
{
    Slic3r::Domain::TriangleMesh mesh;
    if (lower_extension(path) == ".stl") {
        auto loaded = Slic3r::Biz::load_stl(path.string());
        if (!loaded) throw std::runtime_error(loaded.error());
        mesh = std::move(*loaded);
    } else {
        auto loaded = Slic3r::Biz::load_obj(path.string());
        if (!loaded) throw std::runtime_error(loaded.error());
        mesh = std::move(*loaded);
    }

    if (mesh.empty()) throw std::runtime_error("the mesh is empty");
    return mesh;
}

/// The models are measured as loaded, so the only placement is onto the plate.
inline void centre_on_plate(Slic3r::Domain::TriangleMesh& mesh)
{
    const auto bb = mesh.bounding_box();
    const auto c  = Slic3r::Biz::Algorithms::BoundingBox::center(bb);
    mesh.translate(Slic3r::Domain::Vec3f(float(-c.x()), float(-c.y()), float(-bb.min.z())));
}

/// The error text of a loader goes into a report, so the file name and the folder are replaced by
/// the id: a loader that quotes the path in its message must not leak the corpus.
inline std::string describe_error(const std::string& message, const ModelSpec& spec)
{
    std::string text = message;
    for (const std::string& secret : {spec.folder, spec.file_name}) {
        if (secret.empty()) continue;
        for (size_t pos = text.find(secret); pos != std::string::npos;
             pos          = text.find(secret, pos + spec.id.size()))
            text.replace(pos, secret.size(), spec.id);
    }
    return text;
}

} // namespace Bench
