#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <test_utils.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <unordered_set>
#include <vector>

#include "Slic3r/Biz/Format/STL.hpp"
#include "Slic3r/Biz/Algorithms/Execution/ExecutionSeq.hpp"
#include "Slic3r/Biz/Algorithms/Execution/ExecutionTBB.hpp"
#include "Slic3r/Biz/Algorithms/Optimize/NLoptOptimizer.hpp"
#include "Slic3r/Biz/Algorithms/Optimize/Optimizer.hpp"
#include "libslic3r/SLA/SupportTreeUtils.hpp"
#include "libslic3r/SLA/SupportTreeUtilsLegacy.hpp"

using Catch::Approx;
namespace triangle_mesh = Slic3r::Biz::Algorithms::TriangleMesh;
using Slic3r::Domain::SLA::SupportPoints;

// Test pair hash for 'nums' random number pairs.
template <class I, class II>
void test_pairhash()
{
    const constexpr size_t nums = 1'000;
    I A[nums] = {0}, B[nums] = {0};
    std::unordered_set<I> CH;
    std::unordered_map<II, std::pair<I, I>> ints;

    std::random_device rd;
    std::mt19937 gen(rd());

    const I Ibits   = int(sizeof(I) * CHAR_BIT);
    const II IIbits = int(sizeof(II) * CHAR_BIT);

    int bits = IIbits / 2 < Ibits ? Ibits / 2 : Ibits;
    if (std::is_signed<I>::value)
        bits -= 1;
    const I Imin = 0;
    const I Imax = I(std::pow(2., bits) - 1);

    std::uniform_int_distribution<I> dis(Imin, Imax);

    for (size_t i = 0; i < nums;) {
        I a = dis(gen);
        if (CH.find(a) == CH.end()) {
            CH.insert(a);
            A[i] = a;
            ++i;
        }
    }

    for (size_t i = 0; i < nums;) {
        I b = dis(gen);
        if (CH.find(b) == CH.end()) {
            CH.insert(b);
            B[i] = b;
            ++i;
        }
    }

    for (size_t i = 0; i < nums; ++i) {
        I a = A[i], b = B[i];

        REQUIRE(a != b);

        II hash_ab = Slic3r::sla::pairhash<I, II>(a, b);
        II hash_ba = Slic3r::sla::pairhash<I, II>(b, a);
        REQUIRE(hash_ab == hash_ba);

        auto it = ints.find(hash_ab);

        if (it != ints.end()) {
            REQUIRE(
                ((it->second.first == a && it->second.second == b)
                 || (it->second.first == b && it->second.second == a))
            );
        } else
            ints[hash_ab] = std::make_pair(a, b);
    }
}

TEST_CASE("Pillar pairhash should be unique", "[suptreeutils]")
{
    test_pairhash<int, int>();
    test_pairhash<int, long>();
    test_pairhash<unsigned, unsigned>();
    test_pairhash<unsigned, unsigned long>();
}

static void eval_ground_conn(
    const Slic3r::sla::GroundConnection& conn,
    const Slic3r::sla::SupportableMesh& sm,
    const Slic3r::sla::Junction& j,
    double end_r,
    const std::string& stl_fname = "output.stl"
)
{
    using namespace Slic3r;

    // #ifndef NDEBUG

    sla::SupportTreeBuilder builder;

    if (!conn)
        builder.add_junction(j);

    sla::build_ground_connection(builder, sm, conn);

    indexed_triangle_set mesh = *sm.emesh.get_triangle_mesh();
    Domain::its_merge(mesh, builder.merged_mesh());

    Biz::store_stl(stl_fname, Slic3r::Domain::TriangleMesh(std::move(mesh)), false);
    // #endif

    REQUIRE(bool(conn));

    // The route should include the source and one avoidance junction.
    REQUIRE(conn.path.size() == 2);

    // Check if the radius increases with each node
    REQUIRE(conn.path.front().r < conn.path.back().r);
    REQUIRE(conn.path.back().r < conn.pillar_base->r_top);

    // The end radius and the pillar base's upper radius should match
    REQUIRE(conn.pillar_base->r_top == Approx(end_r));
}

TEST_CASE("Pillar search dumb case", "[suptreeutils]")
{
    using namespace Slic3r;
    using Slic3r::Biz::Algorithms::Execution::ex_seq;

    constexpr double FromR = 0.5;
    auto j                 = sla::Junction{Vec3d::Zero(), FromR};

    SECTION("with empty mesh")
    {
        // A named local, not a temporary: the AABBMesh of a SupportableMesh is a view on
        // a triangle mesh and not a copy of it, so a temporary handed to it is freed
        // before the first query of the search that reads the mesh through it.
        const indexed_triangle_set empty_mesh;
        sla::SupportableMesh sm{.emesh = AABBMesh(empty_mesh)};

        constexpr double EndR = 1.;
        sla::GroundConnection conn = sla::deepsearch_ground_connection(ex_seq, sm, j, EndR, sla::DOWN);

        REQUIRE(conn);
        // REQUIRE(conn.path.size() == 1);
        REQUIRE(conn.pillar_base->pos.z() == Approx(ground_level(sm)));
    }

    SECTION("with zero R source and destination")
    {
        const indexed_triangle_set empty_mesh;
        sla::SupportableMesh sm{.emesh = AABBMesh(empty_mesh)};

        j.r                   = 0.;
        constexpr double EndR = 0.;
        sla::GroundConnection conn = sla::deepsearch_ground_connection(ex_seq, sm, j, EndR, sla::DOWN);

        REQUIRE(conn);
        // REQUIRE(conn.path.size() == 1);
        REQUIRE(conn.pillar_base->pos.z() == Approx(ground_level(sm)));
        REQUIRE(conn.pillar_base->r_top == Approx(0.));
    }

    SECTION("with zero init direction")
    {
        const indexed_triangle_set empty_mesh;
        sla::SupportableMesh sm{.emesh = AABBMesh(empty_mesh)};

        constexpr double EndR = 1.;
        Vec3d init_dir        = Vec3d::Zero();
        sla::GroundConnection conn = sla::deepsearch_ground_connection(ex_seq, sm, j, EndR, init_dir);

        REQUIRE(conn);
        // REQUIRE(conn.path.size() == 1);
        REQUIRE(conn.pillar_base->pos.z() == Approx(ground_level(sm)));
    }
}

TEST_CASE("Avoid disk below junction", "[suptreeutils]")
{
    using Slic3r::Biz::Algorithms::Execution::ex_tbb;

    // In this test there will be a disk mesh with some radius, centered at
    // (0, 0, 0) and above the disk, a junction from which the support pillar
    // should be routed. The algorithm needs to find an avoidance route.

    using namespace Slic3r;

    constexpr double FromRadius = .5;
    constexpr double EndRadius  = 1.;
    constexpr double CylRadius  = 4.;
    constexpr double CylHeight  = 1.;

    indexed_triangle_set disk = triangle_mesh::its_make_cylinder(CylRadius, CylHeight);

    // 2.5 * CyRadius height should be enough to be able to insert a bridge
    // with 45 degree tilt above the disk.
    sla::Junction j{Vec3d{0., 0., 2.5 * CylRadius}, FromRadius};

    sla::SupportableMesh sm{.emesh = AABBMesh(disk)};

    SECTION("with elevation")
    {
        sla::GroundConnection conn = sla::deepsearch_ground_connection(ex_tbb, sm, j, EndRadius, sla::DOWN);

        eval_ground_conn(conn, sm, j, EndRadius, "disk.stl");

        // Check if the avoidance junction is indeed outside of the disk barrier's
        // edge.
        auto p    = conn.path.back().pos;
        double pR = std::sqrt(p.x() * p.x()) + std::sqrt(p.y() * p.y());
        REQUIRE(pR + FromRadius > CylRadius);
    }

    SECTION("without elevation")
    {
        sm.cfg.object_elevation_mm = 0.;

        sla::GroundConnection conn = sla::deepsearch_ground_connection(ex_tbb, sm, j, EndRadius, sla::DOWN);

        eval_ground_conn(conn, sm, j, EndRadius, "disk_ze.stl");

        // Check if the avoidance junction is indeed outside of the disk barrier's
        // edge.
        auto p    = conn.path.back().pos;
        double pR = std::sqrt(p.x() * p.x()) + std::sqrt(p.y() * p.y());
        REQUIRE(pR + FromRadius > CylRadius);
    }
}

TEST_CASE("Avoid disk below junction with barrier on the side", "[suptreeutils]")
{
    using Slic3r::Biz::Algorithms::Execution::ex_seq;
    // In this test there will be a disk mesh with some radius, centered at
    // (0, 0, 0) and above the disk, a junction from which the support pillar
    // should be routed. The algorithm needs to find an avoidance route.

    using namespace Slic3r;

    constexpr double FromRadius = .5;
    constexpr double EndRadius  = 1.;
    constexpr double CylRadius  = 4.;
    constexpr double CylHeight  = 1.;
    constexpr double JElevX     = 2.5;

    sla::SupportTreeConfig cfg;

    indexed_triangle_set disk = triangle_mesh::its_make_cylinder(CylRadius, CylHeight);
    indexed_triangle_set wall = triangle_mesh::its_make_cube(1., 2 * CylRadius, JElevX * CylRadius);
    its_translate(wall, Vec3f{float(FromRadius), -float(CylRadius), 0.f});
    Domain::its_merge(disk, wall);

    // 2.5 * CyRadius height should be enough to be able to insert a bridge
    // with 45 degree tilt above the disk.
    sla::Junction j{Vec3d{0., 0., JElevX * CylRadius}, FromRadius};

    sla::SupportableMesh sm{.emesh = AABBMesh(disk)};

    SECTION("with elevation")
    {
        sla::GroundConnection conn = sla::deepsearch_ground_connection(ex_seq, sm, j, EndRadius, sla::DOWN);

        eval_ground_conn(conn, sm, j, EndRadius, "disk_with_barrier.stl");

        // Check if the avoidance junction is indeed outside of the disk barrier's
        // edge.
        auto p    = conn.path.back().pos;
        double pR = std::sqrt(p.x() * p.x()) + std::sqrt(p.y() * p.y());
        REQUIRE(pR + FromRadius > CylRadius);
    }

    SECTION("without elevation")
    {
        sm.cfg.object_elevation_mm = 0.;

        sla::GroundConnection conn = sla::deepsearch_ground_connection(ex_seq, sm, j, EndRadius, sla::DOWN);

        eval_ground_conn(conn, sm, j, EndRadius, "disk_with_barrier_ze.stl");

        // Check if the avoidance junction is indeed outside of the disk barrier's
        // edge.
        auto p    = conn.path.back().pos;
        double pR = std::sqrt(p.x() * p.x()) + std::sqrt(p.y() * p.y());
        REQUIRE(pR + FromRadius > CylRadius);
    }
}

TEST_CASE("Find ground route just above ground", "[suptreeutils]")
{
    using namespace Slic3r;
    using Slic3r::Biz::Algorithms::Execution::ex_seq;

    sla::SupportTreeConfig cfg;
    cfg.object_elevation_mm = 0.;

    sla::Junction j{Vec3d{0., 0., 2. * cfg.head_back_radius_mm}, cfg.head_back_radius_mm};

    const indexed_triangle_set empty_mesh;
    sla::SupportableMesh sm{.emesh = AABBMesh(empty_mesh)};
    sla::GroundConnection conn = sla::deepsearch_ground_connection(
        ex_seq,
        sm,
        j,
        Geometry::spheric_to_dir(3 * PI / 4, PI)
    );

    REQUIRE(conn);

    REQUIRE(conn.pillar_base->pos.z() >= Approx(ground_level(sm)));
}

TEST_CASE("BranchingSupports::MergePointFinder", "[suptreeutils]")
{
    using namespace Slic3r;

    SECTION("Identical points have the same merge point")
    {
        Vec3f a{0.f, 0.f, 0.f}, b = a;
        auto slope = float(PI / 4.);

        auto mergept = sla::find_merge_pt(a, b, slope);

        REQUIRE(bool(mergept));
        REQUIRE((*mergept - b).norm() < EPSILON);
        REQUIRE((*mergept - a).norm() < EPSILON);
    }

    // ^ Z
    // | a *
    // |
    // | b * <= mergept
    SECTION("Points at different heights have the lower point as mergepoint")
    {
        Vec3f a{0.f, 0.f, 0.f}, b = {0.f, 0.f, -1.f};
        auto slope = float(PI / 4.);

        auto mergept = sla::find_merge_pt(a, b, slope);

        REQUIRE(bool(mergept));
        REQUIRE((*mergept - b).squaredNorm() < 2 * EPSILON);
    }

    // -|---------> X
    // a       b
    // *       *
    // * <= mergept
    SECTION("Points at different X have mergept in the middle at lower Z")
    {
        Vec3f a{0.f, 0.f, 0.f}, b = {1.f, 0.f, 0.f};
        auto slope = float(PI / 4.);

        auto mergept = sla::find_merge_pt(a, b, slope);

        REQUIRE(bool(mergept));

        // Distance of mergept should be equal from both input points
        float D = std::abs((*mergept - b).squaredNorm() - (*mergept - a).squaredNorm());

        REQUIRE(D < EPSILON);
        REQUIRE(!sla::is_outside_support_cone(a, *mergept, slope));
        REQUIRE(!sla::is_outside_support_cone(b, *mergept, slope));
    }

    // -|---------> Y
    // a       b
    // *       *
    // * <= mergept
    SECTION("Points at different Y have mergept in the middle at lower Z")
    {
        Vec3f a{0.f, 0.f, 0.f}, b = {0.f, 1.f, 0.f};
        auto slope = float(PI / 4.);

        auto mergept = sla::find_merge_pt(a, b, slope);

        REQUIRE(bool(mergept));

        // Distance of mergept should be equal from both input points
        float D = std::abs((*mergept - b).squaredNorm() - (*mergept - a).squaredNorm());

        REQUIRE(D < EPSILON);
        REQUIRE(!sla::is_outside_support_cone(a, *mergept, slope));
        REQUIRE(!sla::is_outside_support_cone(b, *mergept, slope));
    }

    SECTION("Points separated by less than critical angle have the lower point as mergept")
    {
        Vec3f a{-1.f, -1.f, -1.f}, b = {-1.5f, -1.5f, -2.f};
        auto slope = float(PI / 4.);

        auto mergept = sla::find_merge_pt(a, b, slope);

        REQUIRE(bool(mergept));
        REQUIRE((*mergept - b).norm() < 2 * EPSILON);
    }

    // -|----------------------------> Y
    // a                          b
    // *            * <= mergept  *
    //
    SECTION("Points at same height have mergepoint in the middle if critical angle is zero ")
    {
        Vec3f a{-1.f, -1.f, -1.f}, b = {-1.5f, -1.5f, -1.f};
        auto slope = EPSILON;

        auto mergept = sla::find_merge_pt(a, b, slope);

        REQUIRE(bool(mergept));
        Vec3f middle = (b + a) / 2.;
        REQUIRE((*mergept - middle).norm() < 4 * EPSILON);
    }
}

TEST_CASE("Searches run beside each other and stay repeatable", "[suptreeutils]")
{
    namespace ex  = Slic3r::Biz::Algorithms::Execution;
    namespace opt = Slic3r::Biz::Algorithms::Optimize;

    // The genetic searches of the support tree draw from NLopt's generator,
    // which is seeded per search. NLopt keeps that generator per thread, so the
    // searches of different support points run in parallel and each of them
    // draws the numbers of its own seed - provided that no other search runs on
    // the same thread while it is not finished. That can happen: the objective
    // of a search is a model query, which is a loop under the TBB policy
    // (pinhead_mesh_hit, beam_mesh_hit), and a thread which waits for such a
    // loop may be handed a task of the support tree's own loop, which starts a
    // search (add_pinheads, routing_to_model). optimize() runs a search in an
    // isolated region so that the waiting thread is only handed the tasks of its
    // own search.

    SECTION("NLopt keeps a generator per thread")
    {
        // Otherwise every search takes nlopt_rng_lock() and the searches of the
        // support tree run one at a time: still repeatable, but slow.
        REQUIRE(opt::detail::nlopt_rng_is_thread_local());
    }

    SECTION("a search in every task of a parallel loop is the search it is alone")
    {
        // How many searches the calling thread is inside of, and how often a
        // search was started on a thread which already was inside one.
        static thread_local int depth = 0;
        std::atomic<int> nested{0};
        std::atomic<int> elsewhere{0};

        // A genetic search whose objective runs a loop of two under the TBB
        // policy. The first iteration waits a moment for the second one to be
        // taken by another thread, and the second one takes its time, so that
        // the thread that started the loop is left waiting for a task it cannot
        // run itself - which is when TBB hands a waiting thread other work.
        auto search = [&nested, &elsewhere](double shift) {
            opt::Optimizer<opt::AlgNLoptGenetic> solver(opt::StopCriteria{}.max_iterations(100));
            solver.seed(0);
            const std::thread::id owner = std::this_thread::get_id();

            return solver.to_min().optimize(
                [&nested, &elsewhere, shift, owner](const opt::Input<2> &input) {
                    if (depth++ > 0)
                        ++nested;

                    auto [x, y] = input;
                    std::atomic<bool> second_started{false};
                    std::array<double, 2> parts{};

                    ex::for_each(ex::ex_tbb, size_t(0), parts.size(), [&](size_t i) {
                        if (std::this_thread::get_id() != owner)
                            ++elsewhere;

                        if (i == 0) {
                            const auto until = std::chrono::steady_clock::now() +
                                               std::chrono::microseconds(500);
                            while (!second_started && std::chrono::steady_clock::now() < until) {}
                            parts[0] = std::sin(3. * x + shift);
                        } else {
                            second_started = true;
                            std::this_thread::sleep_for(std::chrono::microseconds(200));
                            parts[1] = std::cos(5. * y - shift);
                        }
                    });

                    --depth;
                    return parts[0] + parts[1] + 0.1 * (x * x + y * y);
                },
                opt::initvals({0., 0.}), opt::bounds({{-2., 2.}, {-2., 2.}}));
        };

        constexpr size_t tasks = 12;

        // Every search alone, one after the other.
        std::vector<opt::Result<2>> alone(tasks);
        for (size_t i = 0; i < tasks; ++i)
            alone[i] = search(0.1 * double(i));

        // The loop of the objective is a parallel loop inside a search, it is
        // not walked by the thread of the search alone.
        if (ex::max_concurrency(ex::ex_tbb) > 1)
            REQUIRE(elsewhere.load() > 0);

        // And every search again, all of them in the tasks of one parallel loop.
        std::vector<opt::Result<2>> together(tasks);
        for (size_t round = 0; round < 3; ++round) {
            ex::for_each(ex::ex_tbb, size_t(0), tasks,
                         [&](size_t i) { together[i] = search(0.1 * double(i)); });

            REQUIRE(nested.load() == 0);

            for (size_t i = 0; i < tasks; ++i) {
                INFO("round " << round << ", search " << i);
                REQUIRE(together[i].score == alone[i].score);
                REQUIRE(std::get<0>(together[i].optimum) == std::get<0>(alone[i].optimum));
                REQUIRE(std::get<1>(together[i].optimum) == std::get<1>(alone[i].optimum));
            }
        }
    }
}
