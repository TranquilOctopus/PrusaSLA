// The infill lattice of a hollow print (M2.29a). hollowing_infill leaves a grid or a cubic lattice
// of struts standing inside the cavity, which is what Chitubox and Lychee offer: a hollow print
// with a thin wall can deform and the struts stiffen it while the resin the hollowing saves stays
// saved. The engine builds the struts over the bounding box of the generated interior and cuts them
// out of it, so what the hollowing step subtracts from the model is the cavity without the struts
// and the struts come out as solid material of the print.
//
// None has to be the hollow print of today bit for bit, so the first test here is that a cavity
// asked for no lattice comes out of the generator untouched. The rest works on a 12mm cube with a
// 2mm wall, small enough that its cavity and the mesh booleans of the test stay quick: the lattice
// adds resin, it stays inside the cavity it was built in, the shell stays a closed solid, and a
// drain hole that a strut stands across is still open.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/CGAL/Algorithms/MeshBoolean.hpp"
#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/SLA/DrainHole.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Utils.hpp"

#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/CSGMesh/CSGMesh.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/SLA/Hollowing.hpp"
#include "libslic3r/SLA/HollowingLattice.hpp"

using namespace Slic3r;
using Catch::Approx;
using Slic3r::Domain::ConfigItemDef;

namespace TriMesh     = Slic3r::Biz::Algorithms::TriangleMesh;
namespace MeshBoolean = Slic3r::Biz::CGAL::Algorithms::MeshBoolean;

namespace {

// A 12mm cube, the model the lattice is measured in. The interior of a 2mm wall inside it is 8mm
// across, which at the spacing below is three axes per direction and a column exactly in the middle
// of it, which is what the drain hole test needs.
constexpr double CUBE_MM = 12.;
constexpr double WALL_MM = 2.;

// The closing distance is what rounds the corners of the interior, half of the wall here.
constexpr double CLOSING_MM = 0.5;

// The hollowing of the model, the closing distance is what rounds the corners of the interior.
Slic3r::sla::HollowingConfig hollowing_cfg()
{
    Slic3r::sla::HollowingConfig cfg;
    cfg.min_thickness    = WALL_MM;
    cfg.quality          = 0.5;
    cfg.closing_distance = CLOSING_MM;
    return cfg;
}

// The lattice of the test: axes at 2.5mm, so the cavity of a 2mm wall in a 12mm cube gets three
// axes per direction and nine columns, one of which stands in the middle of the cavity.
Slic3r::sla::HollowingInfillConfig grid_cfg(double spacing_mm = 2.5, double strut_mm = 0.6)
{
    Slic3r::sla::HollowingInfillConfig cfg;
    cfg.type       = Domain::sla::HollowingInfillType::Grid;
    cfg.spacing_mm = spacing_mm;
    cfg.strut_mm   = strut_mm;
    return cfg;
}

Slic3r::sla::HollowingInfillConfig none_cfg()
{
    Slic3r::sla::HollowingInfillConfig cfg;
    cfg.type = Domain::sla::HollowingInfillType::None;
    return cfg;
}

// The positive CSG parts of the cube and its outer surface. The parts only point into the mesh, so
// the mesh is kept next to them.
struct Cube
{
    std::vector<indexed_triangle_set> meshes;
    std::vector<csg::CSGPart> parts;
    indexed_triangle_set outer;
};

Cube make_cube()
{
    Cube cube;
    cube.meshes.emplace_back(TriMesh::its_make_cube(CUBE_MM, CUBE_MM, CUBE_MM));

    indexed_triangle_set &mesh = cube.meshes.back();
    // The voxelizer tells inside from outside by the winding, so the part has to be a closed solid
    // with its normals pointing outwards. The primitive is, this only makes the test independent of
    // how its faces happen to be ordered.
    if (Domain::its_volume(mesh) < 0.f)
        sla::swap_normals(mesh);

    REQUIRE(Domain::its_volume(mesh) > 0.f);

    cube.parts.emplace_back(&mesh, csg::CSGType::Union);
    cube.outer = csg::csgmesh_merge_positive_parts(cube.parts);
    REQUIRE(!cube.outer.indices.empty());

    return cube;
}

// The interior of the hollowed cube, which is what the lattice is cut out of.
Slic3r::sla::InteriorPtr hollowed_interior(const Cube &cube)
{
    Slic3r::sla::InteriorPtr interior =
        sla::generate_interior(Slic3r::range(cube.parts), hollowing_cfg());

    REQUIRE(interior);

    // Reported before the check that fails first, which is the emptiness of the mesh the lattice
    // would be cut out of, and the settings of the hollowing that made it.
    const indexed_triangle_set& mesh = sla::get_mesh(*interior);
    INFO(
        "a "
        << CUBE_MM
        << "mm cube hollowed with a "
        << WALL_MM
        << "mm wall and a closing distance of "
        << CLOSING_MM
        << "mm came out with "
        << mesh.vertices.size()
        << " vertices and "
        << mesh.indices.size()
        << " triangles"
    );
    REQUIRE(!mesh.indices.empty());

    return interior;
}

// The shell the hollowing step prints: the outer surface of the model with the cavity taken out of
// it. This is the one boolean the csg of the hollowing step does, done here the same way so the
// result can be measured.
indexed_triangle_set hollowed_shell(const Cube &cube, const indexed_triangle_set &cavity)
{
    indexed_triangle_set shell = cube.outer;
    MeshBoolean::cgal::minus(shell, cavity);
    return shell;
}

// The struts of a lattice as they are printed: the material that the lattice adds to a print, which
// is the part of the lattice that lies inside the cavity, because the print is the model with the
// cavity minus the lattice taken out of it.
//
// It is not cut out of the two shells with a mesh boolean, and that is not a shortcut: the shell
// with the lattice in it and the plain hollow shell are two solids that share their whole outer
// surface and the wall of the cavity around every strut, so the faces of the two are coplanar over
// the whole of that surface. A mesh boolean wants the two surfaces to cross in segments, and two
// solids that share a surface never do, which is why CGAL refuses that difference and not this one.
// The lattice and the cavity cross each other instead, which is a boolean it does answer.
indexed_triangle_set
printed_struts(const indexed_triangle_set &cavity, const Slic3r::sla::HollowingInfillConfig &cfg)
{
    const Domain::BoundingBox3d bb = Domain::bounding_box(cavity);

    indexed_triangle_set struts;
    for (const indexed_triangle_set &bundle :
         sla::make_hollowing_lattice(bb, cfg, sla::JobController{}, sla::LATTICE_WALL_OVERLAP_MM)) {
        indexed_triangle_set inside = bundle;
        MeshBoolean::cgal::intersect(inside, cavity);
        Domain::its_merge(struts, inside);
    }

    return struts;
}

// How far the material of a mesh is from the outside of the model, in mm. The lattice is inside the
// cavity, so the smallest of those distances is what tells that no strut reached the outer surface.
double min_distance_to_outer(const indexed_triangle_set &mesh, const AABBMesh &outer)
{
    double min_d = std::numeric_limits<double>::infinity();

    for (const Domain::Vec3f &p : mesh.vertices) {
        const Domain::Vec3d v = p.cast<double>();
        min_d                 = std::min(min_d, std::sqrt(outer.squared_distance(v)));
    }

    return min_d;
}

// The volume of a mesh, which is how much resin a print of it holds.
double volume(const indexed_triangle_set &mesh)
{
    double v = 0.;

    for (const Domain::Index3 &t : mesh.indices)
        v += mesh.vertices[t[0]].dot(mesh.vertices[t[1]].cross(mesh.vertices[t[2]]));

    return std::abs(v) / 6.;
}

// The number of surfaces a ray meets on its way, which is what says whether the hole it runs
// along is open: an open hole is a void from one end of the ray to the other and meets nothing.
size_t crossings(const AABBMesh &mesh, const Domain::Vec3d &from, const Domain::Vec3d &direction)
{
    size_t n = 0;

    for (const AABBMesh::hit_result &hit : mesh.query_ray_hits(from, direction))
        if (hit.is_hit())
            ++n;

    return n;
}

// A config view over the given object settings, the way the engine builds one for a print object.
Slic3r::SLAPrintObjectConfigView make_config_view(Slic3r::Domain::SLAObjectSettings &settings)
{
    using Slic3r::Domain::FullConfigSLA;
    using Slic3r::Domain::PartialObjectConfigSLA;

    Slic3r::Domain::FullConfigSLAPtr full{
        std::make_shared<const FullConfigSLA>(FullConfigSLA::defaults())
    };
    Slic3r::Domain::PartialObjectConfigSLAPtr object{
        std::make_shared<PartialObjectConfigSLA>(settings, full->hw_config())
    };

    return Slic3r::SLAPrintObjectConfigView{full, object};
}

const ConfigItemDef *find_def(const std::string &name)
{
    const auto &defs = Slic3r::Domain::get_defs_sla();
    for (const auto &def : defs.defs())
        if (def.name == name)
            return &def;

    return nullptr;
}

// The axes of the grid in the plane it stands in, the columns of the pattern being every pair of
// them. The lattice is placed from the bounds of the cavity, which the voxelizer decides, so this
// asks the engine where the struts are rather than assuming where they are.
std::vector<double> grid_axes(const Domain::BoundingBox3d &cavity_bb, int axis, double spacing_mm)
{
    return sla::hollowing_lattice_axes(cavity_bb.min[axis], cavity_bb.max[axis], spacing_mm);
}

// Where the sides of the struts of a bundle stand along one axis, sorted and without a repeat. The
// struts of a direction all run the same way, so every one of them puts its two sides on two of
// these, and two struts never touch, so the values come in pairs.
std::vector<double> strut_sides(const indexed_triangle_set &struts, int axis)
{
    std::vector<double> sides;
    sides.reserve(struts.vertices.size());

    for (const Domain::Vec3f &p : struts.vertices)
        sides.push_back(static_cast<double>(p[axis]));

    std::sort(sides.begin(), sides.end());
    sides.erase(std::unique(sides.begin(), sides.end()), sides.end());

    return sides;
}

// The axis of the strut that stands between the two given sides.
double strut_axis(const std::vector<double> &sides, size_t i)
{
    return 0.5 * (sides[i] + sides[i + 1]);
}

// The axis nearest to the middle of the interval, which is the column of the grid that stands in
// the middle of the cavity.
double middle_axis(const std::vector<double> &axes, double middle)
{
    double best     = middle;
    double distance = std::numeric_limits<double>::infinity();

    for (double axis : axes) {
        const double d = std::abs(axis - middle);
        if (d < distance) {
            distance = d;
            best     = axis;
        }
    }

    return best;
}

// The drain hole of the through hole check: a 3mm wide hole through the floor of the cube, on the
// column of the grid that stands in the middle of the cavity. The hole is off the centre line of
// the column by a twentieth of a millimetre on purpose: on it, the cutter would run exactly along
// the faces of the strut, which is the one geometry a mesh boolean is least happy about, while a
// hundredth of a millimetre off leaves the cutter clear of them and the column cut right through.
// It reaches past the model on both ends, so the column is severed from end to end and not only at
// its foot, and the cap of the cutter stands clear of the faces of the model on purpose as well: a
// cap that ends exactly in the face of the model is a cap coplanar with a face of the shell, and a
// mesh boolean does not answer a cutter that lies in the surface it is to cut, the same way it does
// not answer a strut that ends in the wall of the cavity.
Domain::SLA::DrainHoles hole_through_the_column(
    const Domain::BoundingBox3d &cavity_bb,
    const Slic3r::sla::HollowingInfillConfig &cfg,
    double &x,
    double &y
)
{
    const double middle_x = 0.5 * (cavity_bb.min[0] + cavity_bb.max[0]);
    const double middle_y = 0.5 * (cavity_bb.min[1] + cavity_bb.max[1]);

    x = middle_axis(grid_axes(cavity_bb, 0, cfg.spacing_mm), middle_x) + 0.05;
    y = middle_axis(grid_axes(cavity_bb, 1, cfg.spacing_mm), middle_y) + 0.05;

    Domain::SLA::DrainHole hole;
    hole.pos    = Domain::Vec3f(static_cast<float>(x), static_cast<float>(y), 0.f);
    hole.normal = Domain::Vec3f(0.f, 0.f, 1.f);
    hole.radius = 1.5f;
    hole.height = 14.f;
    hole.failed = false;

    Domain::SLA::DrainHoles holes{hole};
    // The hole goes through the transformation of the object on its way to the cutter, which pulls
    // the near cap a millimetre back and makes the hole that much deeper. The slicing step does the
    // same before it drills, so the test does it here: the cutter then runs from z = -1 to z = 15 and
    // stands clear of the top face of the cube, which is at z = 12.
    sla::transform_drainhole_points(holes, Domain::Transform3d::Identity());

    return holes;
}

} // namespace

TEST_CASE(
    "Hollowing: the infill lattice settings sit with the hollowing keys",
    "[SLA][Hollowing][Infill]"
)
{
    // The three knobs live in the group that shows the wall thickness and the closing distance, and
    // they start at no lattice at all, so a print preset that never sets them keeps today's hollow.
    const ConfigItemDef *thickness = find_def("hollowing_min_thickness");
    const ConfigItemDef *infill    = find_def("hollowing_infill");
    const ConfigItemDef *spacing   = find_def("hollowing_infill_spacing");
    const ConfigItemDef *strut     = find_def("hollowing_infill_strut");

    REQUIRE(thickness != nullptr);
    REQUIRE(infill != nullptr);
    REQUIRE(spacing != nullptr);
    REQUIRE(strut != nullptr);

    CHECK(infill->category == ConfigItemDef::Category::Print_Hollowing);
    CHECK(infill->option_group == thickness->option_group);
    CHECK(spacing->category == ConfigItemDef::Category::Print_Hollowing);
    CHECK(strut->category == ConfigItemDef::Category::Print_Hollowing);
    CHECK(spacing->option_group == thickness->option_group);
    CHECK(strut->option_group == thickness->option_group);

    CHECK(infill->gui_type == ConfigItemDef::GUIType::combobox);
    CHECK(spacing->gui_type == ConfigItemDef::GUIType::textfield);
    CHECK(strut->gui_type == ConfigItemDef::GUIType::textfield);

    CHECK_FALSE(infill->tooltip.empty());
    CHECK_FALSE(spacing->tooltip.empty());
    CHECK_FALSE(strut->tooltip.empty());

    CHECK(
        infill->init_fn().get<Domain::sla::HollowingInfillType>()
        == Domain::sla::HollowingInfillType::None
    );
    CHECK(spacing->init_fn().get<double>() > 0.);
    CHECK(strut->init_fn().get<double>() > 0.);
}

TEST_CASE("Hollowing: the infill keys reach the lattice of the engine", "[SLA][Hollowing][Infill]")
{
    Slic3r::Domain::SLAObjectSettings settings;

    SECTION("a config without the keys is the plain cavity")
    {
        const Slic3r::sla::HollowingInfillConfig cfg =
            Slic3r::make_hollowing_infill_cfg(make_config_view(settings));

        CHECK(cfg.type == Domain::sla::HollowingInfillType::None);
    }

    SECTION("the keys are what the cavity is filled with")
    {
        settings.overrides.set("hollowing_infill", Domain::sla::HollowingInfillType::Cubic);
        settings.overrides.set("hollowing_infill_spacing", 5.);
        settings.overrides.set("hollowing_infill_strut", 0.8);

        const Slic3r::sla::HollowingInfillConfig cfg =
            Slic3r::make_hollowing_infill_cfg(make_config_view(settings));

        CHECK(cfg.type == Domain::sla::HollowingInfillType::Cubic);
        CHECK(cfg.spacing_mm == Approx(5.));
        CHECK(cfg.strut_mm == Approx(0.8));
    }
}

TEST_CASE("Hollowing: no infill is the hollow print of today", "[SLA][Hollowing][Infill]")
{
    // The control for the tests below: the cavity of a hollowed cube with no lattice asked for.
    const Cube cube                               = make_cube();
    const Slic3r::sla::InteriorPtr plain_interior = hollowed_interior(cube);
    const indexed_triangle_set plain_cavity       = sla::get_mesh(*plain_interior);

    const Cube none_cube                         = make_cube();
    const Slic3r::sla::InteriorPtr none_interior = hollowed_interior(none_cube);
    indexed_triangle_set none_cavity             = sla::get_mesh(*none_interior);

    // Asking for no lattice changes nothing at all, so a print that does not set the keys is the
    // hollow print it has always been.
    CHECK_FALSE(sla::subtract_lattice_from_cavity(none_cavity, none_cfg()));
    CHECK(none_cavity == plain_cavity);

    // The struts themselves are not even built for None, while a grid is one bundle of columns.
    const Slic3r::sla::HollowingInfillConfig grid = grid_cfg();
    const Domain::BoundingBox3d bb                = Domain::bounding_box(plain_cavity);
    CHECK(sla::make_hollowing_lattice(bb, none_cfg()).empty());
    CHECK(sla::make_hollowing_lattice(bb, grid).size() == size_t(1));
}

TEST_CASE(
    "Hollowing: a grid is a square grid of columns, a cubic lattice adds two directions",
    "[SLA][Hollowing][Infill]"
)
{
    // Any box will do here, the pattern only depends on the box and the two knobs. It is the box of
    // the cavity in the engine, and the tests below cut the struts out of a real one.
    Domain::BoundingBox3d bb{Domain::Vec3d(2., 3., 4.), Domain::Vec3d(12., 13., 14.)};

    SECTION("the axes of the pattern are the ones the engine reports")
    {
        // Three axes at 3mm in a 10mm interval, symmetric about its middle and a whole spacing away
        // from the ends. One axis of a shorter interval is still one strut, so a cavity that is
        // thinner than the spacing is not left without a lattice.
        const std::vector<double> axes = sla::hollowing_lattice_axes(0., 10., 3.);
        REQUIRE(axes.size() == size_t(3));
        CHECK(axes[0] == Approx(2.));
        CHECK(axes[1] == Approx(5.));
        CHECK(axes[2] == Approx(8.));
        CHECK(sla::hollowing_lattice_axes(0., 10., 0.).empty());
        CHECK(sla::hollowing_lattice_axes(5., 5., 3.).empty());
        CHECK(sla::hollowing_lattice_axes(0., 1., 3.).size() == size_t(1));
    }

    SECTION("a grid is one bundle of square columns")
    {
        const Slic3r::sla::HollowingInfillConfig cfg    = grid_cfg(3., 0.8);
        const std::vector<indexed_triangle_set> lattice = sla::make_hollowing_lattice(bb, cfg);

        REQUIRE(lattice.size() == size_t(1));

        // A column per pair of axes, and a box of six faces is twelve triangles.
        const std::vector<double> xs = grid_axes(bb, 0, cfg.spacing_mm);
        const std::vector<double> ys = grid_axes(bb, 1, cfg.spacing_mm);
        INFO("the grid stands on " << xs.size() << " by " << ys.size() << " columns");
        CHECK(lattice[0].indices.size() == 12 * xs.size() * ys.size());

        // The columns stand where the engine says they stand, and they are as wide as the strut.
        const std::vector<double> sides = strut_sides(lattice[0], 0);
        REQUIRE(sides.size() == 2 * xs.size());
        for (size_t i = 0; i < xs.size(); ++i) {
            INFO(
                "column "
                << i
                << " stands on "
                << strut_axis(sides, 2 * i)
                << ", the engine says "
                << xs[i]
            );
            CHECK(strut_axis(sides, 2 * i) == Approx(xs[i]).margin(1e-3));
            CHECK(sides[2 * i + 1] - sides[2 * i] == Approx(cfg.strut_mm).margin(1e-3));
        }

        // Every column runs the whole height of the box along its own axis, so the bundle fills the
        // box in that direction and nothing of it is outside. The two axes the columns cross are a
        // different matter: the outermost axes stand a whole spacing inside the ends of the box,
        // which is the rule the first section pins and the one that keeps two neighbouring struts
        // the same gap apart at the ends as in the middle, so the bundle there reaches from the
        // first axis less half a strut to the last axis plus half a strut.
        const Domain::BoundingBox3d bundle_bb = Domain::bounding_box(lattice[0]);
        CHECK(bundle_bb.min[2] == Approx(bb.min[2]).margin(1e-3));
        CHECK(bundle_bb.max[2] == Approx(bb.max[2]).margin(1e-3));

        for (int axis = 0; axis < 2; ++axis) {
            const std::vector<double> axes = grid_axes(bb, axis, cfg.spacing_mm);
            INFO(
                "axis "
                << axis
                << ": the bundle runs from "
                << bundle_bb.min[axis]
                << " to "
                << bundle_bb.max[axis]
                << ", the outermost columns stand on "
                << axes.front()
                << " and "
                << axes.back()
                << " in a box that runs from "
                << bb.min[axis]
                << " to "
                << bb.max[axis]
            );
            CHECK(bundle_bb.min[axis] == Approx(axes.front() - 0.5 * cfg.strut_mm).margin(1e-3));
            CHECK(bundle_bb.max[axis] == Approx(axes.back() + 0.5 * cfg.strut_mm).margin(1e-3));
            CHECK(bundle_bb.min[axis] >= bb.min[axis]);
            CHECK(bundle_bb.max[axis] <= bb.max[axis]);
        }
    }

    SECTION("a strut never takes the whole spacing, so two of them keep a gap")
    {
        // Two struts that touch are two boxes that overlap, which is not a solid a mesh boolean can
        // take, so a strut wider than the gap is narrowed to nine tenths of the spacing.
        const Slic3r::sla::HollowingInfillConfig cfg    = grid_cfg(2., 5.);
        const std::vector<indexed_triangle_set> lattice = sla::make_hollowing_lattice(bb, cfg);

        REQUIRE(lattice.size() == size_t(1));

        const std::vector<double> sides = strut_sides(lattice[0], 0);
        REQUIRE(sides.size() > size_t(2));
        for (size_t i = 0; i + 1 < sides.size(); i += 2)
            CHECK(sides[i + 1] - sides[i] == Approx(1.8).margin(1e-3));
    }

    SECTION("a cubic lattice is the same grid plus two more directions")
    {
        Slic3r::sla::HollowingInfillConfig cfg = grid_cfg(3., 0.8);
        cfg.type                               = Domain::sla::HollowingInfillType::Cubic;

        CHECK(sla::make_hollowing_lattice(bb, cfg).size() == size_t(3));
    }

    SECTION("a box or a spacing that leaves no room for a strut has no lattice")
    {
        CHECK(sla::make_hollowing_lattice(Domain::BoundingBox3d{}, grid_cfg()).empty());

        Slic3r::sla::HollowingInfillConfig cfg = grid_cfg(3., 0.);
        CHECK(sla::make_hollowing_lattice(bb, cfg).empty());
    }
}

TEST_CASE(
    "Hollowing: a grid in the cavity adds resin and stays inside the model",
    "[SLA][Hollowing][Infill]"
)
{
    const Cube cube = make_cube();

    // The plain hollow print, the control for the volume and for what a strut adds.
    const Slic3r::sla::InteriorPtr plain_interior = hollowed_interior(cube);
    const indexed_triangle_set plain_shell = hollowed_shell(cube, sla::get_mesh(*plain_interior));
    REQUIRE(!plain_shell.indices.empty());

    // The same print with the grid left in the cavity.
    const Slic3r::sla::InteriorPtr infilled_interior = hollowed_interior(cube);
    indexed_triangle_set infilled_cavity             = sla::get_mesh(*infilled_interior);
    REQUIRE(sla::subtract_lattice_from_cavity(infilled_cavity, grid_cfg()));

    const indexed_triangle_set infilled_shell = hollowed_shell(cube, infilled_cavity);
    REQUIRE(!infilled_shell.indices.empty());

    INFO(
        "the plain shell is "
        << volume(plain_shell)
        << "mm3, with the grid "
        << volume(infilled_shell)
        << "mm3, out of "
        << volume(cube.outer)
        << "mm3 of solid cube"
    );
    CHECK(volume(infilled_shell) > volume(plain_shell));
    CHECK(volume(infilled_shell) < volume(cube.outer));

    // The lattice is inside the cavity, so the print keeps the outline of the model: the shell is
    // still within the bounds of the solid cube.
    const Domain::BoundingBox3d outer_bb = Domain::bounding_box(cube.outer);
    const Domain::BoundingBox3d shell_bb = Domain::bounding_box(infilled_shell);
    for (int axis = 0; axis < 3; ++axis) {
        INFO(
            "axis "
            << axis
            << ": the shell runs from "
            << shell_bb.min[axis]
            << " to "
            << shell_bb.max[axis]
            << ", the cube from "
            << outer_bb.min[axis]
            << " to "
            << outer_bb.max[axis]
        );
        CHECK(shell_bb.min[axis] >= outer_bb.min[axis]);
        CHECK(shell_bb.max[axis] <= outer_bb.max[axis]);
    }

    // The struts themselves, which is what the lattice added to the print, and no strut reached the
    // outer surface: the cavity is a full wall away from it everywhere.
    const indexed_triangle_set struts = printed_struts(sla::get_mesh(*plain_interior), grid_cfg());
    REQUIRE(!struts.indices.empty());

    const AABBMesh outer(cube.outer);
    const double min_d = min_distance_to_outer(struts, outer);
    INFO(
        "the closest the "
        << struts.indices.size()
        << " triangles of the lattice come to the "
        << "outer surface is "
        << min_d
        << "mm"
    );
    CHECK(min_d > 0.2);

    // And the shell with the struts in it is still a closed solid, which is what the slicer and the
    // printer need of it.
    CHECK(
        MeshBoolean::cgal::
            does_bound_a_volume(*MeshBoolean::cgal::triangle_mesh_to_cgal(infilled_shell))
    );
    CHECK(Domain::its_volume(infilled_shell) > 0.f);
}

TEST_CASE(
    "Hollowing: a drain hole through a strut of the lattice stays open",
    "[SLA][Hollowing][Infill][DrainHole]"
)
{
    const Cube cube                                  = make_cube();
    const Slic3r::sla::InteriorPtr infilled_interior = hollowed_interior(cube);
    const indexed_triangle_set cavity                = sla::get_mesh(*infilled_interior);
    indexed_triangle_set infilled_cavity             = cavity;

    // The hole goes through the column of the grid that stands in the middle of the cavity, so it
    // asks the engine where the struts are before it drills.
    const Slic3r::sla::HollowingInfillConfig cfg = grid_cfg();
    const Domain::BoundingBox3d cavity_bb        = Domain::bounding_box(cavity);
    double hole_x                                = 0.;
    double hole_y                                = 0.;
    const Domain::SLA::DrainHoles holes  = hole_through_the_column(cavity_bb, cfg, hole_x, hole_y);
    const indexed_triangle_set hole_mesh = sla::to_mesh(holes.front());

    REQUIRE(sla::subtract_lattice_from_cavity(infilled_cavity, cfg));

    // The shell of the print with the lattice in it, and the material the lattice is: the struts are
    // printed as solid, so they are the part of the lattice that the cavity left standing.
    const indexed_triangle_set infilled_shell = hollowed_shell(cube, infilled_cavity);
    const indexed_triangle_set struts        = printed_struts(cavity, cfg);
    REQUIRE(!struts.indices.empty());

    // The hole is cut in the drilling step, which comes after the hollowing one, so it reaches the
    // struts as well: they are material of the print by then, not a void of the cavity any more.
    const Domain::Vec3d on_the_axis(hole_x, hole_y, cavity_bb.min[2] - 0.5);
    const Domain::Vec3d upwards(0., 0., 1.);

    const size_t closed_crossings = crossings(AABBMesh{struts}, on_the_axis, upwards);

    indexed_triangle_set opened = struts;
    MeshBoolean::cgal::minus(opened, hole_mesh);

    const size_t open_crossings = crossings(AABBMesh{opened}, on_the_axis, upwards);

    INFO(
        "a ray up the axis of the hole at ("
        << hole_x
        << ", "
        << hole_y
        << ") met "
        << closed_crossings
        << " surfaces of the lattice and "
        << open_crossings
        << " of the same lattice with the hole cut into it"
    );

    // The column of the grid is really standing on the axis of the hole, so a drain hole there
    // would be plugged without being cut into the struts.
    CHECK(closed_crossings > size_t(0));

    // And it is cut through, so the line of the hole is clear from the floor of the cavity to the
    // top of the column.
    CHECK(open_crossings == size_t(0));

    // The floor of the model is open under it, which is the whole point of a drain hole and the
    // check the hollowing tests already make: a ray down the axis of the hole starts in the floor
    // and finds nothing on its way out, while the same ray against the shell with the hole not cut
    // into it finds the bottom of the model.
    indexed_triangle_set drilled = infilled_shell;
    MeshBoolean::cgal::minus(drilled, hole_mesh);

    const Domain::Vec3d in_the_floor(hole_x, hole_y, 1.);
    const Domain::Vec3d downwards(0., 0., -1.);

    const size_t drilled_crossings   = crossings(AABBMesh{drilled}, in_the_floor, downwards);
    const size_t undrilled_crossings = crossings(AABBMesh{infilled_shell}, in_the_floor, downwards);

    INFO(
        "a ray down the axis of the hole met "
        << drilled_crossings
        << " surfaces of the drilled "
        << "shell and "
        << undrilled_crossings
        << " of the same shell with the hole not cut "
        << "into it"
    );
    CHECK(undrilled_crossings >= size_t(1));
    CHECK(drilled_crossings == size_t(0));
}
