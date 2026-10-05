#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libslic3r/SLA/CavityDetection.hpp"
#include "libslic3r/SLA/Rotfinder.hpp"
#include "libslic3r/SLAAutoOrient.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Model.hpp"

#include <Eigen/Geometry>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <utility>
#include <vector>

using Catch::Approx;

namespace {

// A model mesh is in millimetres, the coordinate system a model keeps its mesh in: it is the mesh
// the importers hand over, and the engine cuts its slicing planes at that Z. The polygons the
// slicer hands back are the other way round, in the scaled coordinates of a layer, so an area of
// those is multiplied by this squared to get mm².
constexpr double sf = Slic3r::Biz::Algorithms::Scaling::SCALING_FACTOR;

// A model with one object holding a box of the given size, standing on the plate. The box can be
// tilted in the mesh itself, because that is what the rotation optimizer works on.
struct BoxModel
{
    Slic3r::Domain::Model model;
    Slic3r::Domain::ModelObject* object{nullptr};

    BoxModel(double x, double y, double z, double tilt_about_x_rad = 0.)
    {
        Slic3r::Domain::TriangleMesh mesh =
            Slic3r::Biz::Algorithms::TriangleMesh::make_cube(x, y, z);
        if (tilt_about_x_rad != 0.) {
            mesh.transform(Slic3r::Transform3d{
                Eigen::AngleAxisd{tilt_about_x_rad, Slic3r::Vec3d::UnitX()}});
        }

        object = model.add_object();
        Slic3r::Biz::Algorithms::ModelObject::add_volume(object, std::move(mesh));
        object->add_instance();
    }
};

// The rotation as a transform. The engine hands out a float transform, but the mesh vertices are
// cast to double below, so the transform is widened here: Eigen does not mix scalar types.
Slic3r::Transform3d rotation_transform(const Slic3r::Vec2d& rotation)
{
    Slic3r::Transform3d t = Slic3r::Transform3d::Identity();
    t.linear() = Slic3r::sla::rotation_angles_to_transform(rotation).linear().cast<double>();
    return t;
}

// Height of the object's mesh after rotating it by the given X/Y angles, in mm.
double height_after_rotation(const Slic3r::Domain::ModelObject& object, const Slic3r::Vec2d& rotation)
{
    const Slic3r::Transform3d trafo = rotation_transform(rotation);
    double min_z = std::numeric_limits<double>::max();
    double max_z = std::numeric_limits<double>::lowest();
    for (const auto& volume : object.volumes) {
        for (const auto& vertex : volume->mesh().its.vertices) {
            const double z = (trafo * vertex.cast<double>()).z();
            min_z = std::min(min_z, z);
            max_z = std::max(max_z, z);
        }
    }
    return max_z - min_z;
}

struct Faces
{
    // Area of the biggest face of the mesh, in mm².
    double largest{0.};
    // Area of the biggest face lying flat on the build plate, in mm².
    double largest_horizontal{0.};
    // Normal of the biggest face of the mesh.
    Slic3r::Vec3d largest_normal{Slic3r::Vec3d::Zero()};
};

// The faces of the object's mesh after rotating it by the given X/Y angles. The vertices are in
// millimetres, so the areas are mm² the tests compare against.
Faces faces_after_rotation(const Slic3r::Domain::ModelObject& object, const Slic3r::Vec2d& rotation)
{
    const Slic3r::Transform3d trafo = rotation_transform(rotation);
    Faces faces;
    for (const auto& volume : object.volumes) {
        const indexed_triangle_set& its{volume->mesh().its};
        for (const auto& face : its.indices) {
            const Slic3r::Vec3d p0{trafo * its.vertices[face[0]].cast<double>()};
            const Slic3r::Vec3d p1{trafo * its.vertices[face[1]].cast<double>()};
            const Slic3r::Vec3d p2{trafo * its.vertices[face[2]].cast<double>()};
            const Slic3r::Vec3d cross{(p1 - p0).cross(p2 - p0)};
            const double area = 0.5 * cross.norm();
            if (area > faces.largest) {
                faces.largest        = area;
                faces.largest_normal = cross.normalized();
            }
            if (std::abs(cross.normalized().z()) > 1. - 1e-3 && area > faces.largest_horizontal) {
                faces.largest_horizontal = area;
            }
        }
    }
    return faces;
}

// A model with one object holding the mesh it is given, in millimetres, which is the coordinate
// system a model keeps its mesh in: the goals that slice the mesh (least peel, no cups) cut their
// planes at that Z and hand the mesh to the slicer in it, so a mesh in scaled coordinates would be
// measured a million times too big.
struct MeshModel
{
    Slic3r::Domain::Model model;
    Slic3r::Domain::ModelObject* object{nullptr};

    explicit MeshModel(Slic3r::Domain::TriangleMesh mesh)
    {
        object = model.add_object();
        Slic3r::Biz::Algorithms::ModelObject::add_volume(object, std::move(mesh));
        object->add_instance();
    }
};

// The mesh of a square cup standing on the plate: a floor and four walls around an empty pocket
// that is open at the top (+Z). It is built from boxes because the pocket then has to be the space
// between closed solids rather than a contour that has to be wound the right way, and every layer
// between the floor and the top is the ring between the outer square and the pocket, which is a
// hole of the layer: the cup of M4.8e, once the model is turned upside down.
Slic3r::Domain::TriangleMesh make_cup(double outer, double height, double wall, double floor_mm)
{
    const double half = outer / 2.;
    const double inner = half - wall;

    indexed_triangle_set its;

    // make_cube() stands the box on the origin, so it is the lower corner that goes on
    // (x0, y0, z0) and not the middle of its footprint: with the middle in x and y the floor
    // ends up beside the two walls meant to stand on it and the mesh has no pocket at all.
    const auto add_box = [&its](double x0, double x1, double y0, double y1, double z0, double z1) {
        Slic3r::Domain::TriangleMesh box{
            Slic3r::Biz::Algorithms::TriangleMesh::make_cube(x1 - x0, y1 - y0, z1 - z0)};
        box.translate(Slic3r::Vec3f{float(x0), float(y0), float(z0)});
        Slic3r::Domain::its_merge(its, box.its);
    };

    add_box(-half, half, -half, half, 0., floor_mm); // the floor of the pocket
    add_box(-half, half, -half, -inner, 0., height); // the wall towards -Y
    add_box(-half, half, inner, half, 0., height); // the wall towards +Y
    add_box(-half, -inner, -half, half, 0., height); // the wall towards -X
    add_box(inner, half, -half, half, 0., height); // the wall towards +X

    return Slic3r::Domain::TriangleMesh{std::move(its)};
}

// The coarse cross sections of a pose, measured here and not by the code under test: the mesh of the
// object after the given rotation is sliced a millimetre at a time, and the biggest of the slices
// and the cups on them are counted. Those are the two terms the least peel and no cups goals are
// scored on.
struct CoarseSlices
{
    double peak_area_mm2{0.};
    double cup_opening_mm2{0.};
    size_t cup_count{0};
};

CoarseSlices coarse_slices_after_rotation(
    const Slic3r::Domain::ModelObject& object,
    const Slic3r::Vec2d& rotation,
    double step_mm = 1.
)
{
    CoarseSlices result;
    const Slic3r::Domain::TriangleMesh mesh = Slic3r::sla::auto_orient_mesh(object);
    if (mesh.its.vertices.empty())
        return result;

    const Slic3r::Transform3f trafo = Slic3r::sla::rotation_angles_to_transform(rotation);

    float zmin = std::numeric_limits<float>::max();
    float zmax = std::numeric_limits<float>::lowest();
    for (const Slic3r::Vec3f& vertex : mesh.its.vertices) {
        const float z = (trafo * vertex).z();
        zmin = std::min(zmin, z);
        zmax = std::max(zmax, z);
    }
    const float height = zmax - zmin;
    if (!(height > 0.f))
        return result;

    // The planes sit between the extremes, as they do in the engine: a plane on a horizontal face
    // of the mesh does not cut it. The Z of the mesh is in millimetres, and so is the step of the
    // planes: the slicer cuts at the Z of the mesh and scales the mesh up into the coordinates of
    // a layer itself, so a millimetre here is a millimetre of the pose.
    std::vector<float> zs;
    for (double z = double(zmin) + step_mm / 2.; z < double(zmax); z += step_mm)
        zs.push_back(float(z));
    if (zs.empty())
        return result;

    Slic3r::MeshSlicingParamsEx params;
    params.trafo = Slic3r::Domain::Transform3d{trafo.cast<double>()};
    const std::vector<Slic3r::Domain::ExPolygons> layers =
        Slic3r::slice_mesh_ex(mesh.its, zs, params);
    if (layers.empty())
        return result;

    for (const Slic3r::Domain::ExPolygons& layer : layers) {
        double area_mm2 = 0.;
        for (const Slic3r::Domain::ExPolygon& region : layer)
            area_mm2 += std::abs(region.area()) * sf * sf;
        result.peak_area_mm2 = std::max(result.peak_area_mm2, area_mm2);
    }

    const std::vector<float> thicknesses(layers.size(), float(step_mm));
    const Slic3r::SLA::CavityAnalysis cavities =
        Slic3r::SLA::detect_cavities(layers, thicknesses);
    result.cup_count = cavities.cups.size();
    for (const Slic3r::SLA::CupHit& cup : cavities.cups)
        result.cup_opening_mm2 += cup.opening_area_mm2;

    return result;
}

// ------------------------------------------------------------------------------------------------
// The shapes of the miniature goal, written out here rather than taken from a fixture file.

// The angle between two directions, in degrees.
double angle_between_degrees(const Slic3r::Vec3d& one, const Slic3r::Vec3d& other)
{
    const double cos_angle = one.normalized().dot(other.normalized());
    return std::acos(std::clamp(cos_angle, -1., 1.)) * 180. / std::numbers::pi;
}

// The smallest angle between the normals of two facets of the mesh that share an edge, in degrees.
// This is what says whether a mesh has a flat face in it at all: the facets of one flat face
// differ by the noise of the tessellation and not by anything like this, while the facets of a
// curved surface differ by the grid they are cut on.
double smallest_shared_edge_normal_angle_deg(const Slic3r::Domain::TriangleMesh& mesh)
{
    double smallest = 180.;
    std::map<std::pair<size_t, size_t>, Slic3r::Vec3d> normal_of_edge;

    for (size_t fi = 0; fi < mesh.its.indices.size(); ++fi) {
        const auto& face = mesh.its.indices[fi];
        const Slic3r::Vec3d p0{mesh.its.vertices[face[0]].cast<double>()};
        const Slic3r::Vec3d p1{mesh.its.vertices[face[1]].cast<double>()};
        const Slic3r::Vec3d p2{mesh.its.vertices[face[2]].cast<double>()};
        const Slic3r::Vec3d normal = (p1 - p0).cross(p2 - p0).normalized();

        for (int edge = 0; edge < 3; ++edge) {
            const size_t a = std::min(face[edge], face[(edge + 1) % 3]);
            const size_t b = std::max(face[edge], face[(edge + 1) % 3]);
            const auto it = normal_of_edge.find({a, b});
            if (it == normal_of_edge.end()) {
                normal_of_edge.emplace(std::make_pair(a, b), normal);
            } else {
                smallest = std::min(smallest, angle_between_degrees(normal, it->second));
            }
        }
    }

    return smallest;
}

// A ball of the given radius, written out of triangles: an icosahedron with every edge split
// `subdivisions` times and the corners pushed out onto the sphere.
//
// Not a latitude/longitude ball, which would be the obvious thing to write: the quads between two
// rings of it are planar (their corners are mirrored into each other, so they are two parallel
// lines in one plane) and the two triangles of every quad are therefore exactly coplanar. A goal
// that looks for flat faces would find one in every quad of such a ball, and the "no flat face at
// all" case could not be written down. An icosphere has no two facets that share an edge in one
// plane, which is also what a piece that came out of a scanner looks like.
indexed_triangle_set make_sphere(double radius, size_t subdivisions)
{
    const double golden = (1. + std::sqrt(5.)) / 2.;
    std::vector<Slic3r::Vec3f> vertices{
        {-1.f, float(golden), 0.f},
        {1.f, float(golden), 0.f},
        {-1.f, float(-golden), 0.f},
        {1.f, float(-golden), 0.f},
        {0.f, -1.f, float(golden)},
        {0.f, 1.f, float(golden)},
        {0.f, -1.f, float(-golden)},
        {0.f, 1.f, float(-golden)},
        {float(golden), 0.f, -1.f},
        {float(golden), 0.f, 1.f},
        {float(-golden), 0.f, -1.f},
        {float(-golden), 0.f, 1.f}
    };
    std::vector<Slic3r::Domain::Index3> faces{
        {0, 11, 5},  {0, 5, 1},  {0, 1, 7},  {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
        {11, 10, 2}, {10, 7, 6}, {7, 1, 8},  {3, 9, 4},  {3, 4, 2},   {3, 2, 6}, {3, 6, 8},
        {3, 8, 9},   {4, 9, 5},  {2, 4, 11}, {6, 2, 10}, {8, 6, 7},   {9, 8, 1}
    };

    for (size_t pass = 0; pass < subdivisions; ++pass) {
        // The corner in the middle of every edge, kept once per edge.
        std::map<std::pair<size_t, size_t>, size_t> middle;
        const auto between = [&vertices, &middle](size_t one, size_t other)
        {
            const auto key{std::make_pair(std::min(one, other), std::max(one, other))};
            const auto it = middle.find(key);
            if (it != middle.end())
                return it->second;
            // Out of the two corners it is put in, not off them: the corner is a value of its own
            // before the vector of the corners is grown under it.
            const Slic3r::Vec3f half{vertices[one] + vertices[other]};
            vertices.push_back(half / 2.f);
            const size_t index{vertices.size() - 1};
            middle.emplace(key, index);
            return index;
        };

        std::vector<Slic3r::Domain::Index3> split;
        split.reserve(4 * faces.size());
        for (const Slic3r::Domain::Index3& face : faces) {
            const size_t a{size_t(face[0])}, b{size_t(face[1])}, c{size_t(face[2])};
            const size_t ab{between(a, b)};
            const size_t bc{between(b, c)};
            const size_t ca{between(c, a)};
            split.push_back({int(a), int(ab), int(ca)});
            split.push_back({int(b), int(bc), int(ab)});
            split.push_back({int(c), int(ca), int(bc)});
            split.push_back({int(ab), int(bc), int(ca)});
        }
        faces = std::move(split);
    }

    indexed_triangle_set its;
    its.vertices.reserve(vertices.size());
    for (const Slic3r::Vec3f& vertex : vertices)
        its.vertices.push_back(vertex.normalized() * float(radius));
    its.indices = std::move(faces);

    return its;
}

// A closed cylinder standing on the z = 0 plane with its axis on the origin, with a cap at both
// ends: the bottom one wound so that it looks down, the top one so that it looks up.
indexed_triangle_set make_cylinder(double radius, double height, size_t segments)
{
    indexed_triangle_set its;
    its.vertices.reserve(2 * segments + 2);
    its.indices.reserve(4 * segments);

    for (size_t seg = 0; seg < segments; ++seg) {
        const double phi = 2 * std::numbers::pi * double(seg) / double(segments);
        its.vertices.emplace_back(float(radius * std::cos(phi)), float(radius * std::sin(phi)), 0.f);
        its.vertices.emplace_back(
            float(radius * std::cos(phi)), float(radius * std::sin(phi)), float(height));
    }
    its.vertices.emplace_back(0.f, 0.f, 0.f);            // the centre of the bottom cap
    its.vertices.emplace_back(0.f, 0.f, float(height));  // the centre of the top cap

    const int bottom_centre = int(2 * segments), top_centre = int(2 * segments + 1);
    const auto rim = [segments](int seg, bool top) { return 2 * (seg % int(segments)) + (top ? 1 : 0); };

    for (int seg = 0; seg < int(segments); ++seg) {
        its.indices.push_back({rim(seg, false), rim(seg + 1, false), rim(seg + 1, true)});
        its.indices.push_back({rim(seg, false), rim(seg + 1, true), rim(seg, true)});
        its.indices.push_back({bottom_centre, rim(seg + 1, false), rim(seg, false)});
        its.indices.push_back({top_centre, rim(seg, true), rim(seg + 1, true)});
    }

    return its;
}

// A head on a neck: a ball for the head and a short cylinder for the neck under it, with the
// bottom of the head inside the neck so that the two read as one piece. The flat cut at the bottom
// of the neck is the only flat face of the mesh at all, so it is the face the miniature goal lays
// on the plate.
Slic3r::Domain::TriangleMesh make_head_on_neck(
    double head_radius, double neck_radius, double neck_height
)
{
    // The head sits that far above the cut, so that its bottom is inside the neck.
    Slic3r::Domain::TriangleMesh head{std::move(make_sphere(head_radius, 1))};
    head.translate(Slic3r::Vec3f{0.f, 0.f, float(head_radius + 2.)});
    Slic3r::Domain::TriangleMesh neck{std::move(make_cylinder(neck_radius, neck_height, 24))};

    indexed_triangle_set its;
    Slic3r::Domain::its_merge(its, head.its);
    Slic3r::Domain::its_merge(its, neck.its);

    return Slic3r::Domain::TriangleMesh{std::move(its)};
}

// A bust: a box shaped body with one small flat face at its lower end, the cut it is glued to a
// body by, and a wide flat top, which is the face of the piece a support could not be hidden in.
// Its four sides are deliberately not flat: the top is a wide rectangle turned against the small
// one at the bottom, so every side is a warped quad and no two of its triangles lie in a plane
// together.
Slic3r::Domain::TriangleMesh make_bust(double cut_x, double cut_y, double top_x, double top_y,
                                       double height, double twist_deg)
{
    const double twist = twist_deg * std::numbers::pi / 180.;
    const auto turn = [twist](double x, double y, double z) {
        return Slic3r::Vec3f{float(x * std::cos(twist) - y * std::sin(twist) + 1.5),
                             float(x * std::sin(twist) + y * std::cos(twist) + 0.5),
                             float(z)};
    };

    indexed_triangle_set its;
    const Slic3r::Vec3f bottom[4] = {{float(-cut_x / 2.), float(-cut_y / 2.), 0.f},
                                     {float(cut_x / 2.), float(-cut_y / 2.), 0.f},
                                     {float(cut_x / 2.), float(cut_y / 2.), 0.f},
                                     {float(-cut_x / 2.), float(cut_y / 2.), 0.f}};
    for (const Slic3r::Vec3f& corner : bottom)
        its.vertices.push_back(corner);
    for (const Slic3r::Vec3f& corner : {turn(-top_x / 2., -top_y / 2., height),
                                         turn(top_x / 2., -top_y / 2., height),
                                         turn(top_x / 2., top_y / 2., height),
                                         turn(-top_x / 2., top_y / 2., height)})
        its.vertices.push_back(corner);

    const auto bottom_corner = [](int i) { return i % 4; };
    const auto top_corner    = [](int i) { return 4 + i % 4; };

    // The cut at the bottom, wound so that it looks down.
    its.indices.push_back({bottom_corner(0), bottom_corner(3), bottom_corner(2)});
    its.indices.push_back({bottom_corner(0), bottom_corner(2), bottom_corner(1)});
    // The top of the bust, looking up.
    its.indices.push_back({top_corner(0), top_corner(1), top_corner(2)});
    its.indices.push_back({top_corner(0), top_corner(2), top_corner(3)});
    // The four sides.
    for (int side = 0; side < 4; ++side) {
        its.indices.push_back({bottom_corner(side), bottom_corner(side + 1), top_corner(side + 1)});
        its.indices.push_back({bottom_corner(side), top_corner(side + 1), top_corner(side)});
    }

    return Slic3r::Domain::TriangleMesh{std::move(its)};
}

} // namespace

TEST_CASE("Rotfinder: minimum-height rotation lays a tall box down", "[SLA][Rotfinder]")
{
    BoxModel box{10., 10., 40.};
    REQUIRE(height_after_rotation(*box.object, Slic3r::Vec2d::Zero()) > 39.);

    const Slic3r::Vec2d rotation =
        Slic3r::sla::find_min_z_height_rotation(*box.object, Slic3r::sla::RotOptimizeParams{});

    REQUIRE(std::isfinite(rotation.x()));
    REQUIRE(std::isfinite(rotation.y()));
    // Lying down, the box is 10 mm tall instead of 40 mm.
    CHECK(height_after_rotation(*box.object, rotation) < 11.);
}

TEST_CASE("Auto orient (public API) lays a tall box down", "[SLA][Rotfinder]")
{
    BoxModel box{10., 10., 40.};
    const Slic3r::Vec2d rotation = Slic3r::sla::auto_orient_min_height(*box.object);
    CHECK(height_after_rotation(*box.object, rotation) < 11.);
}

TEST_CASE("Rotfinder: misalignment rotation returns finite angles", "[SLA][Rotfinder]")
{
    BoxModel box{10., 20., 5.};

    const Slic3r::Vec2d rotation = Slic3r::sla::find_best_misalignment_rotation(
        *box.object, Slic3r::sla::RotOptimizeParams{}.accuracy(0.1f)
    );

    CHECK(std::isfinite(rotation.x()));
    CHECK(std::isfinite(rotation.y()));
}

TEST_CASE("Auto orient: fewest supports lays a tilted box on its largest face", "[SLA][Rotfinder]")
{
    // A 40 x 10 x 10 mm box tilted 30 degrees in the mesh. The mesh is cut into triangles, so its
    // biggest faces are the 200 mm2 halves of the two 400 mm2 sides.
    BoxModel box{40., 10., 10., 30. * std::numbers::pi / 180.};
    const double half_largest_face_area = 40. * 10. / 2.;

    const Faces tilted = faces_after_rotation(*box.object, Slic3r::Vec2d::Zero());
    REQUIRE(tilted.largest == Approx(half_largest_face_area));
    REQUIRE(std::abs(tilted.largest_normal.z()) < 0.9); // the biggest face is not horizontal yet
    // No face of the tilted box is flat.
    REQUIRE(tilted.largest_horizontal == 0.);

    const Slic3r::Vec2d rotation =
        Slic3r::sla::auto_orient(*box.object, Slic3r::sla::AutoOrientGoal::LeastSupports);

    REQUIRE(std::isfinite(rotation.x()));
    REQUIRE(std::isfinite(rotation.y()));

    // The biggest face is the one the box can rest on the plate with, without any support.
    const Faces laid_down = faces_after_rotation(*box.object, rotation);
    CHECK(laid_down.largest == Approx(half_largest_face_area));
    CHECK(laid_down.largest_horizontal == Approx(laid_down.largest));
}

TEST_CASE("Auto orient: an object with nothing to rotate does not throw", "[SLA][Rotfinder]")
{
    Slic3r::Domain::Model model;
    Slic3r::Domain::ModelObject* object{model.add_object()};

    CHECK_NOTHROW(Slic3r::sla::auto_orient(*object, Slic3r::sla::AutoOrientGoal::LeastSupports));
    CHECK_NOTHROW(Slic3r::sla::auto_orient(*object, Slic3r::sla::AutoOrientGoal::MinHeight));
    CHECK_NOTHROW(Slic3r::sla::auto_orient(*object, Slic3r::sla::AutoOrientGoal::LeastPeel));
    CHECK_NOTHROW(Slic3r::sla::auto_orient(*object, Slic3r::sla::AutoOrientGoal::NoCups));
    CHECK_NOTHROW(Slic3r::sla::auto_orient(*object, Slic3r::sla::AutoOrientGoal::Miniature));

    // The mesh of an object with no geometry is empty, and an empty mesh has nothing to rotate.
    CHECK(Slic3r::sla::auto_orient_mesh(*object).its.vertices.empty());
    const Slic3r::Vec2d rotation =
        Slic3r::sla::auto_orient(Slic3r::Domain::TriangleMesh{}, Slic3r::sla::AutoOrientGoal::NoCups);
    CHECK(rotation.x() == 0.);
    CHECK(rotation.y() == 0.);
}

TEST_CASE("Auto orient: least peel stands a flat plate on its edge", "[SLA][Rotfinder]")
{
    // A 60 x 40 x 2 mm plate. Lying down it is the shortest print: 2 mm tall and 2400 mm2 of cured
    // area in every one of its layers. Standing on its 40 x 2 mm edge it is 60 mm tall and 80 mm2
    // in every layer, which is the peak peel force the print has to get through.
    MeshModel plate{Slic3r::Biz::Algorithms::TriangleMesh::make_cube(60., 40., 2.)};

    const Slic3r::Vec2d lowest =
        Slic3r::sla::auto_orient(*plate.object, Slic3r::sla::AutoOrientGoal::MinHeight);
    const CoarseSlices lowest_slices = coarse_slices_after_rotation(*plate.object, lowest);

    REQUIRE(height_after_rotation(*plate.object, lowest) < 3.);
    REQUIRE(lowest_slices.peak_area_mm2 == Approx(60. * 40.).margin(1.));

    const Slic3r::Vec2d easiest =
        Slic3r::sla::auto_orient(*plate.object, Slic3r::sla::AutoOrientGoal::LeastPeel);
    REQUIRE(std::isfinite(easiest.x()));
    REQUIRE(std::isfinite(easiest.y()));

    const CoarseSlices easiest_slices = coarse_slices_after_rotation(*plate.object, easiest);
    // On its edge the plate is 2 mm wide, so the biggest layer is an order of magnitude smaller
    // than the plate lying down, and the object is much taller than it was.
    CHECK(easiest_slices.peak_area_mm2 < lowest_slices.peak_area_mm2 / 10.);
    CHECK(height_after_rotation(*plate.object, easiest) > 10.);

    // The smallest cross section a 60 x 40 x 2 mm plate can have is its 40 x 2 mm edge, so this is
    // the pose that was found, not one that is merely smaller than the flat one.
    CHECK(easiest_slices.peak_area_mm2 == Approx(40. * 2.).margin(1.));

    // The search does not depend on how the poses came out of the parallel loop, so a second run
    // finds the same rotation.
    const Slic3r::Vec2d again =
        Slic3r::sla::auto_orient(*plate.object, Slic3r::sla::AutoOrientGoal::LeastPeel);
    CHECK(again.x() == easiest.x());
    CHECK(again.y() == easiest.y());
}

TEST_CASE("Auto orient: no cups turns an upside down cup off the plate", "[SLA][Rotfinder]")
{
    // A 20 mm cup with a 2 mm wall and a 2 mm floor, turned upside down in the mesh so that its
    // opening faces the vat. Every layer from the rim up to the floor is the ring between the two
    // squares and holds a 16 x 16 mm vacuum against the film.
    Slic3r::Domain::TriangleMesh mesh = make_cup(20., 20., 2., 2.);
    mesh.transform(Slic3r::Transform3d{
        Eigen::AngleAxisd{std::numbers::pi, Slic3r::Vec3d::UnitX()}});
    MeshModel cup{std::move(mesh)};

    // The pose the model is loaded in is the cup, and the measurement above finds it.
    const CoarseSlices as_loaded = coarse_slices_after_rotation(*cup.object, Slic3r::Vec2d::Zero());
    REQUIRE(as_loaded.cup_count == 1);
    REQUIRE(as_loaded.cup_opening_mm2 == Approx(16. * 16.).margin(1.));

    const Slic3r::Vec2d rotation =
        Slic3r::sla::auto_orient(*cup.object, Slic3r::sla::AutoOrientGoal::NoCups);
    REQUIRE(std::isfinite(rotation.x()));
    REQUIRE(std::isfinite(rotation.y()));

    // The cup is turned, so no layer of it is a suction cup any more.
    const CoarseSlices turned = coarse_slices_after_rotation(*cup.object, rotation);
    CHECK(turned.cup_count == 0);
    CHECK(turned.cup_opening_mm2 == 0.);

    // Its opening (the pocket, which is the local -Z of the cup after the turn) may end up sideways,
    // which is cup free too, but it may not end up facing the plate.
    const Slic3r::Vec3d opening{rotation_transform(rotation) * -Slic3r::Vec3d::UnitZ()};
    CHECK(opening.z() > -0.5);

    // A second run finds the same rotation.
    const Slic3r::Vec2d again =
        Slic3r::sla::auto_orient(*cup.object, Slic3r::sla::AutoOrientGoal::NoCups);
    CHECK(again.x() == rotation.x());
    CHECK(again.y() == rotation.y());
}

TEST_CASE("Auto orient: a cup standing the right way up is not turned by no cups", "[SLA][Rotfinder]")
{
    // The same cup, opening up. Its pocket is a pocket of trapped resin and not a cup, so there is
    // nothing for this goal to gain by turning the model, and the pose it comes back in keeps the
    // opening clear of the plate.
    MeshModel cup{make_cup(20., 20., 2., 2.)};

    const CoarseSlices as_loaded = coarse_slices_after_rotation(*cup.object, Slic3r::Vec2d::Zero());
    REQUIRE(as_loaded.cup_count == 0);

    const Slic3r::Vec2d rotation =
        Slic3r::sla::auto_orient(*cup.object, Slic3r::sla::AutoOrientGoal::NoCups);
    const CoarseSlices turned = coarse_slices_after_rotation(*cup.object, rotation);
    CHECK(turned.cup_count == 0);

    const Slic3r::Vec3d opening{rotation_transform(rotation) * Slic3r::Vec3d::UnitZ()};
    CHECK(opening.z() > -0.5);
}

TEST_CASE("Auto orient: the mesh of the object searches like the object does", "[SLA][Rotfinder]")
{
    // The mesh overload is what a job off the UI thread runs on, so it has to find the same
    // rotation as the model does.
    MeshModel plate{Slic3r::Biz::Algorithms::TriangleMesh::make_cube(60., 40., 2.)};
    const Slic3r::Domain::TriangleMesh mesh = Slic3r::sla::auto_orient_mesh(*plate.object);
    REQUIRE(!mesh.its.vertices.empty());

    for (const Slic3r::sla::AutoOrientGoal goal : {Slic3r::sla::AutoOrientGoal::MinHeight,
                                                    Slic3r::sla::AutoOrientGoal::LeastPeel,
                                                    Slic3r::sla::AutoOrientGoal::NoCups,
                                                    Slic3r::sla::AutoOrientGoal::Miniature}) {
        const Slic3r::Vec2d from_object = Slic3r::sla::auto_orient(*plate.object, goal);
        const Slic3r::Vec2d from_mesh   = Slic3r::sla::auto_orient(mesh, goal);
        CHECK(from_mesh.x() == from_object.x());
        CHECK(from_mesh.y() == from_object.y());
    }
}

TEST_CASE("Auto orient: miniature lays a head on its neck cut", "[SLA][Rotfinder]")
{
    // A head of 8 mm on a neck of 3 mm, with the neck cut flat. The head sits 10 mm above the cut,
    // so the piece is 18 mm tall as it is loaded.
    Slic3r::Domain::TriangleMesh mesh = make_head_on_neck(8., 3., 5.);

    // The neck is cut flat, so the facets of the bottom of it are exactly in one plane, and the
    // ball of the head has nothing flat in it at all: the cut is the only face the goal can find.
    REQUIRE(smallest_shared_edge_normal_angle_deg(mesh) < 2.);

    MeshModel head{std::move(mesh)};
    const Slic3r::Vec2d rotation =
        Slic3r::sla::auto_orient(*head.object, Slic3r::sla::AutoOrientGoal::Miniature);
    REQUIRE(std::isfinite(rotation.x()));
    REQUIRE(std::isfinite(rotation.y()));

    const Slic3r::Transform3d trafo = rotation_transform(rotation);

    // The cut of the neck points at the plate, off by the lean of the goal and nothing else: the
    // cut is 48 degrees off straight down, because the lean of the miniature goal is 48 degrees.
    const Slic3r::Vec3d cut_after = trafo * -Slic3r::Vec3d::UnitZ();
    CHECK(cut_after.z() < -0.5);
    CHECK(angle_between_degrees(cut_after, -Slic3r::Vec3d::UnitZ()) == Approx(48.).margin(0.5));

    // The piece leans over by that same angle, which is what makes the cross sections of the print
    // ramp in from the narrow neck up to the head instead of starting at the width of the head.
    const Slic3r::Vec3d up_after = trafo * Slic3r::Vec3d::UnitZ();
    CHECK(angle_between_degrees(up_after, Slic3r::Vec3d::UnitZ()) == Approx(48.).margin(0.5));

    // The head ends up above the cut and off to one side of it, which is the pose the goal is for.
    const Slic3r::Vec3d head_centre = trafo * Slic3r::Vec3d{0., 0., 10.};
    CHECK(head_centre.z() > 5.);
    CHECK(std::abs(head_centre.x()) + std::abs(head_centre.y()) > 1.);
}

TEST_CASE("Auto orient: miniature lays a bust on its small flat face", "[SLA][Rotfinder]")
{
    // A bust 6 x 5 mm at the cut and 20 x 14 mm at the top, 10 mm tall, with the top turned 15
    // degrees against the cut so that its four sides are warped quads and not flat. The wide top is
    // flat as well, but a face that is a third of the surface of the piece is a side of it and not
    // somewhere to hide a support in, so the small face at the bottom is the one that is printed on.
    Slic3r::Domain::TriangleMesh mesh = make_bust(6., 5., 20., 14., 10., 15.);

    // The two triangles of the cut are exactly in one plane, and no side of the bust is flat at
    // all, its sides being warped.
    REQUIRE(smallest_shared_edge_normal_angle_deg(mesh) < 2.);

    MeshModel bust{std::move(mesh)};
    const Slic3r::Vec2d rotation =
        Slic3r::sla::auto_orient(*bust.object, Slic3r::sla::AutoOrientGoal::Miniature);
    REQUIRE(std::isfinite(rotation.x()));
    REQUIRE(std::isfinite(rotation.y()));

    const Slic3r::Transform3d trafo = rotation_transform(rotation);

    // The small flat face is the one on the plate, within the lean.
    const Slic3r::Vec3d cut_after = trafo * -Slic3r::Vec3d::UnitZ();
    CHECK(cut_after.z() < -0.5);
    CHECK(angle_between_degrees(cut_after, -Slic3r::Vec3d::UnitZ()) == Approx(48.).margin(0.5));

    // The wide end of the bust is up and leaning, so the layers grow as the print goes up and the
    // face of it is not cut along its own layers.
    const Slic3r::Vec3d top_centre = trafo * Slic3r::Vec3d{1.5, 0.5, 10.};
    CHECK(top_centre.z() > 5.);
    const Slic3r::Vec3d up_after = trafo * Slic3r::Vec3d::UnitZ();
    CHECK(angle_between_degrees(up_after, Slic3r::Vec3d::UnitZ()) == Approx(48.).margin(0.5));
}

TEST_CASE("Auto orient: a miniature with no flat face is only leaned over", "[SLA][Rotfinder]")
{
    // A ball: there is no flat face in it at all, so there is no cut for the goal to find. Not two
    // of its facets that share an edge lie in one plane, which is what the icosphere is for.
    Slic3r::Domain::TriangleMesh mesh{make_sphere(10., 1)};
    REQUIRE(smallest_shared_edge_normal_angle_deg(mesh) > 2.);
    MeshModel ball{std::move(mesh)};

    Slic3r::Vec2d rotation{Slic3r::Vec2d::Zero()};
    CHECK_NOTHROW(
        rotation = Slic3r::sla::auto_orient(*ball.object, Slic3r::sla::AutoOrientGoal::Miniature));
    REQUIRE(std::isfinite(rotation.x()));
    REQUIRE(std::isfinite(rotation.y()));

    // The fallback keeps the pose the piece was loaded in and only leans it over, by the same 48
    // degrees as every other piece: no cut, nothing turned over, nothing thrown away.
    const Slic3r::Transform3d trafo = rotation_transform(rotation);
    const Slic3r::Vec3d up_after = trafo * Slic3r::Vec3d::UnitZ();
    CHECK(angle_between_degrees(up_after, Slic3r::Vec3d::UnitZ()) == Approx(48.).margin(0.5));
    CHECK((trafo * Slic3r::Vec3d{0., 0., 10.}).z() > 5.);
}

TEST_CASE("Auto orient: the status callback stops the search", "[SLA][Rotfinder]")
{
    MeshModel plate{Slic3r::Biz::Algorithms::TriangleMesh::make_cube(60., 40., 2.)};

    // The callback is asked whether to go on, so a search that is told to stop right away never
    // scores a pose and still answers with one instead of leaving the caller with nothing.
    int asked = 0;
    const Slic3r::Vec2d stopped = Slic3r::sla::auto_orient(
        *plate.object,
        Slic3r::sla::AutoOrientGoal::LeastPeel,
        [&asked](int) {
            ++asked;
            return false;
        }
    );
    CHECK(asked > 0);
    CHECK(std::isfinite(stopped.x()));
    CHECK(std::isfinite(stopped.y()));

    // A search that is allowed to run is asked as well, once per pose it walks, and answers with
    // the rotation it found. How many of those questions carry a percentage is up to the engine
    // (it drops the ones it has already reported), so only the asking is counted here.
    asked = 0;
    const Slic3r::Vec2d finished = Slic3r::sla::auto_orient(
        *plate.object,
        Slic3r::sla::AutoOrientGoal::NoCups,
        [&asked](int) {
            ++asked;
            return true;
        }
    );
    CHECK(asked > 1);
    CHECK(std::isfinite(finished.x()));
    CHECK(std::isfinite(finished.y()));
}
