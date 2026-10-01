#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <test_utils.hpp>

#include <algorithm>
#include <array>
#include <atomic>
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

TEST_CASE("A search runs its own loops on its own thread", "[suptreeutils]")
{
    namespace ex  = Slic3r::Biz::Algorithms::Execution;
    namespace opt = Slic3r::Biz::Algorithms::Optimize;

    // Every search of the support tree holds the lock of the one process wide
    // NLopt generator (nlopt_rng_lock) from the seeding to the end of the search,
    // so that the same search on the same input is the same search twice. The
    // objective of such a search is a model query, and a model query is a loop
    // under the TBB policy (pinhead_mesh_hit, beam_mesh_hit shoot one ray per
    // sample of a ring). A thread that waits for a parallel loop may be handed
    // any task of its arena, and the tasks of the support tree's own loops start
    // a search of their own (add_pinheads, routing_to_model): this thread would
    // then ask that lock for the second time and std::mutex answers that with
    // "resource deadlock would occur". A loop started inside a search therefore
    // belongs to the thread which started it.
    constexpr size_t samples = 16;

    // One search of the shape the support tree builds: a loop of `samples` in the
    // objective, the score read out of what the loop wrote. `owner` is the thread
    // the search runs on and `elsewhere` is raised by any iteration of the loop
    // which did not run on it.
    auto search = [](const std::thread::id &owner, std::atomic<bool> &elsewhere, double x) {
        opt::StopCriteria criteria;
        criteria.abs_score_diff(1e-6).rel_score_diff(1e-4).max_iterations(30);

        opt::Optimizer<opt::AlgNLoptGenetic> solver(criteria);
        solver.seed(0);
        auto result = solver.to_max().optimize(
            [&x, owner, &elsewhere, samples](const opt::Input<3> &input) {
                std::array<double, samples> hits{};

                ex::for_each(ex::ex_tbb, size_t(0), hits.size(), [&](size_t i) {
                    if (std::this_thread::get_id() != owner)
                        elsewhere = true;
                    hits[i] = std::abs(x + double(i) - std::get<0>(input));
                });

                return *std::min_element(hits.begin(), hits.end());
            },
            opt::initvals({0., 0., 0.}),
            opt::bounds({{0., 1.}, {0., 1.}, {0., 1.}}));

        return result.score;
    };

    SECTION("the loop of the objective stays on the thread which started the search")
    {
        std::atomic<bool> elsewhere{false};
        const double score = search(std::this_thread::get_id(), elsewhere, 3.);

        REQUIRE(!elsewhere.load());
        REQUIRE(score <= 4.);
    }

    SECTION("the guard of a search does not outlive it")
    {
        REQUIRE(!ex::in_sequential_region());

        std::atomic<bool> elsewhere{false};
        search(std::this_thread::get_id(), elsewhere, 3.);

        REQUIRE(!ex::in_sequential_region());
    }

    SECTION("a search of every task of a parallel loop runs to the end")
    {
        // The shape that used to throw: the tasks of a parallel loop each start a
        // search, every search runs a loop of its own, and the lock of the
        // generator is held while that loop runs.
        constexpr size_t tasks = 12, rounds = 3;

        std::atomic<bool> elsewhere{false};
        std::atomic<size_t> finished{0};
        std::vector<double> scores(tasks * rounds, 0.);

        for (size_t round = 0; round < rounds; ++round) {
            ex::for_each(
                ex::ex_tbb, size_t(0), tasks,
                [&](size_t i) {
                    scores[round * tasks + i] = search(std::this_thread::get_id(), elsewhere, 3.);
                    finished.fetch_add(1);
                },
                ex::max_concurrency(ex::ex_tbb));
        }

        REQUIRE(finished.load() == tasks * rounds);
        REQUIRE(!elsewhere.load());

        // The lock is what makes a search repeatable, and nothing of that may be
        // given up for it: every search of the same input is the same search.
        for (size_t i = 0; i < scores.size(); ++i)
            REQUIRE(scores[i] == scores[0]);
    }
}
