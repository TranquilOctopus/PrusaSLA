#include "Slic3r/Biz/Algorithms/TriangleSelector.hpp"
#include "Slic3r/Biz/Format/3mf.hpp"
#include "Slic3r/Biz/Format/ResultLoad3mf.hpp"
#include "Slic3r/Biz/Slicing/TestUtils.hpp"
#include "Slic3r/Biz/Sla/DrainHoleSuggestion.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Percentage.hpp"
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
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

using json = nlohmann::ordered_json;

using namespace Slic3r;
using namespace Slic3r::Biz;

namespace fs = boost::filesystem;

using Slic3r::Biz::Sla::DrainHoleSuggestion;
using Slic3r::Domain::FacetsAnnotation;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelObjectPtrs;
using Slic3r::Domain::ModelVolume;
using Slic3r::Domain::Project;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::SLA::DrainHole;
using Slic3r::Domain::SLA::PointsStatus;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPointType;
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
    // The per-point "may this support end on the model" switch of M2.26 and the per-point bracing
    // switch of M2.38, on the point that asks for them. The points above keep the defaults, which is
    // what a project without the switches has.
    object->sla_support_points[3].on_model = SupportPoint::OnModel::Allow;
    object->sla_support_points[3].brace    = SupportPoint::Brace::Off;

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
    // No "om" and no "br" in the file, so the point follows the object, as every point of an older
    // project does.
    CHECK(loaded_object->sla_support_points[0].on_model == SupportPoint::OnModel::Inherit);
    CHECK(loaded_object->sla_support_points[0].brace == SupportPoint::Brace::Inherit);

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
    // Written as "t": 2 and parsed back as an unsigned number; the signed reader used to reject
    // that and leave manual_add. See value_from_json(json, number_integer_t&) in PrusaFile.cpp.
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
    CHECK(loaded_object->sla_support_points[3].on_model == SupportPoint::OnModel::Allow);
    CHECK(loaded_object->sla_support_points[3].brace == SupportPoint::Brace::Off);

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

namespace {

// A temporary directory with a project stored in it, for the tests that go into the archive to
// see what the writer wrote. The caller removes the directory with remove_stored_project().
struct StoredProject
{
    fs::path dir;
    fs::path file;
};

StoredProject store_project(const Project& project, const std::string& name)
{
    const fs::path dir = fs::temp_directory_path() / fs::unique_path("slic3r-3mf-sla-%%%%-%%%%");
    fs::create_directories(dir);
    const fs::path file = dir / name;
    store_3mf(file.string(), project);
    return {dir, file};
}

void remove_stored_project(const StoredProject& stored)
{
    boost::system::error_code cleanup_error;
    fs::remove_all(stored.dir, cleanup_error);
}

// Every entry of a 3mf archive by name, and the writing of one back. Used to look at what the
// writer wrote and to build a file with keys taken out, the way a build from before those keys
// existed would have written it.
std::map<std::string, std::string> read_zip_entries(const fs::path& file_path)
{
    mz_zip_archive archive{};
    mz_zip_zero_struct(&archive);
    REQUIRE(mz_zip_reader_init_file(&archive, file_path.string().c_str(), 0));

    std::map<std::string, std::string> entries;
    for (int i = 0; i < (int) mz_zip_reader_get_num_files(&archive); ++i) {
        mz_zip_archive_file_stat stat;
        REQUIRE(mz_zip_reader_file_stat(&archive, i, &stat));
        const std::string filename(stat.m_filename);
        const size_t uncomp_size = static_cast<size_t>(stat.m_uncomp_size);
        std::unique_ptr<char[]> buffer(new char[uncomp_size + 1]);
        REQUIRE(mz_zip_reader_extract_to_mem(&archive, i, buffer.get(), uncomp_size, 0) == MZ_TRUE);
        entries[filename] = std::string(buffer.get(), uncomp_size);
    }
    mz_zip_reader_end(&archive);
    return entries;
}

void write_zip_entries(const fs::path& file_path, const std::map<std::string, std::string>& entries)
{
    mz_zip_archive archive{};
    mz_zip_zero_struct(&archive);
    REQUIRE(mz_zip_writer_init_file(&archive, file_path.string().c_str(), 0));
    for (const auto& [filename, content] : entries) {
        REQUIRE(mz_zip_writer_add_mem(
            &archive,
            filename.c_str(),
            content.data(),
            content.size(),
            MZ_DEFAULT_COMPRESSION
        ));
    }
    REQUIRE(mz_zip_writer_finalize_archive(&archive));
    mz_zip_writer_end(&archive);
}

// The entry of the archive that holds the model data of an object, found by its content: the
// name of that entry is not fixed, and the first version of the test guessed
// "Metadata/Slic3r_project.json", which does not exist, so it failed before testing anything.
std::string find_sla_data_entry(const std::map<std::string, std::string>& entries)
{
    std::string entry_names;
    for (const auto& [name, content] : entries) {
        entry_names += name + "\n";
        if (content.find("slaSupportPoints") != std::string::npos)
            return name;
    }
    UNSCOPED_INFO("3MF entries:\n" << entry_names);
    return {};
}

// The json of the object that owns the support points, which sits at some depth in the entry.
json* find_sla_object_json(json& node)
{
    if (node.is_object()) {
        if (node.contains("slaSupportPoints"))
            return &node;
        for (auto& [key, child] : node.items()) {
            if (json* found = find_sla_object_json(child))
                return found;
        }
    } else if (node.is_array()) {
        for (auto& child : node) {
            if (json* found = find_sla_object_json(child))
                return found;
        }
    }
    return nullptr;
}

// Every field of a support point, compared one by one, so a round trip that loses any single one
// of them shows which. Every value a project can hold is a legal one: the point is compared with
// what was stored, not with a default.
void check_same_support_point(const SupportPoint& point, const SupportPoint& expected)
{
    INFO("support point at " << expected.pos.x() << ", " << expected.pos.y());
    CHECK(Domain::is_approx(point.pos.x(), expected.pos.x()));
    CHECK(Domain::is_approx(point.pos.y(), expected.pos.y()));
    CHECK(Domain::is_approx(point.pos.z(), expected.pos.z()));
    CHECK(Domain::is_approx(point.head_front_radius, expected.head_front_radius));
    CHECK(point.type == expected.type);
    CHECK(Domain::is_approx(point.pillar_diameter, expected.pillar_diameter));
    CHECK(Domain::is_approx(point.base_diameter, expected.base_diameter));
    CHECK(Domain::is_approx(point.base_height, expected.base_height));
    CHECK(point.base_shape == expected.base_shape);
    CHECK(point.tip_shape == expected.tip_shape);
    CHECK(Domain::is_approx(point.tip_length, expected.tip_length));
    CHECK(Domain::is_approx(point.contact_depth, expected.contact_depth));
    CHECK(point.stem_sides == expected.stem_sides);
    CHECK(Domain::is_approx(point.stem_taper, expected.stem_taper));
    CHECK(Domain::is_approx(point.knot_radius, expected.knot_radius));
    CHECK(point.on_model == expected.on_model);
    CHECK(point.brace == expected.brace);
    CHECK(point.role == expected.role);
    // The struct compares the whole point, which is what the support tree reads, so a difference
    // the field-by-field walk above cannot see (a field added later) still fails here.
    CHECK(point == expected);
}

// Every field of a drain hole. `failed` is not one of them: DrainHole::operator== leaves it out
// too, it is the result of a print and not part of the hole the user placed.
void check_same_drain_hole(const DrainHole& hole, const DrainHole& expected)
{
    INFO("drain hole at " << expected.pos.x() << ", " << expected.pos.y());
    CHECK(Domain::is_approx(hole.pos.x(), expected.pos.x()));
    CHECK(Domain::is_approx(hole.pos.y(), expected.pos.y()));
    CHECK(Domain::is_approx(hole.pos.z(), expected.pos.z()));
    CHECK(Domain::is_approx(hole.normal.x(), expected.normal.x()));
    CHECK(Domain::is_approx(hole.normal.y(), expected.normal.y()));
    CHECK(Domain::is_approx(hole.normal.z(), expected.normal.z()));
    CHECK(Domain::is_approx(hole.radius, expected.radius));
    CHECK(Domain::is_approx(hole.height, expected.height));
    CHECK(hole == expected);
}

// A project saved and read back, the shape every SLA round trip here needs.
Loaded3MF round_trip(const Project& project, const std::string& name)
{
    const StoredProject stored = store_project(project, name);
    const Loaded3MF loaded     = load_3mf(stored.file.string());
    remove_stored_project(stored);
    return loaded;
}

// Every issue the 3mf reader can report about the SLA data of an object, so a load of a file
// written by an older build can be checked for silence.
const std::vector<Read3mfIssueType>& sla_read_issue_types()
{
    static const std::vector<Read3mfIssueType> types{
        Read3mfIssueType::project_sla_support_points_must_be_array,
        Read3mfIssueType::project_sla_support_point_unknown_property,
        Read3mfIssueType::project_sla_support_point_position_issue,
        Read3mfIssueType::project_sla_support_point_radius_issue,
        Read3mfIssueType::project_sla_support_point_is_new_island_issue,
        Read3mfIssueType::project_sla_support_point_type_issue,
        Read3mfIssueType::project_sla_support_point_tip_length_issue,
        Read3mfIssueType::project_sla_support_point_contact_depth_issue,
        Read3mfIssueType::project_sla_support_point_tip_shape_issue,
        Read3mfIssueType::project_sla_support_point_base_shape_issue,
        Read3mfIssueType::project_sla_support_point_stem_sides_issue,
        Read3mfIssueType::project_sla_support_point_stem_taper_issue,
        Read3mfIssueType::project_sla_support_point_knot_radius_issue,
        Read3mfIssueType::project_sla_support_point_on_model_issue,
Read3mfIssueType::project_sla_support_point_brace_issue,
        Read3mfIssueType::project_sla_support_point_role_issue,
        Read3mfIssueType::project_sla_drain_holes_must_be_array,
        Read3mfIssueType::project_sla_drain_hole_unknown_property,
        Read3mfIssueType::project_sla_drain_hole_position_issue,
        Read3mfIssueType::project_sla_drain_hole_normal_issue,
        Read3mfIssueType::project_sla_drain_hole_radius_issue,
        Read3mfIssueType::project_sla_drain_hole_height_issue,
    };
    return types;
}

// A value of another kind than the one the item holds, so a round trip can tell an override that
// was written and read from one that was dropped. False for a value with no second state (an enum
// of one entry, a list), which then stays out of the round trip test.
template <typename T>
bool put_other_value(T& value)
{
    if constexpr (std::is_same_v<T, Domain::EnumWrapper>) {
        if (value.def().size() < 2)
            return false;
        const size_t next = (value.index_of_value(value.value()) + 1) % value.def().size();
        value.set_index(next);
        return true;
    } else if constexpr (std::is_same_v<T, bool>) {
        value = !value;
        return true;
    } else if constexpr (std::is_same_v<T, int>) {
        value += 1;
        return true;
    } else if constexpr (std::is_same_v<T, std::optional<int>>) {
        value = value.value_or(0) + 1;
        return true;
    } else if constexpr (std::is_same_v<T, double>) {
        value += 1.;
        return true;
    } else if constexpr (std::is_same_v<T, std::string>) {
        value += "_roundtrip";
        return true;
    } else if constexpr (std::is_same_v<T, Domain::Vec2d>) {
        value[0] += 1.;
        return true;
    } else if constexpr (std::is_same_v<T, Domain::Percentage>) {
        value.value += 1.;
        return true;
    } else if constexpr (std::is_same_v<T, Domain::FloatOrPercentage>) {
        if (value.is_percentage()) {
            value = Domain::FloatOrPercentage{Domain::Percentage{value.percentage().value + 1.}};
        } else {
            value = Domain::FloatOrPercentage{value.float_value() + 1.};
        }
        return true;
    } else {
        // A list, or a list of enums: no second value without knowing what the list is for.
        return false;
    }
}

} // namespace

TEST_CASE("3MF SLA round trip preserves every per-point support field", "[3mf][sla]")
{
    Project project;
    project.model()     = Test::generate_cubes(1, 1);
    ModelObject* object = project.model().objects[0];

    // Two points with every field set to something no default has, so nothing can pass on the
    // strength of a default. The second point differs from the first in every field again: a
    // reader that remembered the last value it read for a field would fail on one of the two. The
    // third point below carries the role of M7.8.3 on top of that.
    SupportPoint point{};
    point.pos               = Vec3f{11.5f, 12.25f, 13.75f};
    point.head_front_radius = 0.45f;
    point.type              = SupportPointType::slope;
    point.pillar_diameter   = 1.7f;
    point.base_diameter     = 3.9f;
    point.base_height       = 0.8f;
    point.base_shape        = SupportPoint::BaseShape::Flat;
    point.tip_shape         = SupportPoint::TipShape::Ball;
    point.tip_length        = 2.3f;
    point.contact_depth     = 0.35f;
    point.stem_sides        = 6;
    point.stem_taper        = 0.4f;
    point.knot_radius       = 0.9f;
    point.on_model          = SupportPoint::OnModel::Forbid;
point.brace             = SupportPoint::Brace::Off;
    point.role              = SupportPoint::Role::Fragile;

    SupportPoint other_point      = point;
    other_point.pos               = Vec3f{1.5f, 2.5f, 3.5f};
    other_point.head_front_radius = 0.2f;
    other_point.type              = SupportPointType::island;
    other_point.pillar_diameter   = 2.2f;
    other_point.base_diameter     = 1.1f;
    other_point.base_height       = 1.5f;
    other_point.base_shape        = SupportPoint::BaseShape::Cylinder;
    other_point.tip_shape         = SupportPoint::TipShape::Cone;
    other_point.tip_length        = 0.6f;
    other_point.contact_depth     = 0.15f;
    other_point.stem_sides        = 4;
    other_point.stem_taper        = 0.75f;
    other_point.knot_radius       = 0.3f;
    other_point.on_model          = SupportPoint::OnModel::Allow;
other_point.brace             = SupportPoint::Brace::On;
    other_point.role              = SupportPoint::Role::Anchor;

    // A third point whose role is the anchor of a very large object (M7.8.3), the role the name table
    // of the writer gained an entry for last. Every role of the enumeration is written through that
    // table, so a role without an entry in it is a read past the end of it rather than a name.
    SupportPoint large_point = point;
    large_point.pos          = Vec3f{31.5f, 32.5f, 33.5f};
    large_point.role         = SupportPoint::Role::AnchorLarge;

    object->sla_support_points = {point, other_point, large_point};

    const Loaded3MF loaded = round_trip(project, "sla_point_fields.3mf");
    REQUIRE(loaded.model.objects.size() == 1);
    const ModelObject* loaded_object = loaded.model.objects[0];
    REQUIRE(loaded_object->sla_support_points.size() == 3);
    check_same_support_point(loaded_object->sla_support_points[0], point);
    check_same_support_point(loaded_object->sla_support_points[1], other_point);
    check_same_support_point(loaded_object->sla_support_points[2], large_point);

    // A whole 3MF of these points loads without one word about the SLA data.
    for (const Read3mfIssueType type : sla_read_issue_types())
        CHECK_FALSE(loaded.issues_map.has_issue(type));
}

TEST_CASE("3MF SLA round trip preserves every per-object SLA override", "[3mf][sla]")
{
    Project project;
    project.model()     = Test::generate_cubes(1, 1);
    ModelObject* object = project.model().objects[0];

    // Every key the object may override, not a picked list of them: a key added to the object
    // settings after this test was written is then covered by it as well. Each key is given a
    // value of its own kind that is not its default, and the value is remembered for the compare.
    std::map<std::string, Domain::ConfigValue> written;
    std::vector<std::string> without_a_second_value;
    for (Domain::ConfigItem& item : object->object_settings_sla.overrides.all_items()) {
        const std::string key{item.name()};
        INFO("override " << key);
        if (!item.visit([](auto& value) { return put_other_value(value); })) {
            without_a_second_value.push_back(key);
            continue;
        }
        // set() with the value the item now holds also marks the override as used, which is what
        // makes the writer put it into the 3MF at all.
        const Domain::ConfigValue value{item.value()};
        object->object_settings_sla.overrides.set(key, value);
        written.emplace(key, value);
    }
    std::string not_tested;
    for (const std::string& key : without_a_second_value)
        not_tested += (not_tested.empty() ? "" : ", ") + key;
    UNSCOPED_INFO("no second value to give these overrides: " << not_tested);
    REQUIRE_FALSE(written.empty());

    const Loaded3MF loaded = round_trip(project, "sla_object_overrides.3mf");
    REQUIRE(loaded.model.objects.size() == 1);
    const ModelObject* loaded_object = loaded.model.objects[0];

    // Every override comes back with the value it was given, and still switched on for this
    // object, not only present in the box with its default.
    for (const auto& [key, value] : written) {
        INFO("override " << key);
        const Domain::ConfigItem* loaded_item =
            loaded_object->object_settings_sla.overrides.find(key);
        REQUIRE(loaded_item != nullptr);
        CHECK(loaded_object->object_settings_sla.overrides.get(key).has_value());
        CHECK(loaded_item->value() == value);
    }

    // The keys batch m15 added are named here, so a key that stops being per object (or a group of
    // them that is not written) fails here and not only as a silently shorter loop above.
    for (const std::string& key : {
             // The support presets and the tree type: M2.18 and M2.22.
             "support_tree_type",
             "supports_enable",
             "support_enforcers_only",
             "support_points_density_relative",
             "support_points_minimal_distance",
             "support_points_overhang_angle",
             // The bracing of M2.15.
             "support_brace_enable",
             "support_brace_diameter",
             "support_brace_start_height",
             // The raft of M2.14b1, M2.14b2, M2.14b3 and M2.25.
             "raft_type",
             "raft_edge_taper",
             "raft_infill",
             "raft_infill_spacing",
             "raft_infill_wall",
             "raft_infill_skin",
             "raft_floor_thickness",
             "raft_interface_thickness",
             "raft_interface_exposure",
             // The tip and stem of M2.16c and M2.24, the foot of M2.23.
             "support_tip_shape",
             "support_tip_length",
             "support_knot_diameter",
             "support_stem_sides",
             "support_stem_taper",
             "support_base_shape",
             "support_buildplate_only",
             "hollowing_enable",
         })
    {
        INFO("override " << key);
        const auto it = written.find(key);
        REQUIRE(it != written.end());
    }

    // The settings that cannot be per object stay out of it: the preset dimensions (Mini
    // included) and the vat film belong to the print and the printer preset and have no object
    // override of their own, so there is nothing to write for them. Should one of them become
    // overridable per object, it lands in the loop above and these are the checks to take away.
    CHECK(
        object->object_settings_sla.overrides.find("support_preset_mini_head_diameter") == nullptr
    );
    CHECK(
        object->object_settings_sla.overrides.find("support_preset_light_pillar_diameter")
        == nullptr
    );
    CHECK(object->object_settings_sla.overrides.find("vat_film_type") == nullptr);
}

TEST_CASE("3MF SLA round trip preserves a drain hole added by the suggestion path", "[3mf][sla]")
{
    Project project;
    project.model()     = Test::generate_cubes(1, 1);
    ModelObject* object = project.model().objects[0];

    // The M4.8f "Add drain hole" button does exactly this: it takes the suggestion of a cup or a
    // trapped resin pocket and appends the hole it converts to in sla_drain_holes, which is the
    // same storage as the drain hole tool of M2.5. A pocket is answered with a hole through its
    // floor, drilled downwards, a cup with one through its roof, drilled upwards, so the two
    // suggestions differ in every field.
    DrainHoleSuggestion pocket{};
    pocket.position_mm = Domain::Vec3d{7.5, 8.5, 0.};
    pocket.normal      = Domain::Vec3d{0., 0., -1.};
    pocket.radius_mm   = 4.25;
    pocket.height_mm   = 9.5;
    DrainHoleSuggestion cup{};
    cup.position_mm = Domain::Vec3d{2.5, 3.5, 20.};
    cup.normal      = Domain::Vec3d{0., 0., 1.};
    cup.radius_mm   = 5.;
    cup.height_mm   = 10.;

    object->sla_drain_holes.clear();
    object->sla_drain_holes.emplace_back(pocket.to_drain_hole());
    object->sla_drain_holes.emplace_back(cup.to_drain_hole());

    const Loaded3MF loaded = round_trip(project, "sla_drain_holes.3mf");
    REQUIRE(loaded.model.objects.size() == 1);
    const ModelObject* loaded_object = loaded.model.objects[0];
    REQUIRE(loaded_object->sla_drain_holes.size() == 2);
    check_same_drain_hole(loaded_object->sla_drain_holes[0], pocket.to_drain_hole());
    check_same_drain_hole(loaded_object->sla_drain_holes[1], cup.to_drain_hole());
    for (const Read3mfIssueType type : sla_read_issue_types())
        CHECK_FALSE(loaded.issues_map.has_issue(type));
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
    std::map<std::string, std::string> file_contents = read_zip_entries(file_path);

    // Find the archive entry that holds the object data by its content rather than guessing its
    // name: the first version of this test assumed "Metadata/Slic3r_project.json", which does not
    // exist, so it failed before testing anything.
    const std::string meta_filename = find_sla_data_entry(file_contents);
    REQUIRE_FALSE(meta_filename.empty());
    INFO("object data is in " << meta_filename);
    json meta_json = json::parse(file_contents[meta_filename]);

    // The object json can sit at any depth in that file; find the one owning the points.
    json* obj_json = find_sla_object_json(meta_json);
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
    write_zip_entries(file_path2, file_contents);

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

TEST_CASE(
    "3MF SLA round trip of a file written before the per-point keys of batch m15",
    "[3mf][sla]"
)
{
    Project project;
    project.model()     = Test::generate_cubes(1, 1);
    ModelObject* object = project.model().objects[0];

    // A point with everything on it, so the test can see that the keys batch m15 added are the
    // only thing missing from the file it builds below.
    SupportPoint point{};
    point.pos                  = Vec3f{4.5f, 6.5f, 8.5f};
    point.head_front_radius    = 0.3f;
    point.type                 = SupportPointType::island;
    point.pillar_diameter      = 1.3f;
    point.base_diameter        = 2.3f;
    point.base_height          = 0.6f;
    point.base_shape           = SupportPoint::BaseShape::Cylinder;
    point.tip_shape            = SupportPoint::TipShape::Cone;
    point.tip_length           = 1.4f;
    point.contact_depth        = 0.25f;
    point.stem_sides           = 8;
    point.stem_taper           = 0.55f;
    point.knot_radius          = 0.65f;
    point.on_model             = SupportPoint::OnModel::Allow;
    point.brace                = SupportPoint::Brace::Off;
    point.role                 = SupportPoint::Role::Island;
    object->sla_support_points = {point};

    object->sla_points_status = PointsStatus::UserModified;
    object->object_settings_sla.overrides.set("support_tip_length", 1.5);
    object->object_settings_sla.overrides.set("raft_infill_spacing", 2.5);

    const StoredProject stored                 = store_project(project, "sla_with_every_key.3mf");
    std::map<std::string, std::string> entries = read_zip_entries(stored.file);
    const std::string data_entry               = find_sla_data_entry(entries);
    REQUIRE_FALSE(data_entry.empty());
    json meta_json = json::parse(entries[data_entry]);
    json* obj_json = find_sla_object_json(meta_json);
    REQUIRE(obj_json != nullptr);
    REQUIRE((*obj_json)["slaSupportPoints"].size() == 1);

    // Take out the keys of the per-point fields batch m15 added, and with them the two that came
    // after it: "br" for the per-point bracing switch of M2.38 and "role" for the role of M7.8.2.
    // Those are the only keys of a support point that came later. The keys of the fields that were
    // already in the file (the position, the head radius, the island flag, the pillar and base
    // sizes and the type) stay, that is the whole point of the exercise.
    const json& written_point = (*obj_json)["slaSupportPoints"][0];
    INFO("written point: " << written_point.dump());
    for (const char* key : {"bs", "ts", "tl", "cd", "ss", "st", "kr", "om", "br", "role"}) {
        INFO("per point key " << key);
        CHECK(written_point.contains(key));
        (*obj_json)["slaSupportPoints"][0].erase(key);
    }

    entries[data_entry]       = Biz::beautify_json(meta_json, 2);
    const fs::path older_file = stored.dir / "sla_without_point_keys.3mf";
    write_zip_entries(older_file, entries);
    const Loaded3MF loaded = load_3mf(older_file.string());
    remove_stored_project(stored);

    REQUIRE(loaded.model.objects.size() == 1);
    const ModelObject* loaded_object = loaded.model.objects[0];
    REQUIRE(loaded_object->sla_support_points.size() == 1);
    const SupportPoint& loaded_point = loaded_object->sla_support_points[0];

    // The keys that were in the file are all still read.
    CHECK(Domain::is_approx(loaded_point.pos.x(), 4.5f));
    CHECK(Domain::is_approx(loaded_point.pos.y(), 6.5f));
    CHECK(Domain::is_approx(loaded_point.pos.z(), 8.5f));
    CHECK(Domain::is_approx(loaded_point.head_front_radius, 0.3f));
    CHECK(loaded_point.type == SupportPointType::island);
    CHECK(Domain::is_approx(loaded_point.pillar_diameter, 1.3f));
    CHECK(Domain::is_approx(loaded_point.base_diameter, 2.3f));
    CHECK(Domain::is_approx(loaded_point.base_height, 0.6f));

    // The keys that were taken out read as the defaults, which is what such a point did anyway.
    CHECK(loaded_point.base_shape == SupportPoint::BaseShape::Default);
    CHECK(loaded_point.tip_shape == SupportPoint::TipShape::Default);
    CHECK(Domain::is_approx(loaded_point.tip_length, 0.f));
    CHECK(Domain::is_approx(loaded_point.contact_depth, 0.f));
    CHECK(loaded_point.stem_sides == 0);
    CHECK(Domain::is_approx(loaded_point.stem_taper, 0.f));
    CHECK(Domain::is_approx(loaded_point.knot_radius, 0.f));
    CHECK(loaded_point.on_model == SupportPoint::OnModel::Inherit);
    // A project written before the bracing switch existed has no "br", so the point reads back as
    // Inherit, i.e. what such a point did anyway.
    CHECK(loaded_point.brace == SupportPoint::Brace::Inherit);
    // A project written before the roles existed has no "role", so the point reads back as the
    // Unknown one, which is the support it was built as anyway.
    CHECK(loaded_point.role == SupportPoint::Role::Unknown);

    // The rest of the object data of the file is untouched by the missing keys.
    CHECK(loaded_object->sla_points_status == PointsStatus::UserModified);
    const std::optional<Domain::ConfigItem> tip_length =
        loaded_object->object_settings_sla.overrides.get("support_tip_length");
    REQUIRE(tip_length.has_value());
    CHECK(Domain::is_approx(tip_length->get<double>(), 1.5));
    const std::optional<Domain::ConfigItem> infill_spacing =
        loaded_object->object_settings_sla.overrides.get("raft_infill_spacing");
    REQUIRE(infill_spacing.has_value());
    CHECK(Domain::is_approx(infill_spacing->get<double>(), 2.5));

    // A file without a key is not something to report: loading it is quiet.
    for (const Read3mfIssueType type : sla_read_issue_types())
        CHECK_FALSE(loaded.issues_map.has_issue(type));
}

TEST_CASE("3MF SLA round trip of a file with a key and a value it does not know", "[3mf][sla]")
{
    Project project;
    project.model()     = Test::generate_cubes(1, 1);
    ModelObject* object = project.model().objects[0];

    SupportPoint point{};
    point.pos                  = Vec3f{3.5f, 2.5f, 1.5f};
    point.head_front_radius    = 0.35f;
    point.tip_shape            = SupportPoint::TipShape::Ball;
    point.tip_length           = 1.25f;
    point.stem_sides           = 5;
    point.on_model             = SupportPoint::OnModel::Forbid;
    object->sla_support_points = {point};
    object->sla_drain_holes.push_back(
        DrainHole{Vec3f{1.f, 2.f, 3.f}, Vec3f{0.f, 0.f, -1.f}, 3.5f, 7.5f}
    );
    object->object_settings_sla.overrides.set("raft_edge_taper", 1.25);

    const StoredProject stored                 = store_project(project, "sla_with_future_keys.3mf");
    std::map<std::string, std::string> entries = read_zip_entries(stored.file);
    const std::string data_entry               = find_sla_data_entry(entries);
    REQUIRE_FALSE(data_entry.empty());
    json meta_json = json::parse(entries[data_entry]);
    json* obj_json = find_sla_object_json(meta_json);
    REQUIRE(obj_json != nullptr);
    REQUIRE((*obj_json)["slaSupportPoints"].size() == 1);
    REQUIRE((*obj_json)["slaDrainHoles"].size() == 1);
    REQUIRE((*obj_json).contains("objectSettingsSla"));

    // What a build from the future writes: keys on all three places an SLA key can sit, and a
    // point type this build has no name for. Loading has to go on anyway - an unknown key is a
    // thing to report, never a reason to drop the point, the hole or the override it sits on, and
    // a point type we cannot build is the manual point the file asked for.
    (*obj_json)["slaSupportPoints"][0]["tip_hollow"]       = 3.5;
    (*obj_json)["slaSupportPoints"][0]["zzFromTheFuture"]  = "what";
    (*obj_json)["slaSupportPoints"][0]["t"]                = 7;
    // A role of a build from the future, which is one this one has no name for: the point keeps
    // the support it had, i.e. the Unknown role, rather than a role nothing else agrees on.
    (*obj_json)["slaSupportPoints"][0]["role"]             = "rib_of_the_future";
    (*obj_json)["slaDrainHoles"][0]["zzFromTheFuture"]     = true;
    (*obj_json)["objectSettingsSla"]["zz_from_the_future"] = 7.;

    entries[data_entry]        = Biz::beautify_json(meta_json, 2);
    const fs::path future_file = stored.dir / "sla_with_future_keys_2.3mf";
    write_zip_entries(future_file, entries);
    const Loaded3MF loaded = load_3mf(future_file.string());
    remove_stored_project(stored);

    REQUIRE(loaded.model.objects.size() == 1);
    const ModelObject* loaded_object = loaded.model.objects[0];

    // The point is still there, with every field this build knows, and the point type it cannot
    // name reads as the manual point.
    REQUIRE(loaded_object->sla_support_points.size() == 1);
    check_same_support_point(loaded_object->sla_support_points[0], point);
    CHECK(loaded_object->sla_support_points[0].type == SupportPointType::manual_add);
    CHECK(loaded_object->sla_support_points[0].role == SupportPoint::Role::Unknown);

    // The hole as well.
    REQUIRE(loaded_object->sla_drain_holes.size() == 1);
    check_same_drain_hole(loaded_object->sla_drain_holes[0], object->sla_drain_holes[0]);

    // And the override, next to the unknown key in its box.
    REQUIRE(loaded_object->object_settings_sla.overrides.find("raft_edge_taper") != nullptr);
    CHECK(
        Domain::is_approx(
            loaded_object->object_settings_sla.overrides.get("raft_edge_taper")->get<double>(),
            1.25
        )
    );
}
