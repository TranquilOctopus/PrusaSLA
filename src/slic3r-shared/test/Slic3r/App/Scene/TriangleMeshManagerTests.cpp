#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Slic3r/App/Scene/TriangleMeshManager.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"

#include <memory>
#include <type_traits>

using Catch::Approx;
using Slic3r::AABBMesh;
using Slic3r::App::Scene::TriangleMesh;
using Slic3r::Domain::TriangleMesh as DomainMesh;
using Slic3r::Domain::Vec3d;

namespace TriMesh = Slic3r::Biz::Algorithms::TriangleMesh;

// M0.15: the manager mesh holds an AABBMesh, and that is a view on the indexed_triangle_set of the
// very object it lives in rather than a copy of it: it keeps the address it was built from and
// reads the vertices and the indices of that address on every query. A copy of the object would
// leave the view pointing into the source (which the copy does not keep alive) and a move of it
// would leave the view pointing at the vectors the move has just taken away, so both are deleted
// and the manager keeps its meshes in place. These are the compile-time halves of that; the
// runtime halves below are what the view is used for.

TEST_CASE("Scene::TriangleMesh - a manager mesh cannot be copied or moved away from its acceleration structure",
          "[Scene][TriangleMeshManager]")
{
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<TriangleMesh>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<TriangleMesh>);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<TriangleMesh>);
    STATIC_REQUIRE_FALSE(std::is_move_assignable_v<TriangleMesh>);

    // The object itself stays usable in place, which is the only way the manager holds one.
    STATIC_REQUIRE(std::is_default_constructible_v<TriangleMesh>);
}

TEST_CASE("Scene::TriangleMesh - the acceleration structure answers on the mesh the manager holds",
          "[Scene][TriangleMeshManager]")
{
    SECTION("A mesh of its own is queried through its own vertices")
    {
        TriangleMesh mesh{TriMesh::its_make_cube(10., 20., 30.)};
        const AABBMesh& aabb = mesh.aabb_mesh();

        // A ray straight down the middle of the cube meets its top face at z = 30 and its bottom
        // face at z = 0, so both hits have to be found on the mesh this object owns.
        const AABBMesh::hit_result top = aabb.query_ray_hit(Vec3d{5., 10., 100.}, -Vec3d::UnitZ());
        REQUIRE(top.is_hit());
        CHECK(top.position().z() == Approx(30.));

        const AABBMesh::hit_result bottom = aabb.query_ray_hit(Vec3d{5., 10., -100.}, Vec3d::UnitZ());
        REQUIRE(bottom.is_hit());
        CHECK(bottom.position().z() == Approx(0.));
    }

    SECTION("A shared mesh stays alive through the shared pointer the manager took")
    {
        // The manager shares the mesh of a volume with the model, so the acceleration structure it
        // builds over it may not outlive the manager entry but the mesh itself outlives both.
        std::shared_ptr<const DomainMesh> shared;
        AABBMesh::hit_result                first;

        {
            shared = std::make_shared<const DomainMesh>(TriMesh::its_make_cube(10., 10., 10.));
            TriangleMesh mesh{shared};
            first = mesh.aabb_mesh().query_ray_hit(Vec3d{5., 5., 50.}, -Vec3d::UnitZ());
            REQUIRE(first.is_hit());
        }

        // The manager entry is gone with its scope; the mesh is not, and the hit that was taken
        // while both were alive still names a point of it.
        REQUIRE(shared != nullptr);
        CHECK(shared->facets_count() > 0u);
        CHECK(first.position().z() == Approx(10.));
    }
}