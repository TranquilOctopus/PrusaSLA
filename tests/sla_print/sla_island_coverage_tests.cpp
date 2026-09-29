#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <cstdlib>
#include <filesystem>

#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Format/STL.hpp"
#include "Slic3r/Biz/Format/OBJ.hpp"
#include "libslic3r/SLA/IslandDetection.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "libslic3r/MTUtils.hpp"
#include "sla_test_utils.hpp"
#include "test_utils.hpp"

using namespace Slic3r;
using namespace Slic3r::SLA;
using Domain::TriangleMesh;
using Domain::SLA::SupportPoints;
using Domain::SLA::SupportPoint;
namespace BB = Biz::Algorithms::BoundingBox;

static void test_mesh_island_coverage(const TriangleMesh& mesh, const std::string& label)
{
    REQUIRE_FALSE(mesh.empty());

    SupportPoints pts = calc_support_pts(mesh);

    auto bb = BB::cast<float>(mesh.bounding_box());
    float zmin = bb.min.z();
    float zmax = bb.max.z();
    float layer_h = 0.05f;

    std::vector<float> heights = grid(zmin, zmax, layer_h);
    std::vector<Domain::ExPolygons> layers = slice_mesh_ex(mesh.its, heights, 0.005f);

    std::vector<IslandHit> hits = detect_islands(layers, 0.1);

    size_t uncovered_count = 0;
    for (const auto& hit : hits) {
        float z_hit = zmin + layer_h * (static_cast<float>(hit.layer_index) + 0.5f);
        double max_xy_dist = std::sqrt(hit.area_mm2) + 1.0;

        bool covered = false;
        for (const auto& pt : pts) {
            double dz = std::abs(static_cast<double>(pt.pos.z()) - z_hit);
            if (dz > 0.5) continue;

            double dx = pt.pos.x() - hit.centroid.x();
            double dy = pt.pos.y() - hit.centroid.y();
            double xy_dist = std::sqrt(dx * dx + dy * dy);
            if (xy_dist <= max_xy_dist) {
                covered = true;
                break;
            }
        }

        INFO("Mesh: " << label
              << ", layer_index: " << hit.layer_index
              << ", z_hit: " << z_hit
              << ", centroid: (" << hit.centroid.x() << ", " << hit.centroid.y() << ")"
              << ", area_mm2: " << hit.area_mm2
              << ", max_xy_dist: " << max_xy_dist);
        CHECK(covered);
        if (!covered) {
            ++uncovered_count;
        }
    }

    std::cout << "[IslandCoverage] " << label
              << ": support_points=" << pts.size()
              << ", islands=" << hits.size()
              << ", uncovered=" << uncovered_count;
    for (const auto& hit : hits) {
        float z_hit = zmin + layer_h * (static_cast<float>(hit.layer_index) + 0.5f);
        double max_xy_dist = std::sqrt(hit.area_mm2) + 1.0;
        bool covered = false;
        for (const auto& pt : pts) {
            double dz = std::abs(static_cast<double>(pt.pos.z()) - z_hit);
            if (dz > 0.5) continue;
            double dx = pt.pos.x() - hit.centroid.x();
            double dy = pt.pos.y() - hit.centroid.y();
            double xy_dist = std::sqrt(dx * dx + dy * dy);
            if (xy_dist <= max_xy_dist) {
                covered = true;
                break;
            }
        }
        if (!covered) {
            std::cout << ", uncovered[layer=" << hit.layer_index
                      << ", z=" << z_hit
                      << ", centroid=(" << hit.centroid.x() << "," << hit.centroid.y() << ")"
                      << ", area_mm2=" << hit.area_mm2 << "]";
        }
    }
    std::cout << std::endl;
}

static void test_mesh_island_coverage_by_name(const std::string& mesh_name)
{
    TriangleMesh mesh = load_model(mesh_name);
    test_mesh_island_coverage(mesh, mesh_name);
}

TEST_CASE("Island coverage: V.obj", "[SLA][IslandCoverage]")
{
    test_mesh_island_coverage_by_name("V.obj");
}

TEST_CASE("Island coverage: A_upsidedown.obj", "[SLA][IslandCoverage]")
{
    test_mesh_island_coverage_by_name("A_upsidedown.obj");
}

TEST_CASE("Island coverage: frog_legs.obj", "[SLA][IslandCoverage]")
{
    test_mesh_island_coverage_by_name("frog_legs.obj");
}

// Hidden: runs > 4 min (mesh far from the origin); investigate the support point generator (M4.3).
TEST_CASE("Island coverage: overhang.obj", "[.][SLA][IslandCoverage][slow]")
{
    test_mesh_island_coverage_by_name("overhang.obj");
}

// Hidden, local only: SLA_COVERAGE_MODEL=<path to .stl/.obj>, e.g. a file in local-samples/.
TEST_CASE("Island coverage: local model", "[.][SLA][IslandCoverage][local]")
{
    const char* env_path = std::getenv("SLA_COVERAGE_MODEL");
    if (!env_path || env_path[0] == '\0') {
        SKIP("SLA_COVERAGE_MODEL not set");
    }
    std::filesystem::path model_path(env_path);
    if (!std::filesystem::exists(model_path)) {
        FAIL("SLA_COVERAGE_MODEL path does not exist: " << model_path.string());
    }

    TriangleMesh mesh;
    std::string ext = model_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".stl") {
        auto result = Biz::load_stl(model_path.string());
        REQUIRE(result);
        mesh = std::move(result.value());
    } else if (ext == ".obj") {
        auto result = Biz::load_obj(model_path.string());
        REQUIRE(result);
        mesh = std::move(result.value());
    } else {
        FAIL("Unsupported file extension: " << ext << " (only .stl and .obj supported)");
    }
    REQUIRE_FALSE(mesh.empty());

    // Move mesh so its bounding-box minimum z is 0 and its XY centre is at (0, 0)
    auto bb = mesh.bounding_box();
    Domain::Vec3f translation(-BB::center(bb).x(), -BB::center(bb).y(), -bb.min.z());
    mesh.translate(translation);

    test_mesh_island_coverage(mesh, model_path.filename().string());
}