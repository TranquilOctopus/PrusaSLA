#include "Slic3r/Biz/Algorithms/TriangleSelector.hpp"
#include "Slic3r/Biz/Format/3mf.hpp"
#include "Slic3r/Biz/Slicing/TestUtils.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/SLA/DrainHole.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Biz/Config/3mf_legacy.hpp"
#include "Slic3r/Biz/Config/ConfigLegacy.hpp"
#include "Slic3r/Biz/Algorithms/MiniZWrapper.hpp"
#include "Slic3r/Biz/Config/ConfigSerialize.hpp"

#include <boost/filesystem.hpp>
#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>

using json = nlohmann::ordered_json;

using namespace Slic3r;
using namespace Slic3r::Biz;

namespace fs = boost::filesystem;

using Slic3r::Domain::FacetsAnnotation;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelObjectPtrs;
using Slic3r::Domain::ModelVolume;
using Slic3r::Domain::Project;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPointType;
using Slic3r::Domain::SLA::PointsStatus;
using Slic3r::Domain::SLA::DrainHole;
using Slic3r::Domain::TriangleSelector::TriangleStateType;

static inline std::string test_3mf_path(const char* path)
{
    return std::string(TEST_DATA_DIR) + "/test_3mf/" + path;
}

TEST_CASE("3MF production extension - component without p:path resolves within same file", "[3mf]")
{
    // 3MF Production Extension allows geometry in sub-model files.
    // Per spec (Chapter 2): "Only a component element in the root model file MAY contain a path
    // attribute" and "Non-root model file components MUST only reference objects in the same
    // model file." So components in sub-models reference local objects via objectid alone,
    // without p:path. The parser must resolve such local references correctly.
    const Loaded3MF loaded         = load_3mf(test_3mf_path("production_ext.3mf"));
    const ModelObjectPtrs& objects = loaded.model.objects;

    // Must produce exactly 1 object (not 2 - no ghost empty object, no extra nonprintable object)
    REQUIRE(objects.size() == 1);

    // The single object must have geometry (1 volume of type MODEL_PART)
    REQUIRE(objects[0]->volumes.size() == 1);
    CHECK(objects[0]->volumes[0]->is_model_part());

    // The object must have 1 instance with the build item transform (translation 50,50,0)
    REQUIRE(objects[0]->instances.size() == 1);
    Vec3d offset = objects[0]->instances[0]->get_offset();
    CHECK(Domain::is_approx(offset.x(), 50.0));
    CHECK(Domain::is_approx(offset.y(), 50.0));

    // The mesh must have actual geometry (8 vertices, 12 triangles of a 10x10x10 cube)
    CHECK(objects[0]->volumes[0]->mesh().its.vertices.size() == 8);
    CHECK(objects[0]->volumes[0]->mesh().its.indices.size() == 12);
}

namespace {

constexpr size_t STATE_TYPE_NONE = static_cast<size_t>(TriangleStateType::NONE);

void paint_facets(
    ModelVolume& volume,
    FacetsAnnotation ModelVolume::* facets_member,
    const int painted_facets_count,
    const TriangleStateType state
)
{
    Algorithms::TriangleSelector selector{volume.mesh()};
    for (int facet_idx = 0; facet_idx < painted_facets_count; ++facet_idx) {
        selector.set_facet(facet_idx, state);
    }

    (volume.*facets_member).triangle_splitting_data = selector.serialize();
}

} // namespace

TEST_CASE("3MF round trip restores used_states of painted volumes - NONE", "[3mf]")
{
    Project project;
    project.model() = Test::generate_cubes(3, 3);

    ModelVolume* partially_painted_volume = project.model().objects[0]->volumes.front();
    ModelVolume* fully_painted_volume     = project.model().objects[1]->volumes.front();
    ModelVolume* seam_painted_volume      = project.model().objects[2]->volumes.front();

    const int facets_count = static_cast<int>(partially_painted_volume->mesh().facets_count());

    paint_facets(
        *partially_painted_volume,
        &ModelVolume::mm_segmentation_facets,
        facets_count - 1,
        TriangleStateType::Extruder2
    );

    paint_facets(
        *fully_painted_volume,
        &ModelVolume::mm_segmentation_facets,
        facets_count,
        TriangleStateType::Extruder2
    );

    paint_facets(*seam_painted_volume, &ModelVolume::seam_facets, 1, TriangleStateType::ENFORCER);

    const fs::path temp_dir =
        fs::temp_directory_path() / fs::unique_path("slic3r-3mf-test-%%%%-%%%%");
    fs::create_directories(temp_dir);
    const fs::path file_path = temp_dir / "painted_round_trip.3mf";
    store_3mf(file_path.string(), project);

    const Loaded3MF loaded = load_3mf(file_path.string());
    boost::system::error_code cleanup_error;
    fs::remove_all(temp_dir, cleanup_error);

    REQUIRE(loaded.model.objects.size() == 3);
    REQUIRE(loaded.model.objects[0]->volumes.size() == 1);
    REQUIRE(loaded.model.objects[1]->volumes.size() == 1);
    REQUIRE(loaded.model.objects[2]->volumes.size() == 1);

    const ModelVolume& loaded_partially_painted = *loaded.model.objects[0]->volumes.front();
    const ModelVolume& loaded_fully_painted     = *loaded.model.objects[1]->volumes.front();
    const ModelVolume& loaded_seam_painted      = *loaded.model.objects[2]->volumes.front();

    // Partially painted: one facet has no serialized record, so NONE must be marked as used.
    CHECK(loaded_partially_painted.mm_segmentation_facets.get_data().used_states[STATE_TYPE_NONE]);
    CHECK_FALSE(loaded_partially_painted.is_fully_mm_painted());

    // Fully painted: every facet has a serialized record, so NONE must stay unused.
    CHECK_FALSE(
        loaded_fully_painted.mm_segmentation_facets.get_data().used_states[STATE_TYPE_NONE]
    );
    CHECK(loaded_fully_painted.is_fully_mm_painted());

    CHECK_FALSE(loaded_seam_painted.seam_facets.empty());
    CHECK(loaded_seam_painted.seam_facets.get_data().used_states[STATE_TYPE_NONE]);
}

namespace {

// Loads a 3mf written by the current (core-spec) writer using the legacy 2.x importer, which
// only understands MM segmentation stored inline as slic3rpe:mmu_segmentation <triangle>
// attributes - never Metadata/Slic3r_facets_annotation.json.
Domain::Model load_with_legacy_importer(const fs::path& file_path)
{
    Domain::Model legacy_model;
    Domain::ConfigPack cfg;
    Biz::LegacyPresetMetadata preset_metadata;
    boost::optional<Slic3r::Semver> generator_version;
    Domain::WipeTowersOnBeds wipe_towers;
    Domain::CustomGCodesOnBeds custom_gcodes;
    Biz::VirtualExtrudersConfig virtual_extruders_config;

    bool loaded_ok = Slic3rLegacy::load_3mf_legacy(
        file_path.string().c_str(),
        cfg,
        preset_metadata,
        &legacy_model,
        true,
        generator_version,
        wipe_towers,
        custom_gcodes,
        virtual_extruders_config
    );
    REQUIRE(loaded_ok);
    return legacy_model;
}

} // namespace

TEST_CASE("3MF dual-write: version-1 MM segmentation is readable by the legacy 2.x importer", "[3mf]")
{
    Project project;
    project.model() = Test::generate_cubes(1, 1);

    ModelVolume* volume = project.model().objects[0]->volumes.front();
    const int facets_count = static_cast<int>(volume->mesh().facets_count());
    // Extruder16 (value 16) is the highest state that still fits version 1 encoding.
    paint_facets(*volume, &ModelVolume::mm_segmentation_facets, facets_count, TriangleStateType::Extruder16);

    const fs::path temp_dir = fs::temp_directory_path() / fs::unique_path("slic3r-3mf-dualwrite-v1-%%%%-%%%%");
    fs::create_directories(temp_dir);
    const fs::path file_path = temp_dir / "dual_write_v1.3mf";
    store_3mf(file_path.string(), project);

    const Domain::Model legacy_model = load_with_legacy_importer(file_path);

    boost::system::error_code cleanup_error;
    fs::remove_all(temp_dir, cleanup_error);

    REQUIRE(legacy_model.objects.size() == 1);
    REQUIRE(legacy_model.objects[0]->volumes.size() == 1);

    const ModelVolume& legacy_volume = *legacy_model.objects[0]->volumes.front();
    REQUIRE_FALSE(legacy_volume.mm_segmentation_facets.empty());
    for (int i = 0; i < facets_count; ++i) {
        CHECK(
            legacy_volume.mm_segmentation_facets.get_triangle_as_string(i) ==
            volume->mm_segmentation_facets.get_triangle_as_string(i)
        );
    }
}

TEST_CASE("3MF dual-write: version-2 MM segmentation is not dual-written for the legacy importer", "[3mf]")
{
    Project project;
    project.model() = Test::generate_cubes(1, 1);

    ModelVolume* volume = project.model().objects[0]->volumes.front();
    const int facets_count = static_cast<int>(volume->mesh().facets_count());
    // Any state beyond the named Extruder1..16 enumerators (17-255) forces version 2 encoding,
    // which the legacy inline attribute can't represent - a 2.x reader can't decode it anyway.
    paint_facets(*volume, &ModelVolume::mm_segmentation_facets, facets_count, static_cast<TriangleStateType>(20));

    const fs::path temp_dir = fs::temp_directory_path() / fs::unique_path("slic3r-3mf-dualwrite-v2-%%%%-%%%%");
    fs::create_directories(temp_dir);
    const fs::path file_path = temp_dir / "dual_write_v2.3mf";
    store_3mf(file_path.string(), project);

    const Domain::Model legacy_model = load_with_legacy_importer(file_path);

    boost::system::error_code cleanup_error;
    fs::remove_all(temp_dir, cleanup_error);

    REQUIRE(legacy_model.objects.size() == 1);
    REQUIRE(legacy_model.objects[0]->volumes.size() == 1);

    // A 2.x reader only ever sees MM painting via the inline attribute, so when it's skipped
    // (version 2 data can't be dual-written) the legacy importer must find no MM painting at all.
    CHECK(legacy_model.objects[0]->volumes.front()->mm_segmentation_facets.empty());
}

TEST_CASE("3MF SLA round trip preserves support points and drain holes", "[3mf][sla]")
{
    // Default project works for SLA data round-trip (model object data is stored separately from config)
    Project project;
    project.model() = Test::generate_cubes(1, 1);

    ModelObject* object = project.model().objects[0];

    // (a) SLA support points: positions, head radius, type
    object->sla_support_points.clear();
    object->sla_support_points.push_back(SupportPoint{
        Vec3f{10.0f, 10.0f, 5.0f},  // pos
        1.5f,                        // head_front_radius
        SupportPointType::manual_add // type
    });
    object->sla_support_points.push_back(SupportPoint{
        Vec3f{15.0f, 15.0f, 8.0f},
        2.0f, SupportPointType::island
    });
    object->sla_support_points.push_back(SupportPoint{
        Vec3f{5.0f, 5.0f, 12.0f},
        1.0f, SupportPointType::slope
    });
    // Point with per-point overrides
    object->sla_support_points.push_back(SupportPoint{
        Vec3f{20.0f, 20.0f, 10.0f},
        1.2f,
        SupportPointType::manual_add,
        1.8f,  // pillar_diameter override
        3.5f,  // base_diameter override
        1.2f   // base_height override
    });

    // sla_points_status is a separate field NOT serialized in 3MF (gap)
    object->sla_points_status = PointsStatus::UserModified;

    // (b) SLA drain holes: position, normal, radius, height
    object->sla_drain_holes.clear();
    object->sla_drain_holes.push_back(DrainHole{
        Vec3f{12.0f, 12.0f, 0.0f},   // pos
        Vec3f{0.0f, 0.0f, 1.0f},     // normal
        2.5f,                         // radius
        5.0f                          // height
    });
    object->sla_drain_holes.push_back(DrainHole{
        Vec3f{8.0f, 8.0f, 0.0f},
        Vec3f{0.0f, 0.0f, 1.0f},
        1.5f,
        3.0f
    });

    // (c) Per-object SLA settings in object_settings_sla
    // NOTE: object_settings_sla is NOT currently written by PrusaFile.cpp (gap)
    object->object_settings_sla.overrides.set("support_points_density_relative", 150);
    object->object_settings_sla.overrides.set("hollowing_enable", true);
    object->object_settings_sla.overrides.set("hollowing_min_thickness", 2.0);

    const fs::path temp_dir =
        fs::temp_directory_path() / fs::unique_path("slic3r-3mf-sla-test-%%%%-%%%%");
    fs::create_directories(temp_dir);
    const fs::path file_path = temp_dir / "sla_round_trip.3mf";
    store_3mf(file_path.string(), project);

    const Loaded3MF loaded = load_3mf(file_path.string());
    boost::system::error_code cleanup_error;
    fs::remove_all(temp_dir, cleanup_error);

    REQUIRE(loaded.model.objects.size() == 1);
    const ModelObject* loaded_object = loaded.model.objects[0];

    // ---- Support points round-trip ----
    REQUIRE(loaded_object->sla_support_points.size() == 4);
    CHECK(Domain::is_approx(loaded_object->sla_support_points[0].pos.x(), 10.0f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[0].pos.y(), 10.0f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[0].pos.z(), 5.0f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[0].head_front_radius, 1.5f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[0].pillar_diameter, 0.f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[0].base_diameter, 0.f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[0].base_height, 0.f));
    CHECK(loaded_object->sla_support_points[0].type == SupportPointType::manual_add);

    CHECK(Domain::is_approx(loaded_object->sla_support_points[1].pos.x(), 15.0f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[1].pos.y(), 15.0f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[1].pos.z(), 8.0f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[1].head_front_radius, 2.0f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[1].pillar_diameter, 0.f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[1].base_diameter, 0.f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[1].base_height, 0.f));
    CHECK(loaded_object->sla_support_points[1].type == SupportPointType::island);

    CHECK(Domain::is_approx(loaded_object->sla_support_points[2].pos.x(), 5.0f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[2].pos.y(), 5.0f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[2].pos.z(), 12.0f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[2].head_front_radius, 1.0f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[2].pillar_diameter, 0.f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[2].base_diameter, 0.f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[2].base_height, 0.f));
    // type round-trips correctly (was a gap, now fixed)
    CHECK(loaded_object->sla_support_points[2].type == SupportPointType::slope);

    // Point with per-point overrides
    CHECK(Domain::is_approx(loaded_object->sla_support_points[3].pos.x(), 20.0f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[3].pos.y(), 20.0f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[3].pos.z(), 10.0f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[3].head_front_radius, 1.2f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[3].pillar_diameter, 1.8f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[3].base_diameter, 3.5f));
    CHECK(Domain::is_approx(loaded_object->sla_support_points[3].base_height, 1.2f));
    CHECK(loaded_object->sla_support_points[3].type == SupportPointType::manual_add);

    // sla_points_status round-trips (was a gap, now fixed)
    CHECK(loaded_object->sla_points_status == PointsStatus::UserModified);

    // ---- Drain holes round-trip ----
    REQUIRE(loaded_object->sla_drain_holes.size() == 2);
    CHECK(Domain::is_approx(loaded_object->sla_drain_holes[0].pos.x(), 12.0f));
    CHECK(Domain::is_approx(loaded_object->sla_drain_holes[0].pos.y(), 12.0f));
    CHECK(Domain::is_approx(loaded_object->sla_drain_holes[0].pos.z(), 0.0f));
    CHECK(Domain::is_approx(loaded_object->sla_drain_holes[0].normal.x(), 0.0f));
    CHECK(Domain::is_approx(loaded_object->sla_drain_holes[0].normal.y(), 0.0f));
    CHECK(Domain::is_approx(loaded_object->sla_drain_holes[0].normal.z(), 1.0f));
    CHECK(Domain::is_approx(loaded_object->sla_drain_holes[0].radius, 2.5f));
    CHECK(Domain::is_approx(loaded_object->sla_drain_holes[0].height, 5.0f));

    CHECK(Domain::is_approx(loaded_object->sla_drain_holes[1].pos.x(), 8.0f));
    CHECK(Domain::is_approx(loaded_object->sla_drain_holes[1].pos.y(), 8.0f));
    CHECK(Domain::is_approx(loaded_object->sla_drain_holes[1].pos.z(), 0.0f));
    CHECK(Domain::is_approx(loaded_object->sla_drain_holes[1].radius, 1.5f));
    CHECK(Domain::is_approx(loaded_object->sla_drain_holes[1].height, 3.0f));

    // object_settings_sla round-trips (was a gap, now fixed)
    REQUIRE_FALSE(loaded_object->object_settings_sla.overrides.empty());
    CHECK(loaded_object->object_settings_sla.overrides.get("support_points_density_relative")->get<int>() == 150);
    CHECK(loaded_object->object_settings_sla.overrides.get("hollowing_enable")->get<bool>() == true);
    CHECK(Domain::is_approx(loaded_object->object_settings_sla.overrides.get("hollowing_min_thickness")->get<double>(), 2.0));
}

TEST_CASE("3MF SLA round trip with missing optional keys uses defaults", "[3mf][sla]")
{
    // This test simulates loading a 3MF written by an older build that doesn't have
    // slaPointsStatus, objectSettingsSla, or support point TYPE keys.
    // The defaults must survive: PointsStatus::NoPoints, empty overrides, SupportPointType::manual_add.

    Project project;
    project.model() = Test::generate_cubes(1, 1);

    ModelObject* object = project.model().objects[0];

    // Add a support point with type slope (will be written with TYPE key)
    object->sla_support_points.clear();
    object->sla_support_points.push_back(SupportPoint{
        Vec3f{10.0f, 10.0f, 5.0f},
        1.5f,
        SupportPointType::slope
    });

    // Set non-default status and overrides
    object->sla_points_status = PointsStatus::UserModified;
    object->object_settings_sla.overrides.set("support_points_density_relative", 150);

    const fs::path temp_dir =
        fs::temp_directory_path() / fs::unique_path("slic3r-3mf-sla-missing-keys-%%%%-%%%%");
    fs::create_directories(temp_dir);
    const fs::path file_path = temp_dir / "sla_original.3mf";
    store_3mf(file_path.string(), project);

    // Extract all files from the original 3MF
    mz_zip_archive archive{};
    mz_zip_zero_struct(&archive);
    REQUIRE(mz_zip_reader_init_file(&archive, file_path.string().c_str(), 0));

    // Map to hold all file contents: filename -> content
    std::map<std::string, std::string> file_contents;

    for (int i = 0; i < (int)mz_zip_reader_get_num_files(&archive); ++i) {
        mz_zip_archive_file_stat stat;
        REQUIRE(mz_zip_reader_file_stat(&archive, i, &stat));
        std::string filename(stat.m_filename);

        size_t uncomp_size = static_cast<size_t>(stat.m_uncomp_size);
        std::unique_ptr<char[]> buffer(new char[uncomp_size + 1]);
        REQUIRE(mz_zip_reader_extract_to_mem(&archive, i, buffer.get(), uncomp_size, 0) == MZ_TRUE);
        buffer[uncomp_size] = '\0';
        file_contents[filename] = std::string(buffer.get(), uncomp_size);
    }
    mz_zip_reader_end(&archive);

    // Find the archive entry that holds the object data by its content rather than guessing its
    // name: the first version of this test assumed "Metadata/Slic3r_project.json", which does not
    // exist, so it failed before testing anything.
    std::string meta_filename;
    std::string entry_names;
    for (const auto& [name, content] : file_contents) {
        entry_names += name + "\n";
        if (meta_filename.empty() && content.find("slaSupportPoints") != std::string::npos)
            meta_filename = name;
    }
    INFO("3MF entries:\n" << entry_names);
    REQUIRE_FALSE(meta_filename.empty());
    INFO("object data is in " << meta_filename);
    json meta_json = json::parse(file_contents[meta_filename]);

    // The object json can sit at any depth in that file; find the one owning the points.
    std::function<json*(json&)> find_object = [&](json& node) -> json* {
        if (node.is_object()) {
            if (node.contains("slaSupportPoints"))
                return &node;
            for (auto& [key, child] : node.items())
                if (json* found = find_object(child))
                    return found;
        } else if (node.is_array()) {
            for (auto& child : node)
                if (json* found = find_object(child))
                    return found;
        }
        return nullptr;
    };
    json* obj_json = find_object(meta_json);
    REQUIRE(obj_json != nullptr);
    REQUIRE((*obj_json)["slaSupportPoints"].is_array());
    REQUIRE((*obj_json)["slaSupportPoints"].size() == 1);

    // Before stripping, check the writer really wrote the type. The round-trip test above sees a
    // slope point come back as manual_add; this tells writer and reader apart.
    const json& written_point = (*obj_json)["slaSupportPoints"][0];
    INFO("written point: " << written_point.dump());
    CHECK(written_point.contains("t"));
    if (written_point.contains("t"))
        CHECK(written_point["t"].get<json::number_integer_t>() ==
              static_cast<json::number_integer_t>(SupportPointType::slope));
    CHECK(obj_json->contains("slaPointsStatus"));
    CHECK(obj_json->contains("objectSettingsSla"));

    // Now simulate a file from an older writer by removing all three new keys.
    obj_json->erase("slaPointsStatus");
    obj_json->erase("objectSettingsSla");
    for (auto& pt_json : (*obj_json)["slaSupportPoints"]) {
        // The key is "t", not "type": see SlaSupportPointsSerialization::TYPE in PrusaFile.cpp.
        pt_json.erase("t");
    }

    file_contents[meta_filename] = Biz::beautify_json(meta_json, 2);

    // Create a new 3MF with the modified JSON
    const fs::path file_path2 = temp_dir / "sla_missing_keys.3mf";
    mz_zip_archive archive2{};
    mz_zip_zero_struct(&archive2);
    REQUIRE(mz_zip_writer_init_file(&archive2, file_path2.string().c_str(), 0));

    for (const auto& [filename, content] : file_contents) {
        REQUIRE(mz_zip_writer_add_mem(&archive2, filename.c_str(),
            content.data(), content.size(), MZ_DEFAULT_COMPRESSION));
    }

    REQUIRE(mz_zip_writer_finalize_archive(&archive2));
    mz_zip_writer_end(&archive2);

    // Now load the modified file
    const Loaded3MF loaded = load_3mf(file_path2.string());

    boost::system::error_code cleanup_error;
    fs::remove_all(temp_dir, cleanup_error);

    REQUIRE(loaded.model.objects.size() == 1);
    const ModelObject* loaded_object = loaded.model.objects[0];

    // Support point should default to manual_add when TYPE key is missing
    REQUIRE(loaded_object->sla_support_points.size() == 1);
    CHECK(loaded_object->sla_support_points[0].type == SupportPointType::manual_add);

    // sla_points_status should default to NoPoints
    CHECK(loaded_object->sla_points_status == PointsStatus::NoPoints);

    // object_settings_sla should default to empty overrides
    CHECK(loaded_object->object_settings_sla.overrides.empty());
}
