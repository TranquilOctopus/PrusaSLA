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
#include <numbers>
#include <vector>

using Catch::Approx;

namespace {

// A model mesh is kept in scaled coordinates: one unit is this many millimetres, so a length of it
// is multiplied by it to get mm and an area by it squared. The meshes the importers hand over are
// scaled up, and so is every mesh these models are built from.
constexpr double sf = Slic3r::Biz::Algorithms::Scaling::SCALING_FACTOR;

// A mesh in the coordinates a model keeps it in, the way the importers hand one over. The engine
// searches those, and the helpers below convert back to mm and mm² on the way out.
Slic3r::Domain::TriangleMesh into_model_coordinates(Slic3r::Domain::TriangleMesh mesh)
{
    mesh.scale(float(1. / sf));
    return mesh;
}

// A model with one object holding a box of the given size, standing on the plate. The box can be
// tilted in the mesh itself, because that is what the rotation optimizer works on.
struct BoxModel
{
    Slic3r::Domain::Model model;
    Slic3r::Domain::ModelObject* object{nullptr};

    BoxModel(double x, double y, double z, double tilt_about_x_rad = 0.)
    {
        Slic3r::Domain::TriangleMesh mesh =
            into_model_coordinates(Slic3r::Biz::Algorithms::TriangleMesh::make_cube(x, y, z));
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
    return (max_z - min_z) * sf;
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

// The faces of the object's mesh after rotating it by the given X/Y angles. The vertices are scaled,
// so the areas are scaled down to the mm² the tests compare against.
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
            const double area = 0.5 * cross.norm() * sf * sf;
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

// A model with one object holding the mesh it is given, in millimetres, which is scaled into the
// coordinates a model is kept in: the goals that slice the mesh (least peel, no cups) cut their
// planes at that Z and read the areas of the slices back out of it, so a mesh left in millimetres
// would be measured as a trillionth of its size.
struct MeshModel
{
    Slic3r::Domain::Model model;
    Slic3r::Domain::ModelObject* object{nullptr};

    explicit MeshModel(Slic3r::Domain::TriangleMesh mesh)
    {
        Slic3r::Domain::TriangleMesh scaled_mesh = into_model_coordinates(std::move(mesh));
        object = model.add_object();
        Slic3r::Biz::Algorithms::ModelObject::add_volume(object, std::move(scaled_mesh));
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

    const auto add_box = [&its](double x0, double x1, double y0, double y1, double z0, double z1) {
        Slic3r::Domain::TriangleMesh box{
            Slic3r::Biz::Algorithms::TriangleMesh::make_cube(x1 - x0, y1 - y0, z1 - z0)};
        box.translate(Slic3r::Vec3f{float((x0 + x1) / 2.), float((y0 + y1) / 2.), float(z0)});
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
    // of the mesh does not cut it. The Z of the mesh is scaled, so the step of the planes is scaled
    // with it too: a millimetre of height is 1 / sf of its units.
    const double step = 1. / sf;
    std::vector<float> zs;
    for (double z = double(zmin) + step_mm * step / 2.; z < double(zmax); z += step_mm * step)
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
    const Slic3r::Vec3d opening{rotation_transform(rotation) * Slic3r::Vec3d::NegZ()};
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
                                                    Slic3r::sla::AutoOrientGoal::NoCups}) {
        const Slic3r::Vec2d from_object = Slic3r::sla::auto_orient(*plate.object, goal);
        const Slic3r::Vec2d from_mesh   = Slic3r::sla::auto_orient(mesh, goal);
        CHECK(from_mesh.x() == from_object.x());
        CHECK(from_mesh.y() == from_object.y());
    }
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
