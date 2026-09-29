#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
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

static void test_mesh_island_coverage(const std::string& mesh_name)
{
    TriangleMesh mesh = load_model(mesh_name);
    REQUIRE_FALSE(mesh.empty());

    SupportPoints pts = calc_support_pts(mesh);

    auto bb = BB::cast<float>(mesh.bounding_box());
    float zmin = bb.min.z();
    float zmax = bb.max.z();
    float layer_h = 0.05f;

    std::vector<float> heights = grid(zmin, zmax, layer_h);
    std::vector<Domain::ExPolygons> layers = slice_mesh_ex(mesh.its, heights, 0.005f);

    std::vector<IslandHit> hits = detect_islands(layers, 0.1);

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

        INFO("Mesh: " << mesh_name
              << ", layer_index: " << hit.layer_index
              << ", z_hit: " << z_hit
              << ", centroid: (" << hit.centroid.x() << ", " << hit.centroid.y() << ")"
              << ", area_mm2: " << hit.area_mm2
              << ", max_xy_dist: " << max_xy_dist);
        CHECK(covered);
    }
}

TEST_CASE("Island coverage: V.obj", "[SLA][IslandCoverage]")
{
    test_mesh_island_coverage("V.obj");
}

TEST_CASE("Island coverage: A_upsidedown.obj", "[SLA][IslandCoverage]")
{
    test_mesh_island_coverage("A_upsidedown.obj");
}

TEST_CASE("Island coverage: frog_legs.obj", "[SLA][IslandCoverage]")
{
    test_mesh_island_coverage("frog_legs.obj");
}

TEST_CASE("Island coverage: overhang.obj", "[SLA][IslandCoverage]")
{
    test_mesh_island_coverage("overhang.obj");
}