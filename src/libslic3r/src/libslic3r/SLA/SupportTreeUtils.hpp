#ifndef SLASUPPORTTREEUTILS_H
#define SLASUPPORTTREEUTILS_H

#include <algorithm>
#include <cstdint>
#include <optional>

#include "Slic3r/Biz/Algorithms/Execution/Execution.hpp"
#include <Slic3r/Biz/Algorithms/Optimize/NLoptOptimizer.hpp>
#include <Slic3r/Biz/Algorithms/Optimize/BruteforceOptimizer.hpp>
#include <libslic3r/MeshNormals.hpp>
#include <libslic3r/Geometry.hpp>
#include <libslic3r/SLA/JobController.hpp>
#include <libslic3r/SLA/SupportTreeBuilder.hpp>

#include <boost/variant.hpp>
#include <boost/container/small_vector.hpp>

namespace Slic3r { namespace sla {

using Slic3r::Biz::Algorithms::Optimize::initvals;
using Slic3r::Biz::Algorithms::Optimize::bounds;
using Slic3r::Biz::Algorithms::Optimize::StopCriteria;
using Slic3r::Biz::Algorithms::Optimize::Optimizer;
using Slic3r::Biz::Algorithms::Optimize::AlgNLoptSubplex;
using Slic3r::Biz::Algorithms::Optimize::AlgNLoptGenetic;
using Slic3r::Geometry::dir_to_spheric;
using Slic3r::Geometry::spheric_to_dir;

// Give points on a 3D ring with given center, radius and orientation
// method based on:
// https://math.stackexchange.com/questions/73237/parametric-equation-of-a-circle-in-3d-space
template<size_t N>
class PointRing {
    std::array<double, N - 1> m_phis;

    // Two vectors that will be perpendicular to each other and to the
    // axis. Values for a(X) and a(Y) are now arbitrary, a(Z) is just a
    // placeholder.
    // a and b vectors are perpendicular to the ring direction and to each other.
    // Together they define the plane where we have to iterate with the
    // given angles in the 'm_phis' vector
    Vec3d a = {0, 1, 0}, b;
    double m_radius = 0.;

    static inline bool constexpr is_one(double val)
    {
        constexpr double eps = 1e-20;

        return std::abs(std::abs(val) - 1) < eps;
    }

public:

    PointRing(const Vec3d &n) : m_phis{linspace_array<N - 1>(0., 2 * PI)}
    {
        // We have to address the case when the direction vector v (same as
        // dir) is coincident with one of the world axes. In this case two of
        // its components will be completely zero and one is 1.0. Our method
        // becomes dangerous here due to division with zero. Instead, vector
        // 'a' can be an element-wise rotated version of 'v'
        if(is_one(n(X)) || is_one(n(Y)) || is_one(n(Z))) {
            a = Vec3d{n(Z), n(X), n(Y)};
            b = Vec3d{n(Y), n(Z), n(X)};
        }
        else {
            a(Z) = -(n(Y)*a(Y)) / n(Z); a.normalize();
            b = a.cross(n);
        }
    }

    Vec3d get(size_t idx, const Vec3d &src, double r) const
    {
        if (idx == 0)
            return src;

        double phi = m_phis[idx - 1];
        double sinphi = std::sin(phi);
        double cosphi = std::cos(phi);

        double rpscos = r * cosphi;
        double rpssin = r * sinphi;

        // Point on the sphere
        return {src(X) + rpscos * a(X) + rpssin * b(X),
                src(Y) + rpscos * a(Y) + rpssin * b(Y),
                src(Z) + rpscos * a(Z) + rpssin * b(Z)};
    }
};

template<class T, int N>
LegacyVec<N, T> dirv(const LegacyVec<N, T>& startp, const LegacyVec<N, T>& endp) {
    return (endp - startp).normalized();
}

using Hit = AABBMesh::hit_result;

template<class It> Hit min_hit(It from, It to)
{
    auto mit = std::min_element(from, to, [](const Hit &h1, const Hit &h2) {
        return h1.distance() < h2.distance();
    });

    return *mit;
}

// The defaults of a support tree search: the tolerances and the iteration count of the config.
inline StopCriteria get_criteria(const SupportTreeConfig &cfg)
{
    return StopCriteria{}
        .rel_score_diff(cfg.optimizer_rel_score_diff)
        .max_iterations(cfg.optimizer_max_iterations);
}

// The same, with the stop condition of the job the tree is built for (M4.16). NLopt reads the stop
// condition of the criteria before every evaluation of the objective and forces the search to stop
// when it says so, so a search checks between its iterations and not only between the heads it is
// run for: since M4.5c the searches of the different leaves run one after the other and the whole
// run hangs on one of them otherwise. The predicate is only asked, never stored differently, so a
// build nobody stopped searches exactly what it searched before.
//
// A caller that cannot be stopped passes detail::never_stop, the named function of JobController
// and not a lambda: MSVC mangles the lambdas of a default function argument with a counter that is
// per namespace, so the same mangled name can stand for different lambdas in different translation
// units and the linker keeps one body, and a stop condition that then reads whatever was left in the
// return register would abort every build of a support tree.
inline StopCriteria get_criteria(const SupportTreeConfig &cfg, JobController::StopCond stopcond)
{
    StopCriteria criteria = get_criteria(cfg);
    if (stopcond)
        criteria.stop_condition(stopcond);
    return criteria;
}

// A simple sphere with a center and a radius
struct Ball { Vec3d p; double R; };

template<size_t Samples = 8>
struct Beam_ { // Defines a set of rays displaced along a cone's surface
    static constexpr size_t SAMPLES = Samples;

    Vec3d src;
    Vec3d dir;
    double r1;
    double r2; // radius of the beam 1 unit further from src in dir direction

    Beam_(const Vec3d &s, const Vec3d &d, double R1, double R2)
        : src{s}, dir{d}, r1{R1}, r2{R2} {};

    Beam_(const Ball &src_ball, const Ball &dst_ball)
        : src{src_ball.p}, dir{dirv(src_ball.p, dst_ball.p)}, r1{src_ball.R}
    {
        r2 = src_ball.R;
        auto d = distance(src_ball.p, dst_ball.p);

        if (d > EPSILON)
            r2 += (dst_ball.R - src_ball.R) / d;
    }

    Beam_(const Vec3d &s, const Vec3d &d, double R)
        : src{s}, dir{d}, r1{R}, r2{R}
    {}
};

using Beam = Beam_<>;

template<class Ex, size_t RayCount = Beam::SAMPLES>
Hit beam_mesh_hit(Ex policy,
                  const AABBMesh &mesh,
                  const Beam_<RayCount> &beam,
                  double sd)
{
    namespace execution = Slic3r::Biz::Algorithms::Execution;

    Vec3d src = beam.src;
    Vec3d dst = src + beam.dir;
    double r_src = beam.r1;
    double r_dst = beam.r2;

    Vec3d D = (dst - src);
    Vec3d dir = D.normalized();
    PointRing<RayCount>  ring{dir};

    using Hit = AABBMesh::hit_result;

    // Hit results
    std::array<Hit, RayCount> hits;

    execution::for_each(
        policy, size_t(0), hits.size(),
        [&mesh, r_src, r_dst, src, dst, &ring, dir, sd, &hits](size_t i) {
            Hit &hit = hits[i];

            // Point on the circle on the pin sphere
            Vec3d p_src = ring.get(i, src, r_src + sd);
            Vec3d p_dst = ring.get(i, dst, r_dst + sd);
            Vec3d raydir = (p_dst - p_src).normalized();

            auto hr = mesh.query_ray_hit(p_src + r_src * raydir, raydir);

            if (hr.is_inside()) {
                if (hr.distance() > 2 * r_src + sd)
                    hit = Hit(0.0);
                else {
                    // re-cast the ray from the outside of the object
                    auto q = p_src + (hr.distance() + EPSILON) * raydir;
                    hit = mesh.query_ray_hit(q, raydir);
                }
            } else
                hit = hr;
        }, std::min(execution::max_concurrency(policy), RayCount));

    return min_hit(hits.begin(), hits.end());
}

template<class Ex>
Hit pinhead_mesh_hit(Ex              ex,
                     const AABBMesh &mesh,
                     const Vec3d    &s,
                     const Vec3d    &dir,
                     double          r_pin,
                     double          r_back,
                     double          width,
                     double          sd)
{
    namespace execution = Slic3r::Biz::Algorithms::Execution;

    // Support tree generation speed depends heavily on this value. 8 is almost
    // ok, but to prevent rare cases of collision, 16 is necessary, which makes
    // the algorithm run about 60% longer.
    static const size_t SAMPLES = 16;

    // Move away slightly from the touching point to avoid raycasting on the
    // inner surface of the mesh.

    auto &m         = mesh;
    using HitResult = AABBMesh::hit_result;

       // Hit results
    std::array<HitResult, SAMPLES> hits;

    struct Rings
    {
        double             rpin;
        double             rback;
        Vec3d              spin;
        Vec3d              sback;
        PointRing<SAMPLES> ring;

        Vec3d backring(size_t idx) { return ring.get(idx, sback, rback); }
        Vec3d pinring(size_t idx) { return ring.get(idx, spin, rpin); }
    } rings{r_pin + sd, r_back + sd, s, s + (r_pin + width + r_back) * dir, dir};

    // We will shoot multiple rays from the head pinpoint in the direction
    // of the pinhead robe (side) surface. The result will be the smallest
    // hit distance.

    execution::for_each(
        ex, size_t(0), hits.size(), [&m, &rings, sd, &hits](size_t i) {
            // Point on the circle on the pin sphere
            Vec3d ps = rings.pinring(i);
            // This is the point on the circle on the back sphere
            Vec3d p = rings.backring(i);

            auto &hit = hits[i];

               // Point ps is not on mesh but can be inside or
               // outside as well. This would cause many problems
               // with ray-casting. To detect the position we will
               // use the ray-casting result (which has an is_inside
               // predicate).

            Vec3d n = (p - ps).normalized();
            auto  q = m.query_ray_hit(ps + sd * n, n);

            if (q.is_inside()) { // the hit is inside the model
                if (q.distance() > rings.rpin) {
                    // If we are inside the model and the hit
                    // distance is bigger than our pin circle
                    // diameter, it probably indicates that the
                    // support point was already inside the
                    // model, or there is really no space
                    // around the point. We will assign a zero
                    // hit distance to these cases which will
                    // enforce the function return value to be
                    // an invalid ray with zero hit distance.
                    // (see min_element at the end)
                    hit = HitResult(0.0);
                } else {
                    // re-cast the ray from the outside of the
                    // object. The starting point has an offset
                    // of 2*safety_distance because the
                    // original ray has also had an offset
                    auto q2 = m.query_ray_hit(ps + (q.distance() + 2 * sd) * n, n);
                    hit     = q2;
                }
            } else
                hit = q;
        }, std::min(execution::max_concurrency(ex), SAMPLES));

    return min_hit(hits.begin(), hits.end());
}

template<class Ex>
Hit pinhead_mesh_hit(Ex              ex,
                     const AABBMesh &mesh,
                     const Head     &head,
                     double          safety_d)
{
    return pinhead_mesh_hit(ex, mesh, head.pos, head.dir, head.r_pin_mm,
                            head.r_back_mm, head.width_mm, safety_d);
}

inline double distance(const Domain::SLA::SupportPoint &a, const Domain::SLA::SupportPoint &b)
{
    return (a.pos - b.pos).norm();
}

template<class PtIndex>
std::vector<size_t> non_duplicate_suppt_indices(const PtIndex &index,
                                                const Domain::SLA::SupportPoints &suppts,
                                                double         eps)
{
    std::vector<bool> to_remove(suppts.size(), false);

    for (size_t i = 0; i < suppts.size(); ++i) {
        size_t closest_idx =
            find_closest_point(index, suppts[i].pos,
                               [&i, &to_remove](size_t i_closest) {
                                   return i_closest != i &&
                                          !to_remove[i_closest];
                               });

        if (closest_idx < suppts.size() &&
            (suppts[i].pos - suppts[closest_idx].pos).norm() < eps)
            to_remove[i] = true;
    }

    auto ret = reserve_vector<size_t>(suppts.size());
    for (size_t i = 0; i < to_remove.size(); i++)
        if (!to_remove[i])
            ret.emplace_back(i);

    return ret;
}

// The radius of the pillar that carries this support point's head. Points with
// a per point pillar diameter (the Light/Medium/Heavy support presets) get
// their own pillar width, bounded by the global config: never thinner than the
// fallback radius the algorithm may shrink a head to and never fatter than a
// small multiple of the configured pillar radius. Points without an override
// keep the globally configured width.
inline double head_back_radius(const SupportableMesh            &sm,
                               const Domain::SLA::SupportPoint &sp)
{
    if (!(sp.pillar_diameter > 0.f))
        return sm.cfg.head_back_radius_mm;

    const double hi = sm.cfg.head_back_radius_limit_mm();
    const double lo = std::min(sm.cfg.head_fallback_radius_mm, hi);

    return std::clamp(0.5 * double(sp.pillar_diameter), lo, hi);
}

// How deep the head of this support point sinks into the model. Points with a
// per point contact depth (the support presets) get their own value, the others
// the globally configured one.
inline double head_penetration(const SupportableMesh            &sm,
                               const Domain::SLA::SupportPoint &sp)
{
    return sp.contact_depth > 0.f ? double(sp.contact_depth)
                                   : sm.cfg.head_penetration_mm;
}

// The base (pedestal) a pillar gets where it reaches the ground: the support
// point's own diameter, height and shape when it carries a per point size (the
// support presets), the global config otherwise.
struct BaseSize
{
    double radius = 0.;
    double height = 0.;
    Domain::sla::SupportBaseShape shape = Domain::sla::SupportBaseShape::Cone;
};

// The shape of the foot of this support point: its own, or the globally configured one when
// the point does not ask for a shape of its own.
inline Domain::sla::SupportBaseShape base_shape_of(const Domain::SLA::SupportPoint &sp)
{
    switch (sp.base_shape) {
    case Domain::SLA::SupportPoint::BaseShape::Cone:
        return Domain::sla::SupportBaseShape::Cone;
    case Domain::SLA::SupportPoint::BaseShape::Cylinder:
        return Domain::sla::SupportBaseShape::Cylinder;
    case Domain::SLA::SupportPoint::BaseShape::Flat:
        return Domain::sla::SupportBaseShape::Flat;
    case Domain::SLA::SupportPoint::BaseShape::Default:
        break;
    }

    return Domain::sla::SupportBaseShape::Cone;
}

inline BaseSize base_size(const SupportableMesh            &sm,
                          const Domain::SLA::SupportPoint *sp)
{
    BaseSize ret{sm.cfg.base_radius_mm, sm.cfg.base_height_mm, sm.cfg.base_shape};

    if (sp != nullptr) {
        if (sp->base_diameter > 0.f) ret.radius = 0.5 * double(sp->base_diameter);
        if (sp->base_height > 0.f) ret.height = double(sp->base_height);
        if (sp->base_shape != Domain::SLA::SupportPoint::BaseShape::Default)
            ret.shape = base_shape_of(*sp);
    }

    return ret;
}

// The cross section and the taper of the pillar a support point asks for
// (M2.16b). A stem side count below 3 or above 12 is not a polygon this mesh
// builder makes, so it falls back to the round pillar. Without a stem of its own
// the point gets the round, untapered pillar, which is what every point got
// before the per point stem geometry existed.
inline StemGeometry stem_geometry(const SupportableMesh            &sm,
                                  const Domain::SLA::SupportPoint *sp)
{
    StemGeometry ret;
    ret.min_radius_mm = sm.cfg.head_fallback_radius_mm;

    if (sp == nullptr)
        return ret;

    if (sp->stem_sides >= 3 && sp->stem_sides <= 12)
        ret.sides = sp->stem_sides;

    // The taper is in mm of radius over the pillar: a negative or missing value
    // means no taper at all.
    if (sp->stem_taper > 0.f)
        ret.taper_mm = double(sp->stem_taper);

    return ret;
}

// Whether the pillar of this support point may end on the model surface instead of
// having to reach the build plate (M2.26). Without a switch of its own the point
// follows the object's support_buildplate_only, which is what every point did
// before the switch existed. Allow permits a model anchor even on an object whose
// supports must not end on the model, Forbid forbids it even on an object whose
// supports may.
inline bool may_rest_on_model(const SupportableMesh            &sm,
                              const Domain::SLA::SupportPoint *sp)
{
    bool ret = !sm.cfg.ground_facing_only;

    if (sp != nullptr) {
        switch (sp->on_model) {
        case Domain::SLA::SupportPoint::OnModel::Allow: ret = true; break;
        case Domain::SLA::SupportPoint::OnModel::Forbid: ret = false; break;
        case Domain::SLA::SupportPoint::OnModel::Inherit:
        default: break;
        }
    }

    return ret;
}

// Whether any point of the object may end on the model. The trees ask this before
// they sample the model surface as a place to anchor a support on (M2.26): a point
// that allows a model anchor of its own needs the model points even on an object
// whose supports must not end on the model.
inline bool any_may_rest_on_model(const SupportableMesh &sm)
{
    return sm.pts != nullptr &&
           std::any_of(sm.pts->begin(), sm.pts->end(),
                       [&sm](const Domain::SLA::SupportPoint &sp) {
                           return may_rest_on_model(sm, &sp);
                       });
}

// The stop condition is the one of the job the tree is built for (M4.16): the search below is one
// per support point, and a search that is not given up when the tool is asked to stop holds the
// whole tool up. detail::never_stop leaves it searching exactly what it searched before.
template<class Ex>
bool optimize_pinhead_placement(Ex                     policy,
                                const SupportableMesh &m,
                                Head                  &head,
                                JobController::StopCond stopcond = &detail::never_stop)
{
    Vec3d n = get_normal(m.emesh, head.pos);
    assert(std::abs(n.norm() - 1.0) < EPSILON);

    // for all normals the spherical coordinates are generated and
    // the polar angle is saturated to 45 degrees from the bottom then
    // converted back to standard coordinates to get the new normal.
    // Then a simple quaternion is created from the two normals
    // (Quaternion::FromTwoVectors) and the rotation is applied to the
    // pinhead.

    auto [polar, azimuth] = dir_to_spheric(n);

    double back_r = head.r_back_mm;

    // skip if the tilt is not sane
    if (polar < PI - m.cfg.normal_cutoff_angle) return false;

    // We saturate the polar angle to 3pi/4
    polar = std::max(polar, PI - m.cfg.bridge_slope);

    // save the head (pinpoint) position
    Vec3d hp = head.pos;

    double lmin = head.width_mm > 0. ? head.width_mm : m.cfg.head_width_mm, lmax = lmin;

    if (back_r < m.cfg.head_back_radius_mm) {
        lmin = 0., lmax = head.penetration_mm;
    }

    // The distance needed for a pinhead to not collide with model. Every tip shape
    // of M2.16b reaches as far into the model as the default one, so this does
    // not change with them.
    double w = lmin + 2 * back_r + 2 * head.r_pin_mm - head.penetration_mm;

    double pin_r = head.r_pin_mm;

    // Reassemble the now corrected normal
    auto nn = spheric_to_dir(polar, azimuth).normalized();

    double sd = m.cfg.safety_distance(back_r);

    // check available distance
    Hit t = pinhead_mesh_hit(policy, m.emesh, hp, nn, pin_r, back_r, w, sd);

    if (t.distance() < w) {
        // Let's try to optimize this angle, there might be a
        // viable normal that doesn't collide with the model
        // geometry and its very close to the default.

        Optimizer<Biz::Algorithms::Optimize::AlgNLoptMLSL_Subplx> solver(
            get_criteria(m.cfg, stopcond).stop_score(w).max_iterations(100));
        solver.seed(0); // we want deterministic behavior

        auto oresult = solver.to_max().optimize(
            [&m, pin_r, back_r, hp, sd, policy](const Biz::Algorithms::Optimize::Input<3> &input) {
                auto &[plr, azm, l] = input;

                auto dir = spheric_to_dir(plr, azm).normalized();

                return pinhead_mesh_hit(policy, m.emesh, hp, dir, pin_r,
                                              back_r, l, sd).distance();
            },
            initvals({polar, azimuth,
                      (lmin + lmax) / 2.}), // start with what we have
            bounds({{PI - m.cfg.bridge_slope, PI}, // Must not exceed the slope limit
                    {-PI, PI}, // azimuth can be a full search
                    {lmin, lmax}}));

        if(oresult.score > w) {
            polar = std::get<0>(oresult.optimum);
            azimuth = std::get<1>(oresult.optimum);
            nn = spheric_to_dir(polar, azimuth).normalized();
            lmin = std::get<2>(oresult.optimum);
            t = AABBMesh::hit_result(oresult.score);
        }
    }

    bool ret = false;
    if (t.distance() > w && hp.z() + w * nn.z() >= ground_level(m)) {
        head.dir       = nn;
        head.width_mm  = lmin;
        head.r_back_mm = back_r;

        ret = true;
    } else if (back_r > m.cfg.head_fallback_radius_mm) {
        head.r_back_mm = m.cfg.head_fallback_radius_mm;
        ret = optimize_pinhead_placement(policy, m, head, stopcond);
    }

    return ret;
}

template<class Ex>
std::optional<Head> calculate_pinhead_placement(Ex                     policy,
                                                const SupportableMesh &sm,
                                                size_t suppt_idx,
                                                JobController::StopCond stopcond = &detail::never_stop)
{
    if (suppt_idx >= sm.pts->size())
        return {};

    const Domain::SLA::SupportPoint &sp = sm.pts->at(suppt_idx);

    // The length of the tapered tip: the one the point carries, else the
    // configured default (support_tip_length), which is what the default tree
    // does with the same two values. Zero there keeps the pinhead width, which
    // is the width this tree has always built the head with. (M2.27)
    double tip_length = sp.tip_length > 0.f ? double(sp.tip_length) : sm.cfg.tip_length_mm;

    Head head{
        head_back_radius(sm, sp),
        sp.head_front_radius,
        tip_length,
        head_penetration(sm, sp),
        Vec3d::Zero(),        // dir
        sp.pos.cast<double>() // displacement
    };

    // The rest of the per point geometry of the point (M2.16b): the shape of
    // the contact, an optional knot ball at the junction and the cross section
    // and taper of the pillar that carries this head.
    head.tip_shape      = sp.tip_shape;
    head.knot_radius_mm = double(sp.knot_radius);
    head.stem           = stem_geometry(sm, &sp);

    if (optimize_pinhead_placement(policy, sm, head, stopcond)) {
        head.id = long(suppt_idx);

        return head;
    }

    return {};
}

struct GroundConnection {
    // Currently, a ground connection will contain at most 2 additional junctions
    // which will not require any allocations. If I come up with an algo that
    // can produce a route to ground with more junctions, this design will be
    // able to handle that.
    static constexpr size_t MaxExpectedJunctions = 3;

    boost::container::small_vector<Junction, MaxExpectedJunctions> path;
    std::optional<Pedestal> pillar_base;

    // The radius at which the pillar of this connection counts as a full one:
    // it reaches the pad and may carry a base. A support point with a per point
    // pillar diameter (the support presets) brings its own, zero means the
    // globally configured one. See head_back_radius().
    double full_radius = 0.;

    // The cross section and the taper of the pillar of this connection, from
    // the support point the leaf it starts from. (M2.16b)
    StemGeometry stem;

    operator bool() const { return pillar_base.has_value() && !path.empty(); }
};

// The radius a connection's pillar counts as full at.
inline double full_radius(const SupportableMesh &sm, const GroundConnection &conn)
{
    return conn.full_radius > 0. ? conn.full_radius : sm.cfg.head_back_radius_mm;
}

inline long build_ground_connection(SupportTreeBuilder &builder,
                                    const SupportableMesh &sm,
                                    const GroundConnection &conn)
{
    long ret = SupportTreeNode::ID_UNSET;

    if (!conn)
        return ret;

    auto it = conn.path.begin();
    auto itnx = std::next(it);

    while (itnx != conn.path.end()) {
        builder.add_diffbridge(*it, *itnx);
        builder.add_junction(*itnx);
        ++it; ++itnx;
    }

    auto gp = conn.path.back().pos;
    gp.z() = ground_level(sm);
    double h = conn.path.back().pos.z() - gp.z();

    const double full_r = full_radius(sm, conn);

    if (conn.pillar_base->r_top < full_r) {
        h += sm.pad_cfg.wall_thickness_mm;
        gp.z() -= sm.pad_cfg.wall_thickness_mm;
    }

// TODO: does not work yet
//    if (conn.path.back().id < 0) {
//        // this is a head
//        long head_id = std::abs(conn.path.back().id);
//        ret = builder.add_pillar(head_id, h);
//    } else

    // This tree widens a pillar towards the plate, so the cone it ends in is wider than the
    // pillar above it. A flat disc foot is meant to be the whole foot, so the widening is
    // skipped for it and the pillar runs straight down into the disc (M2.23c). add_pillar_base
    // makes the disc at least as wide as that pillar, so the disc is the widest part of it.
    const double end_r = conn.pillar_base->shape == Domain::sla::SupportBaseShape::Flat ?
                             conn.path.back().r : conn.pillar_base->r_top;

    ret = builder.add_pillar(gp, h, conn.path.back().r, end_r, conn.stem);

    if (conn.pillar_base->r_top >= full_r)
        builder.add_pillar_base(ret, conn.pillar_base->height, conn.pillar_base->r_bottom,
                                conn.pillar_base->shape);

    return ret;
}

template<class Fn>
constexpr bool IsWideningFn = std::is_invocable_r_v</*retval*/ double,
                                                    Fn,
                                                    Ball /*source*/,
                                                    Vec3d /*dir*/,
                                                    double /*length*/>;

// A widening function can determine how many ray samples should a beam contain
// (see in beam_mesh_hit)
template<class WFn> struct BeamSamples { static constexpr size_t Value = 8; };
template<class WFn> constexpr size_t BeamSamplesV = BeamSamples<std::remove_cvref_t<WFn>>::Value;

// To use with check_ground_route, full will check the bridge and the pillar,
// PillarOnly checks only the pillar for collisions.
enum class GroundRouteCheck { Full, PillarOnly };

// Returns the collision point with mesh if there is a collision or a ground point,
// given a source point with a direction of a potential avoidance bridge
// and a bridge length.
// 'base_radius' is the radius of the base the pillar below will stand on, which is
// the one of the support point the route starts from where it has one (M2.12), so
// the gap the base needs in zero elevation mode is kept clear of the model too.
// Left at zero the globally configured base radius is used, which is what the tree
// built before the per point base diameter existed.
template<class Ex, class WideningFn,
         class = std::enable_if_t<IsWideningFn<WideningFn>> >
Vec3d check_ground_route(
    Ex                     policy,
    const SupportableMesh &sm,
    const Junction        &source,      // source location
    const Vec3d           &dir,         // direction of the bridge from the source
    double                bridge_len,   // lenght of the avoidance bridge
    WideningFn            &&wideningfn, // Widening strategy
    GroundRouteCheck      type = GroundRouteCheck::Full,
    double                base_radius = 0.
    )
{
    static const constexpr auto Samples = BeamSamplesV<WideningFn>;

    Vec3d ret;

    const auto sd     = sm.cfg.safety_distance(source.r);
    const auto gndlvl = ground_level(sm);
    const double base_r_cfg = base_radius > 0. ? base_radius : sm.cfg.base_radius_mm;

    // Intersection of the suggested bridge with ground plane. If the bridge
    // spans below ground, stop it at ground level.
    double t = (gndlvl - source.pos.z()) / dir.z();
    bridge_len = std::min(t, bridge_len);

    Vec3d bridge_end = source.pos + bridge_len * dir;

    double down_l     = bridge_end.z() - gndlvl;
    double bridge_r   = wideningfn(Ball{source.pos, source.r}, dir, bridge_len);
    double brhit_dist = 0.;

    if (bridge_len > EPSILON && type == GroundRouteCheck::Full) {
        // beam_mesh_hit with a zero lenght bridge is invalid

        Beam_<Samples> bridgebeam{Ball{source.pos, source.r},
                                  Ball{bridge_end, bridge_r}};

        auto brhit = beam_mesh_hit(policy, sm.emesh, bridgebeam, sd);
        brhit_dist = brhit.distance();
    } else {
        brhit_dist = bridge_len;
    }

    if (brhit_dist < bridge_len) {
        ret = (source.pos + brhit_dist * dir);
    } else if (down_l > 0.) {
        // check if pillar can be placed below
        auto   gp         = Vec3d{bridge_end.x(), bridge_end.y(), gndlvl};
        double end_radius = wideningfn(
            Ball{bridge_end, bridge_r}, DOWN, bridge_end.z() - gndlvl);

        Beam_<Samples> gndbeam {{bridge_end, bridge_r}, {gp, end_radius}};
        auto gndhit = beam_mesh_hit(policy, sm.emesh, gndbeam, sd);
        double gnd_hit_d = std::min(gndhit.distance(), down_l + EPSILON);

        if (source.r >= sm.cfg.head_back_radius_mm && gndhit.distance() > down_l && sm.cfg.object_elevation_mm < EPSILON) {
            // Dealing with zero elevation mode, to not route pillars
            // into the gap between the optional pad and the model
            double gap     = std::sqrt(sm.emesh.squared_distance(gp));
            double base_r  = std::max(base_r_cfg, end_radius);
            double min_gap = sm.cfg.pillar_base_safety_distance_mm + base_r;

            if (gap < min_gap) {
                gnd_hit_d = down_l - min_gap + gap;
            }
        }

        ret = Vec3d{bridge_end.x(), bridge_end.y(), bridge_end.z() - gnd_hit_d};
    } else {
        ret = bridge_end;
    }

    return ret;
}

// Searching a ground connection from an arbitrary source point.
// Currently, the result will contain one avoidance bridge (at most) and a
// pillar to the ground, if it's feasible.
// 'sp' is the support point the connection starts from, or nullptr when it
// starts from somewhere the per point sizes (the support presets) do not
// apply. It gives the base its own size and tells the builder which radius
// counts as a full pillar.
template<class Ex, class WideningFn,
         class = std::enable_if_t<IsWideningFn<WideningFn>> >
GroundConnection deepsearch_ground_connection(
    Ex                              policy,
    const SupportableMesh          &sm,
    const Junction                 &source,
    WideningFn                     &&wideningfn,
    const Vec3d                    &init_dir = DOWN,
    const Domain::SLA::SupportPoint *sp       = nullptr,
    JobController::StopCond          stopcond = &detail::never_stop)
{
    constexpr unsigned MaxIterationsGlobal = 5000;
    constexpr unsigned MaxIterationsLocal  = 100;
    constexpr double   RelScoreDiff        = 0.05;

    const auto gndlvl = ground_level(sm);

    // The used solver (AlgNLoptMLSL_Subplx search method) is composed of a global (MLSL)
    // and a local (Subplex) search method. Criteria can be set in a way that
    // local searches are quick and less accurate. The global method will only
    // consider the max iteration number and the stop score (Z level <= ground)

    auto criteria = get_criteria(sm.cfg, stopcond); // get defaults from cfg
    criteria.max_iterations(MaxIterationsGlobal);
    criteria.abs_score_diff(NaNd);
    criteria.rel_score_diff(NaNd);
    criteria.stop_score(gndlvl);

    auto criteria_loc = criteria;
    criteria_loc.max_iterations(MaxIterationsLocal);
    criteria_loc.abs_score_diff(EPSILON);
    criteria_loc.rel_score_diff(RelScoreDiff);

    Optimizer<Biz::Algorithms::Optimize::AlgNLoptMLSL_Subplx> solver(criteria);
    solver.set_loc_criteria(criteria_loc);
    solver.seed(0); // require repeatability

    // The base the pillar below this route will stand on: the one of the support
    // point the connection starts from, the globally configured one otherwise.
    // (M2.27, see check_ground_route and base_size)
    const BaseSize base = base_size(sm, sp);

    // functor returns the z height of collision point, given a polar and
    // azimuth angles as bridge direction and bridge length. The route is
    // traced from source, through this bridge and an attached pillar. If there
    // is a collision with the mesh, the Z height is returned. Otherwise the
    // z level of ground is returned.
    auto z_fn = [&](const Biz::Algorithms::Optimize::Input<3> &input) {
        // solver suggests polar, azimuth and bridge length values:
        auto &[plr, azm, bridge_len] = input;

        Vec3d n = spheric_to_dir(plr, azm);

        Vec3d hitpt = check_ground_route(policy, sm, source, n, bridge_len,
                                         wideningfn, GroundRouteCheck::Full,
                                         base.radius);

        return hitpt.z();
    };

    // Calculate the initial direction of the search by
    // saturating the polar angle to max tilt defined in config
    auto [plr_init, azm_init] = dir_to_spheric(init_dir);
    plr_init = std::max(plr_init, PI - sm.cfg.bridge_slope);

    auto bound_constraints =
        bounds({
                {PI - sm.cfg.bridge_slope, PI},   // bounds for polar angle
                {-PI, PI},                        // bounds for azimuth
                {0., sm.cfg.max_bridge_length_mm} // bounds bridge length
        });

    // The optimizer can navigate fairly well on the mesh surface, finding
    // lower and lower Z coordinates as collision points. MLSL is not a local
    // search method, so it should not be trapped in a local minima. Eventually,
    // this search should arrive at a ground location.
    auto oresult = solver.to_min().optimize(
        z_fn,
        initvals({plr_init, azm_init, 0.}),
        bound_constraints
    );

    GroundConnection conn;

    // Extract and apply the result
    auto [plr, azm, bridge_l] = oresult.optimum;
    Vec3d n = spheric_to_dir(plr, azm);
    assert(std::abs(n.norm() - 1.) < EPSILON);

    double t = (gndlvl - source.pos.z()) / n.z();
    bridge_l = std::min(t, bridge_l);

    // Now the optimizer gave a possible route to ground with a bridge direction
    // and length. This length can be shortened further by brute-force queries
    // of free route straigt down for a possible pillar.
    // NOTE: This requirement could be incorporated into the optimization as a
    // constraint, but it would not find quickly enough an accurate solution,
    // and it would be very hard to define a stop score which is very useful in
    // terminating the search as soon as the ground is found.
    double l = 0., l_max = bridge_l;
    double zlvl = std::numeric_limits<double>::infinity();
    // Every step of this loop is a beam cast of its own, so it asks the stop condition on its way:
    // a route of a long drop is hundreds of steps (M4.16). Asking it in the condition keeps a run
    // nobody stopped walking the very same steps.
    while(zlvl > gndlvl && l <= l_max && !(stopcond && stopcond())) {

        zlvl = check_ground_route(policy, sm, source, n, l, wideningfn,
                                  GroundRouteCheck::PillarOnly,
                                  base.radius).z();

        if (zlvl <= gndlvl)
            bridge_l = l;

        l += source.r;
    }

    Vec3d bridge_end = source.pos + bridge_l * n;
    Vec3d gp{bridge_end.x(), bridge_end.y(), gndlvl};

    double bridge_r = wideningfn(Ball{source.pos, source.r}, n, bridge_l);
    double down_l = bridge_end.z() - gndlvl;
    double end_radius = wideningfn(Ball{bridge_end, bridge_r}, DOWN, down_l);
    double base_r = std::max(base.radius, end_radius);

    // Even if the search was not succesful, the result is populated by the
    // source and the last best result of the optimization.
    conn.path.emplace_back(source);
    if (bridge_l > EPSILON)
        conn.path.emplace_back(Junction{bridge_end, bridge_r});

    // The resulting ground connection is only valid if the pillar base is set.
    // At this point it will only be set if the search was succesful.
    if (z_fn(Biz::Algorithms::Optimize::Input<3>({plr, azm, bridge_l})) <= gndlvl) {
        conn.pillar_base = Pedestal{gp, base.height, base_r, end_radius, base.shape};
        conn.full_radius = sp != nullptr ? head_back_radius(sm, *sp) : 0.;
        conn.stem        = stem_geometry(sm, sp);
    }

    return conn;
}

// Ground route search with a predefined end radius
template<class Ex>
GroundConnection deepsearch_ground_connection(Ex                              policy,
                                              const SupportableMesh          &sm,
                                              const Junction                 &source,
                                              double                          end_radius,
                                              const Vec3d                    &init_dir = DOWN,
                                              const Domain::SLA::SupportPoint *sp       = nullptr,
                                              JobController::StopCond          stopcond = &detail::never_stop)
{
    double gndlvl = ground_level(sm);
    auto wfn = [end_radius, gndlvl](const Ball &src, const Vec3d &dir, double len) {
        if (len < EPSILON)
            return src.R;

        Vec3d  dst = src.p + len * dir;
        double widening = end_radius - src.R;
        double zlen = dst.z() - gndlvl;
        double full_len = len + zlen;
        double r = src.R + widening * len / full_len;

        return r;
    };

    static_assert(IsWideningFn<decltype(wfn)>, "Not a widening function");

    return deepsearch_ground_connection(policy, sm, source, wfn, init_dir, sp, stopcond);
}

struct DefaultWideningModel {
    static constexpr double WIDENING_SCALE = 0.02;
    const SupportableMesh &sm;

    double operator()(const Ball &src, const Vec3d & /*dir*/, double len) {
        static_assert(IsWideningFn<DefaultWideningModel>,
                "DefaultWideningModel is not a widening function");

        double w = WIDENING_SCALE * sm.cfg.pillar_widening_factor * len;
        return std::max(src.R, sm.cfg.head_back_radius_mm) + w;
    };
};

template<> struct BeamSamples<DefaultWideningModel> {
    static constexpr size_t Value = 16;
};

template<class Ex>
GroundConnection deepsearch_ground_connection(Ex policy,
                                              const SupportableMesh &sm,
                                              const Junction &source,
                                              const Vec3d &init_dir = DOWN,
                                              JobController::StopCond stopcond = &detail::never_stop)
{
    return deepsearch_ground_connection(policy, sm, source,
                                        DefaultWideningModel{sm}, init_dir, nullptr, stopcond);
}

template<class Ex>
bool optimize_anchor_placement(Ex                     policy,
                               const SupportableMesh &sm,
                               const Junction        &from,
                               Anchor                &anchor,
                               JobController::StopCond stopcond = &detail::never_stop)
{
    Vec3d n = get_normal(sm.emesh, anchor.pos);

    auto [polar, azimuth] = dir_to_spheric(n);

    // Saturate the polar angle to 3pi/4
    polar = std::min(polar, sm.cfg.bridge_slope);

    double lmin = 0;
    double lmax = std::min(sm.cfg.head_width_mm,
                           distance(from.pos, anchor.pos) - 2 * from.r);

    double sd = sm.cfg.safety_distance(anchor.r_back_mm);

    Optimizer<AlgNLoptGenetic> solver(get_criteria(sm.cfg, stopcond)
                                          .stop_score(anchor.fullwidth())
                                          .max_iterations(100));

    solver.seed(0); // deterministic behavior

    auto oresult = solver.to_max().optimize(
        [&sm, &anchor, sd, policy](const Biz::Algorithms::Optimize::Input<3> &input) {
            auto &[plr, azm, l] = input;

            auto dir = spheric_to_dir(plr, azm).normalized();

            anchor.width_mm = l;
            anchor.dir = dir;

            return pinhead_mesh_hit(policy, sm.emesh, anchor, sd)
                .distance();
        },
        initvals({polar, azimuth, (lmin + lmax) / 2.}),
        bounds({{0., sm.cfg.bridge_slope}, // Must not exceed the slope limit
                {-PI, PI}, // azimuth can be a full search
                {lmin, lmax}}));

    polar = std::get<0>(oresult.optimum);
    azimuth = std::get<1>(oresult.optimum);
    anchor.dir = spheric_to_dir(polar, azimuth).normalized();
    anchor.width_mm = std::get<2>(oresult.optimum);

    if (oresult.score < anchor.fullwidth()) {
        // Unsuccesful search, the anchor does not fit into its intended space.
        return false;
    }

    return true;
}

template<class Ex>
std::optional<Anchor> calculate_anchor_placement(Ex policy,
                                                 const SupportableMesh &sm,
                                                 const Junction        &from,
                                                 const Vec3d &to_hint,
                                                 JobController::StopCond stopcond = &detail::never_stop)
{
    double back_r    = from.r;
    double pin_r     = sm.cfg.head_front_radius_mm;
    double penetr    = sm.cfg.head_penetration_mm;
    double hwidth    = sm.cfg.head_width_mm;
    Vec3d  bridgedir = dirv(from.pos, to_hint);
    Vec3d  anchordir = -bridgedir;

    std::optional<Anchor> ret;

    Anchor anchor(back_r, pin_r, hwidth, penetr, anchordir, to_hint);

    if (optimize_anchor_placement(policy, sm, from, anchor, stopcond)) {
        ret = anchor;
    } else if (anchor.r_back_mm = sm.cfg.head_fallback_radius_mm;
               optimize_anchor_placement(policy, sm, from, anchor, stopcond)) {
        // Retrying with the fallback strut radius as a last resort.
        ret = anchor;
    }

    return anchor;
}

inline bool is_outside_support_cone(const Vec3f &supp,
                                    const Vec3f &pt,
                                    float angle)
{
    using namespace Slic3r;

    Vec3d D = (pt - supp).cast<double>();
    double dot_sq = -D.z() * std::abs(-D.z());

    return dot_sq <
           D.squaredNorm() * std::cos(angle) * std::abs(std::cos(angle));
}

inline // TODO: should be in a cpp
std::optional<Vec3f> find_merge_pt(const Vec3f &A,
                                   const Vec3f &B,
                                   float        critical_angle)
{
    // The idea is that A and B both have their support cones. But searching
    // for the intersection of these support cones is difficult and its enough
    // to reduce this problem to 2D and search for the intersection of two
    // rays that merge somewhere between A and B. The 2D plane is a vertical
    // slice of the 3D scene where the 2D Y axis is equal to the 3D Z axis and
    // the 2D X axis is determined by the XY direction of the AB vector.
    //
    // Z^
    //  |    A *
    //  |     . .   B *
    //  |    .   .   . .
    //  |   .     . .   .
    //  |  .       x     .
    //  -------------------> XY

    // Determine the transformation matrix for the 2D projection:
    Vec3f diff = {B.x() - A.x(), B.y() - A.y(), 0.f};
    Vec3f dir  = diff.normalized(); // TODO: avoid normalization

    Eigen::Matrix<float, 2, 3> tr2D;
    tr2D.row(0) = Vec3f{dir.x(), dir.y(), dir.z()};
    tr2D.row(1) = Vec3f{0.f, 0.f, 1.f};

    // Transform the 2 vectors A and B into 2D vector 'a' and 'b'. Here we can
    // omit 'a', pretend that its the origin and use BA as the vector b.
    Vec2f b = tr2D * (B - A);

    // Get the square sine of the ray emanating from 'a' towards 'b'. This ray might
    // exceed the allowed angle but that is corrected subsequently.
    // The sign of the original sine is also needed, hence b.y is multiplied by
    // abs(b.y)
    float b_sqn = b.squaredNorm();
    float sin2sig_a = b_sqn > EPSILON ? (b.y() * std::abs(b.y())) / b_sqn : 0.f;

    // sine2 from 'b' to 'a' is the opposite of sine2 from a to b
    float sin2sig_b = -sin2sig_a;

    // Derive the allowed angles from the given critical angle.
    // critical_angle is measured from the horizontal X axis.
    // The rays need to go downwards which corresponds to negative angles

    float sincrit = std::sin(critical_angle);  // sine of the critical angle
    float sin2crit = -sincrit * sincrit;       // signed sine squared
    sin2sig_a = std::min(sin2sig_a, sin2crit); // Do the angle saturation of both rays
    sin2sig_b = std::min(sin2sig_b, sin2crit); //
    float sin2_a = std::abs(sin2sig_a);        // Get cosine squared values
    float sin2_b = std::abs(sin2sig_b);
    float cos2_a = 1.f - sin2_a;
    float cos2_b = 1.f - sin2_b;

    // Derive the new direction vectors. This is by square rooting the sin2
    // and cos2 values and restoring the original signs
    Vec2f Da = {std::copysign(std::sqrt(cos2_a), b.x()), std::copysign(std::sqrt(sin2_a), sin2sig_a)};
    Vec2f Db = {-std::copysign(std::sqrt(cos2_b), b.x()), std::copysign(std::sqrt(sin2_b), sin2sig_b)};

    // Determine where two rays ([0, 0], Da), (b, Db) intersect.
    // Based on
    // https://stackoverflow.com/questions/27459080/given-two-points-and-two-direction-vectors-find-the-point-where-they-intersect
    // One ray is emanating from (0, 0) so the formula is simplified
    double t1 = (Db.y() * b.x() - b.y() * Db.x()) /
                (Da.x() * Db.y() - Da.y() * Db.x());

    Vec2f mp = t1 * Da;
    Vec3f Mp = A + tr2D.transpose() * mp;

    return t1 >= 0.f ? Mp : Vec3f{};
}

}} // namespace Slic3r::sla

#endif // SLASUPPORTTREEUTILS_H
