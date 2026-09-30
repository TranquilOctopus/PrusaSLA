// M2.26: the per-point "may this support end on the model" switch. Lychee lets the user mark a
// single support as allowed (or forbidden) to be anchored on the model instead of on the plate,
// while support_buildplate_only and support_max_weight_on_model only decide that for a whole
// object. The switch is a tri-state on the support point, so a point that says nothing follows
// the object, which is the tree of before.
//
// Both trees are covered, on two models that need a support to be routed away from the point:
// a shallow ledge, where a pillar can be routed off it to the plate, and a deep pocket, where it
// cannot. The point of the pocket is the only place where the model itself can take the support,
// so what the switch decides there is visible in the tree without guessing anything.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "libslic3r/SLA/BranchingTreeSLA.hpp"
#include "libslic3r/SLA/DefaultSupportTree.hpp"
#include "libslic3r/SLA/SupportTree.hpp"
#include "libslic3r/SLA/SupportTreeBuilder.hpp"
#include "libslic3r/SLA/SupportTreeUtils.hpp"

using Catch::Approx;
namespace triangle_mesh = Slic3r::Biz::Algorithms::TriangleMesh;
namespace sla = Slic3r::sla;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPoints;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;

namespace {

// The detail level the support tree mesh builder uses by default.
constexpr size_t steps = 45;

// The object is lifted off the plate by this much, so the plate is at z = -6.
constexpr double elevation = 6.;

// The sizes SupportTreeConfig defaults to, which are the head of both points below.
constexpr double r_pin = 0.2;

indexed_triangle_set box(double x, double y, double z, Vec3f offset)
{
    indexed_triangle_set mesh = triangle_mesh::its_make_cube(x, y, z);
    for (Vec3f& v : mesh.vertices)
        v += offset;

    return mesh;
}

indexed_triangle_set model_of(const std::vector<indexed_triangle_set>& boxes)
{
    indexed_triangle_set ret;
    for (const indexed_triangle_set& b : boxes)
        Slic3r::Domain::its_merge(ret, b);

    return ret;
}

// A 20x20x10mm block with a 20x20x20mm block standing on it, the second one reaching over the
// first on two sides. A support under the overhanging part of the upper block cannot go straight
// down, but it can be routed off the lower block to the plate: the tree of the point is the same
// with and without the switch, which is what the tests here check for a point marked Forbid.
indexed_triangle_set ledge_model()
{
    return model_of({box(20., 20., 10., Vec3f{0.f, 0.f, 0.f}),
                     box(20., 20., 20., Vec3f{10.f, 10.f, 20.f})});
}

// A 40x40x10mm floor with four walls on it and a roof 30mm above the floor, leaving a 6x6x30mm
// pocket in the middle. The point is on the underside of the roof, at the top of the pocket: the
// pocket is closed on all four sides and too deep to leave within the 45 degrees a bridge may be
// tilted by, so the support can only be anchored on the pocket floor. Nothing about the shape of
// the tree depends on which point asked for it, only on where the pillar is allowed to end.
indexed_triangle_set pocket_model()
{
    return model_of({box(40., 40., 10., Vec3f{0.f, 0.f, 0.f}),      // the floor of the pocket
                     box(40., 40., 10., Vec3f{0.f, 0.f, 40.f}),      // the roof of the pocket
                     box(17., 40., 30., Vec3f{0.f, 0.f, 10.f}),     // a wall
                     box(17., 40., 30., Vec3f{23.f, 0.f, 10.f}),    // the opposite wall
                     box(6., 17., 30., Vec3f{17.f, 0.f, 10.f}),     // a wall
                     box(6., 17., 30., Vec3f{17.f, 23.f, 10.f})});  // the opposite wall
}

// Under the overhanging corner of the upper block, so the way down is blocked by the lower one.
const Vec3d ledge_point{15., 15., 20.};
// On the underside of the roof, in the middle of the pocket.
const Vec3d pocket_point{20., 20., 40.};

SupportPoint make_point(const Vec3d& pos, SupportPoint::OnModel on_model)
{
    SupportPoint sp;
    sp.pos               = pos.cast<float>();
    sp.head_front_radius = static_cast<float>(r_pin);
    sp.on_model          = on_model;

    return sp;
}

sla::SupportableMesh make_supportable_mesh(const indexed_triangle_set& model,
                                          const SupportPoints&           pts,
                                          bool                           plate_only = false)
{
    sla::SupportTreeConfig cfg;
    cfg.object_elevation_mm = elevation;
    cfg.ground_facing_only  = plate_only;

    return sla::SupportableMesh{
        .emesh = Slic3r::AABBMesh(model),
        .pts   = std::make_shared<const SupportPoints>(pts),
        .cfg   = cfg};
}

double volume(const indexed_triangle_set& mesh)
{
    return std::abs(double(Slic3r::Domain::its_volume(mesh)));
}

// The head of the point, which is only in the builder while the point got a support.
const sla::Head* head_at(const sla::SupportTreeBuilder& builder, const Vec3d& pos)
{
    for (const sla::Head& head : builder.heads())
        if (head.is_valid() && (head.pos - pos).norm() < 1e-3)
            return &head;

    return nullptr;
}

// A pillar that reaches the plate, which is what a base (pedestal) is built at the end of.
bool reaches_the_plate(const sla::SupportTreeBuilder& builder)
{
    return !builder.pedestals().empty() && !builder.pillars().empty();
}

} // namespace

TEST_CASE("The switch of a point follows the object unless the point says otherwise",
          "[suptreetree]")
{
    sla::SupportableMesh normal = make_supportable_mesh(ledge_model(), SupportPoints{});
    sla::SupportableMesh plate_only =
        make_supportable_mesh(ledge_model(), SupportPoints{}, true);

    // An object whose supports may end on the model: only a point that forbids it may not.
    SupportPoint allow = make_point(ledge_point, SupportPoint::OnModel::Allow);
    SupportPoint forbid = make_point(ledge_point, SupportPoint::OnModel::Forbid);
    SupportPoint inherit = make_point(ledge_point, SupportPoint::OnModel::Inherit);

    CHECK(sla::may_rest_on_model(normal, &allow));
    CHECK_FALSE(sla::may_rest_on_model(normal, &forbid));
    CHECK(sla::may_rest_on_model(normal, &inherit));

    // And an object whose supports may not: only a point that allows it may.
    CHECK(sla::may_rest_on_model(plate_only, &allow));
    CHECK_FALSE(sla::may_rest_on_model(plate_only, &forbid));
    CHECK_FALSE(sla::may_rest_on_model(plate_only, &inherit));

    // Without a point there is nothing to read, so the object decides.
    CHECK(sla::may_rest_on_model(normal, nullptr));
    CHECK_FALSE(sla::may_rest_on_model(plate_only, nullptr));

    // The trees ask whether any point of the object allows a model anchor, because that is what
    // they sample the model surface for.
    SupportPoints mixed{make_point(ledge_point, SupportPoint::OnModel::Inherit),
                        make_point(ledge_point, SupportPoint::OnModel::Forbid)};
    CHECK_FALSE(sla::any_may_rest_on_model(make_supportable_mesh(ledge_model(), mixed, true)));

    mixed[1].on_model = SupportPoint::OnModel::Allow;
    CHECK(sla::any_may_rest_on_model(make_supportable_mesh(ledge_model(), mixed, true)));
    CHECK_FALSE(sla::any_may_rest_on_model(make_supportable_mesh(ledge_model(), SupportPoints{})));
}

TEST_CASE("A point that may not end on the model is routed to the plate", "[suptreetree]")
{
    // Over the ledge the way down is blocked, but the plate is one bridge away, so the support is
    // built and stands on the plate. The switch does not cost the point its support.
    SupportPoints pts{make_point(ledge_point, SupportPoint::OnModel::Forbid)};
    sla::SupportableMesh sm = make_supportable_mesh(ledge_model(), pts);

    sla::SupportTreeBuilder builder;
    sla::create_default_tree(builder, sm);

    const sla::Head* head = head_at(builder, ledge_point);
    REQUIRE(head != nullptr);

    CHECK(reaches_the_plate(builder));
    CHECK(builder.anchors().empty());
    CHECK(volume(builder.merged_mesh(steps)) > 0.);
}

TEST_CASE("A point that may not end on the model gets no support where the plate is out of reach",
          "[suptreetree]")
{
    SECTION("the default of the switch is the model anchor of before")
    {
        // Inherit on an object whose supports may end on the model is the tree of a point from
        // before the switch existed: the pocket floor takes the support.
        SupportPoints pts{make_point(pocket_point, SupportPoint::OnModel::Inherit)};
        sla::SupportableMesh sm = make_supportable_mesh(pocket_model(), pts);

        sla::SupportTreeBuilder builder;
        sla::create_default_tree(builder, sm);

        const sla::Head* head = head_at(builder, pocket_point);
        REQUIRE(head != nullptr);
        // The pillar hangs from the anchor and never reaches the plate, so it has no base.
        CHECK(builder.pillars().size() == 1);
        REQUIRE(builder.anchors().size() == 1);
        // The anchor is on the floor of the pocket, well below the point.
        CHECK(builder.anchors().front().pos.z() < pocket_point.z - 10.);
        CHECK_FALSE(reaches_the_plate(builder));
    }

    SECTION("forbidden, the point has nothing to hold it")
    {
        // The pocket is closed, so there is no route to the plate either: the point is dropped,
        // which is the fallback an object whose supports may not end on the model has always had.
        SupportPoints pts{make_point(pocket_point, SupportPoint::OnModel::Forbid)};
        sla::SupportableMesh sm = make_supportable_mesh(pocket_model(), pts);

        sla::SupportTreeBuilder builder;
        sla::create_default_tree(builder, sm);

        CHECK(head_at(builder, pocket_point) == nullptr);
        CHECK(builder.anchors().empty());
        CHECK(builder.pillars().empty());
        CHECK(volume(builder.merged_mesh(steps)) == Approx(0.));
    }
}

TEST_CASE("A point that may end on the model is anchored on an object that may not",
          "[suptreetree]")
{
    SECTION("without the switch the point is dropped, as an object that may not was built before")
    {
        SupportPoints pts{make_point(pocket_point, SupportPoint::OnModel::Inherit)};
        sla::SupportableMesh sm = make_supportable_mesh(pocket_model(), pts, true);

        sla::SupportTreeBuilder builder;
        sla::create_default_tree(builder, sm);

        CHECK(head_at(builder, pocket_point) == nullptr);
        CHECK(builder.anchors().empty());
        CHECK(builder.pillars().empty());
    }

    SECTION("allowed, the point is anchored on the model")
    {
        SupportPoints pts{make_point(pocket_point, SupportPoint::OnModel::Allow)};
        sla::SupportableMesh sm = make_supportable_mesh(pocket_model(), pts, true);

        sla::SupportTreeBuilder builder;
        sla::create_default_tree(builder, sm);

        const sla::Head* head = head_at(builder, pocket_point);
        REQUIRE(head != nullptr);
        CHECK(builder.pillars().size() == 1);
        REQUIRE(builder.anchors().size() == 1);
        CHECK(builder.anchors().front().pos.z() < pocket_point.z - 10.);

        // The tree of a point that allows the model anchor is the tree the same point gets on an
        // object whose supports may end on the model anyway, down to the mesh of it.
        sla::SupportableMesh normal = make_supportable_mesh(
            pocket_model(),
            SupportPoints{make_point(pocket_point, SupportPoint::OnModel::Inherit)});

        sla::SupportTreeBuilder normal_builder;
        sla::create_default_tree(normal_builder, normal);

        CHECK(volume(builder.merged_mesh(steps)) ==
              Approx(volume(normal_builder.merged_mesh(steps))));
        CHECK(builder.pillars().size() == normal_builder.pillars().size());
    }
}

TEST_CASE("The branching tree routes the same way", "[suptreetree]")
{
    SECTION("an object that may not end on the model drops the point of the pocket")
    {
        SupportPoints pts{make_point(pocket_point, SupportPoint::OnModel::Inherit)};
        sla::SupportableMesh sm = make_supportable_mesh(pocket_model(), pts, true);

        sla::SupportTreeBuilder builder;
        sla::create_branching_tree(builder, sm);

        CHECK(head_at(builder, pocket_point) == nullptr);
        CHECK(builder.anchors().empty());
    }

    SECTION("a point that allows the model anchor is anchored on an object that may not")
    {
        SupportPoints pts{make_point(pocket_point, SupportPoint::OnModel::Allow)};
        sla::SupportableMesh sm = make_supportable_mesh(pocket_model(), pts, true);

        sla::SupportTreeBuilder builder;
        sla::create_branching_tree(builder, sm);

        // The branch of the point ends on the model: the mesh is sampled for it even though the
        // object as a whole may not end on the model, and the support is not routed to the plate.
        CHECK(head_at(builder, pocket_point) != nullptr);
        CHECK(builder.anchors().size() == 1);
    }

    SECTION("a point that forbids the model anchor is not anchored on an object that may")
    {
        SupportPoints pts{make_point(pocket_point, SupportPoint::OnModel::Forbid)};
        sla::SupportableMesh sm = make_supportable_mesh(pocket_model(), pts);

        sla::SupportTreeBuilder builder;
        sla::create_branching_tree(builder, sm);

        CHECK(builder.anchors().empty());
        // The pocket is closed, so there is nothing else the point could have been given.
        CHECK(head_at(builder, pocket_point) == nullptr);
    }
}
