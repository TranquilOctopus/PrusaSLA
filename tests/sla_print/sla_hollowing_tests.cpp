// Hollowing wall thickness (M4.7a, PLAN B5). Hollowing offsets the model surface inwards by
// hollowing_min_thickness and meshes that offset surface back, so the wall that is left is as thick
// as the setting asks for. Nothing checks that, and the offset lives in a voxel distance field, so
// the measurement has to allow for the sampling. Each generated model is hollowed with several wall
// thicknesses and hollowing_quality / hollowing_closing_distance settings, points are sampled on
// the inner surface and the distance to the outer model is measured for each of them. The observed
// min / mean / max are reported for every case so a regression shows how far it moved, not only
// that a bound was crossed.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include <Slic3r/Biz/Algorithms/AABBMesh.hpp>
#include <Slic3r/Biz/Algorithms/TriangleMesh.hpp>
#include <Slic3r/Domain/TriangleMesh.hpp>
#include <Slic3r/Domain/Types.hpp>
#include <Slic3r/Utils.hpp>

#include <libslic3r/CSGMesh/CSGMesh.hpp>
#include <libslic3r/SLA/Hollowing.hpp>

using namespace Slic3r;

namespace TriMesh = Slic3r::Biz::Algorithms::TriangleMesh;

namespace {

// How many points of the inner surface are measured per case. The interior mesh of a 20mm model has
// tens of thousands of triangles at the resolutions used here; more samples than this do not tell
// anything more about the wall, they only make the test slower.
constexpr size_t MAX_SAMPLES = 20000;

struct HollowParams
{
    double thickness = 2.;
    double quality   = 0.5;
    double closing   = 0.5;
};

// A model to hollow: the positive CSG parts it is made of and the merged outer surface to measure
// against. The CSG parts only point into the meshes, so the meshes are kept next to them.
struct Model
{
    std::string name;
    std::vector<indexed_triangle_set> meshes;
    std::vector<csg::CSGPart> parts;
    indexed_triangle_set outer;
};

Model make_model(std::string name, std::vector<indexed_triangle_set> meshes)
{
    Model model;
    model.name   = std::move(name);
    model.meshes = std::move(meshes);

    for (indexed_triangle_set& mesh : model.meshes) {
        // The voxelizer tells inside from outside by the winding, so the parts have to be closed
        // solids with their normals pointing outwards. The primitives below are, this only makes
        // the test independent of how their faces happen to be ordered.
        if (Domain::its_volume(mesh) < 0.f)
            sla::swap_normals(mesh);

        REQUIRE(Domain::its_volume(mesh) > 0.f);
        model.parts.emplace_back(&mesh, csg::CSGType::Union);
    }

    model.outer = csg::csgmesh_merge_positive_parts(model.parts);
    REQUIRE(!model.outer.indices.empty());

    return model;
}

indexed_triangle_set translated(indexed_triangle_set mesh, Domain::Vec3f offset)
{
    for (Domain::Vec3f& v : mesh.vertices)
        v += offset;

    return mesh;
}

// A 20mm sphere sitting on the plate. Nothing but a closed surface, so the measured wall is the
// requested thickness everywhere.
Model sphere_model()
{
    // The second argument of its_make_sphere is the wanted edge length relative to the radius.
    return make_model(
        "sphere r10mm",
        {translated(TriMesh::its_make_sphere(10., 0.1), {10.f, 10.f, 10.f})}
    );
}

// A 20mm cube. The offset surface of a box is a smaller box, so its corners are where the closing
// distance rounds the interior.
Model cube_model()
{
    return make_model("cube 20mm", {TriMesh::its_make_cube(20., 20., 20.)});
}

// A 24mm diameter, 8mm tall cylinder with a 10mm diameter, 10mm tall neck on top of it. The neck is
// the thinnest feature of the model: a 3mm wall still leaves a 4mm wide cavity in it, but only just,
// so this is where a wall that came out too thin shows up first.
Model necked_cylinder_model()
{
    constexpr double step = 2. * std::numbers::pi / 48.;
    return make_model(
        "cylinder with a thin neck",
        {TriMesh::its_make_cylinder(12., 8., step),
         translated(TriMesh::its_make_cylinder(5., 10., step), {0.f, 0.f, 8.f})}
    );
}

// An L shaped block: a 20x20x10mm base with a 20x8x20mm arm standing on it. The reentrant corner
// where the two meet is a sharp inner corner and the cavity around it is where the closing distance
// rounds the interior most.
Model sharp_inner_corner_model()
{
    return make_model(
        "L block with a sharp inner corner",
        {TriMesh::its_make_cube(20., 20., 10.),
         translated(TriMesh::its_make_cube(20., 8., 20.), {0.f, 0.f, 10.f})}
    );
}

std::vector<Model> all_models()
{
    std::vector<Model> models;
    models.reserve(4);
    models.push_back(sphere_model());
    models.push_back(cube_model());
    models.push_back(necked_cylinder_model());
    models.push_back(sharp_inner_corner_model());
    return models;
}

// The wall thickness sweep. The closing distance is kept small in these cases: it rounds the
// interior with a ball of that radius, and what is measured here is the wall thickness itself.
const std::vector<HollowParams>& thickness_sweep()
{
    static const std::vector<HollowParams> cases = {
        {1., 0.5, 0.3},
        {2., 0.5, 0.5},
        {3., 0.5, 0.5},
    };
    return cases;
}

// The accuracy sweep. hollowing_quality 0 is the coarsest grid the config allows and 1 the finest,
// and a closing distance of 0 keeps the interior as close to the exterior shape as it can get.
const std::vector<HollowParams>& accuracy_sweep()
{
    static const std::vector<HollowParams> cases = {
        {2., 0., 0.},
        {2., 1., 0.5},
    };
    return cases;
}

struct WallStats
{
    double min     = 0.;
    double mean    = 0.;
    double max     = 0.;
    size_t samples = 0;
};

// The distance from points on the inner surface to the outer surface of the model. A face centroid
// is sampled rather than a vertex: it belongs to exactly one triangle, so no part of the surface is
// weighted by how many triangles happen to meet at a vertex, and a centroid always lies on the face
// it belongs to.
WallStats measure_wall(const indexed_triangle_set& interior, const AABBMesh& outer)
{
    WallStats stats;
    stats.min = std::numeric_limits<double>::infinity();

    const size_t stride = std::max<size_t>(1, interior.indices.size() / MAX_SAMPLES);

    double sum = 0.;

    for (size_t i = 0; i < interior.indices.size(); i += stride) {
        const Domain::Index3& face = interior.indices[i];
        const Domain::Vec3d p =
            (interior.vertices[size_t(face[0])].cast<double>()
             + interior.vertices[size_t(face[1])].cast<double>()
             + interior.vertices[size_t(face[2])].cast<double>())
            / 3.;

        const double d = std::sqrt(outer.squared_distance(p));

        stats.min = std::min(stats.min, d);
        stats.max = std::max(stats.max, d);
        sum += d;
        ++stats.samples;
    }

    if (stats.samples > 0)
        stats.mean = sum / double(stats.samples);

    return stats;
}

void check_wall(const Model& model, const HollowParams& params)
{
    sla::HollowingConfig cfg;
    cfg.min_thickness    = params.thickness;
    cfg.quality          = params.quality;
    cfg.closing_distance = params.closing;

    // The engine derives the grid resolution from the model volume, the wall thickness and the
    // quality, so the tolerance has to be derived from the voxel size of this very case: a voxel is
    // 1 / voxel_scale mm wide and the inner surface is a marching cubes extraction of that grid, so
    // it cannot be closer to the true offset surface than a fraction of a voxel. The closing
    // distance rounds the interior with a ball of that radius and at a convex corner of the model
    // that ball reaches (sqrt(2) - 1) * closing_distance past the requested wall, which is why the
    // closing distance is the second term. Both are one sided: the interior may sit a little too far
    // in, which is what the bound below allows for.
    const double voxel_mm =
        1. / sla::get_voxel_scale(sla::csgmesh_positive_maxvolume(model.parts), cfg);
    const double tolerance = params.closing + 2. * voxel_mm;

    sla::InteriorPtr interior = sla::generate_interior(range(model.parts), cfg);
    REQUIRE(interior);

    const indexed_triangle_set& inner = sla::get_mesh(*interior);
    REQUIRE(!inner.indices.empty());

    const AABBMesh outer(model.outer);
    const WallStats stats = measure_wall(inner, outer);
    REQUIRE(stats.samples > 0);

    INFO(
        "model "
        << model.name
        << ", wall "
        << params.thickness
        << "mm, quality "
        << params.quality
        << ", closing distance "
        << params.closing
        << "mm: wall min "
        << stats.min
        << "mm, mean "
        << stats.mean
        << "mm, max "
        << stats.max
        << "mm over "
        << stats.samples
        << " samples, voxel "
        << voxel_mm
        << "mm, tolerance "
        << tolerance
        << "mm"
    );

    const double required = params.thickness - tolerance;
    CHECK(stats.min >= required);
}

} // namespace

TEST_CASE("Hollowing: the wall is at least the requested thickness", "[SLA][Hollowing]")
{
    for (const Model& model : all_models()) {
        for (const HollowParams& params : thickness_sweep())
            check_wall(model, params);
    }
}

TEST_CASE("Hollowing: the wall survives the accuracy extremes", "[SLA][Hollowing]")
{
    // The smooth and the sharp cornered model at the coarsest and at the finest grid. The first of
    // these cases is where the voxel size the tolerance is derived from is the largest.
    std::vector<Model> models;
    models.push_back(sphere_model());
    models.push_back(sharp_inner_corner_model());

    for (const Model& model : models) {
        for (const HollowParams& params : accuracy_sweep())
            check_wall(model, params);
    }
}
