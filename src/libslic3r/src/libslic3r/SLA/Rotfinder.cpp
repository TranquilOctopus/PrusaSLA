///|/ Copyright (c) Prusa Research 2020 - 2023 Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
#include <libslic3r/SLA/Rotfinder.hpp>
#include "Slic3r/Biz/Algorithms/Execution/ExecutionTBB.hpp"
#include "Slic3r/Biz/Algorithms/Optimize/BruteforceOptimizer.hpp"
#include <libslic3r/Geometry.hpp>
#include <libslic3r/SLA/OrientCrossSection.hpp>
#include <limits>
#include <thread>
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <numeric>
#include <unordered_map>
#include <vector>
#include <cinttypes>
#include <cstdlib>

#include "Slic3r/Biz/Algorithms/Execution/Execution.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Biz/Algorithms/Optimize/Optimizer.hpp"
#include "libslic3r/Point.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"

namespace Slic3r { namespace sla {
namespace tm = Slic3r::Biz::Algorithms::TriangleMesh;
using Domain::TriangleMesh;

namespace BB = Slic3r::Biz::Algorithms::BoundingBox;
// Transform3f/Transform3d and BoundingBoxf3 are the Slic3r aliases from libslic3r/Point.hpp and the
// legacy block of Slic3r/Domain/BoundingBox.hpp; redeclaring the Domain ones here clashes with them.

namespace {

inline const Vec3f DOWN = {0.f, 0.f, -1.f};
constexpr double POINTS_PER_UNIT_AREA = 1.f;

// Get the vertices of a triangle directly in an array of 3 points
std::array<Vec3f, 3> get_triangle_vertices(const TriangleMesh &mesh,
                                           size_t              faceidx)
{
    const auto &face = mesh.its.indices[faceidx];
    return {mesh.its.vertices[face[0]],
            mesh.its.vertices[face[1]],
            mesh.its.vertices[face[2]]};
}

std::array<Vec3f, 3> get_transformed_triangle(const TriangleMesh &mesh,
                                              const Transform3f & tr,
                                              size_t              faceidx)
{
    const auto &tri = get_triangle_vertices(mesh, faceidx);
    return {tr * tri[0], tr * tri[1], tr * tri[2]};
}

template<class T> LegacyVec<3, T> normal(const std::array<LegacyVec<3, T>, 3> &tri)
{
    LegacyVec<3, T> U = tri[1] - tri[0];
    LegacyVec<3, T> V = tri[2] - tri[0];
    return U.cross(V).normalized();
}

namespace execution = Slic3r::Biz::Algorithms::Execution;

template<class T, class AccessFn>
T sum_score(AccessFn &&accessfn, size_t facecount, size_t Nthreads)
{
    T  initv         = 0.;
    auto   mergefn   = [](T a, T b) { return a + b; };
    size_t grainsize = facecount / Nthreads;
    size_t from = 0, to = facecount;

    return execution::reduce(execution::ex_tbb, from, to, initv, mergefn, accessfn, grainsize);
}

// Get area and normal of a triangle
struct Facestats {
    Vec3f  normal;
    double area;

    explicit Facestats(const std::array<Vec3f, 3> &triangle)
    {
        Vec3f U = triangle[1] - triangle[0];
        Vec3f V = triangle[2] - triangle[0];
        Vec3f C = U.cross(V);
        normal = C.normalized();
        area = 0.5 * C.norm();
    }
};

// Try to guess the number of support points needed to support a mesh
double get_misalginment_score(const TriangleMesh &mesh, const Transform3f &tr)
{
    if (mesh.its.vertices.empty()) return NaNd;

    auto accessfn = [&mesh, &tr](size_t fi) {
        Facestats fc{get_transformed_triangle(mesh, tr, fi)};

        float score = fc.area
                      * (std::abs(fc.normal.dot(Vec3f::UnitX()))
                         + std::abs(fc.normal.dot(Vec3f::UnitY()))
                         + std::abs(fc.normal.dot(Vec3f::UnitZ())));

        // We should score against the alignment with the reference planes
        return scaled<int_fast64_t>(score);
    };

    size_t facecount = mesh.its.indices.size();
    size_t Nthreads  = std::thread::hardware_concurrency();
    double S = unscaled(sum_score<int_fast64_t>(accessfn, facecount, Nthreads));

    return S / facecount;
}

// The score function for a particular face
inline double get_supportedness_score(const Facestats &fc)
{
    // Simply get the angle (acos of dot product) between the face normal and
    // the DOWN vector.
    float cosphi = fc.normal.dot(DOWN);
    float phi = 1.f - std::acos(cosphi) / float(PI);

    // Make the huge slopes more significant than the smaller slopes
    phi = phi * phi * phi;

    // Multiply with the square root of face area of the current face,
    // the area is less important as it grows.
    // This makes many smaller overhangs a bigger impact.
    return std::sqrt(fc.area) * POINTS_PER_UNIT_AREA * phi;
}

// Try to guess the number of support points needed to support a mesh
double get_supportedness_score(const TriangleMesh &mesh, const Transform3f &tr)
{
    if (mesh.its.vertices.empty()) return NaNd;

    auto accessfn = [&mesh, &tr](size_t fi) {
        Facestats fc{get_transformed_triangle(mesh, tr, fi)};
        return scaled<int_fast64_t>(get_supportedness_score(fc));
    };

    size_t facecount = mesh.its.indices.size();
    size_t Nthreads  = std::thread::hardware_concurrency();
    double S = unscaled(sum_score<int_fast64_t>(accessfn, facecount, Nthreads));

    return S / facecount;
}

// Find transformed mesh ground level without copy and with parallel reduce.
float find_ground_level(const TriangleMesh &mesh,
                         const Transform3f & tr,
                         size_t              threads)
{
    size_t vsize = mesh.its.vertices.size();

    auto minfn = [](float a, float b) { return std::min(a, b); };

    auto accessfn = [&mesh, &tr] (size_t vi) {
        return (tr * mesh.its.vertices[vi]).z();
    };

    auto zmin = std::numeric_limits<float>::max();
    size_t granularity = vsize / threads;
    return execution::reduce(execution::ex_tbb, size_t(0), vsize, zmin, minfn, accessfn, granularity);
}

// Score the supportedness of a mesh resting on the build plate: the faces lying flat on the plate
// are rewarded by their area (they need no support at all) and everything else is scored as
// overhang. Upstream used this score for prints where the object touches the plate; unlike the
// general supportedness score it needs no configuration.
double get_supportedness_onfloor_score(const TriangleMesh &mesh, const Transform3f &tr)
{
    if (mesh.its.vertices.empty()) return NaNd;

    size_t Nthreads = std::thread::hardware_concurrency();

    float zmin = find_ground_level(mesh, tr, Nthreads);
    float zlvl = zmin + 0.1f; // Set up a slight tolerance from z level

    auto accessfn = [&mesh, &tr, zlvl](size_t fi) {
        std::array<Vec3f, 3> tri = get_transformed_triangle(mesh, tr, fi);
        Facestats fc{tri};

        if (tri[0].z() <= zlvl && tri[1].z() <= zlvl && tri[2].z() <= zlvl)
            return -2 * fc.area * POINTS_PER_UNIT_AREA;

        return get_supportedness_score(fc);
    };

    size_t facecount = mesh.its.indices.size();
    double S = unscaled(sum_score<int_fast64_t>(accessfn, facecount, Nthreads));

    return S / facecount;
}

using XYRotation = std::array<double, 2>;

// prepare the rotation transformation
Transform3f to_transform3f(const XYRotation &rot)
{
    Transform3f rt = Transform3f::Identity();
    rt.rotate(Eigen::AngleAxisf(float(rot[1]), Vec3f::UnitY()));
    rt.rotate(Eigen::AngleAxisf(float(rot[0]), Vec3f::UnitX()));

    return rt;
}

XYRotation from_transform3f(const Transform3f &tr)
{
    Vec3d rot3 = Domain::extract_rotation(tr.cast<double>());
    return {rot3.x(), rot3.y()};
}

// collect the rotations for each face of the convex hull
std::vector<XYRotation> get_chull_rotations(const TriangleMesh &mesh, size_t max_count)
{
    TriangleMesh chull = tm::convex_hull_3d(mesh);
    double chull2d_area = tm::convex_hull(chull).area();
    double area_threshold = chull2d_area / (scaled<double>(1e3) * scaled(1.));

    size_t facecount = chull.its.indices.size();

    struct RotArea { XYRotation rot; double area; };

    auto inputs = reserve_vector<RotArea>(facecount);

    auto rotcmp = [](const RotArea &r1, const RotArea &r2) {
        double xdiff = r1.rot[X] - r2.rot[X], ydiff = r1.rot[Y] - r2.rot[Y];
        return std::abs(xdiff) < EPSILON ? ydiff < 0. : xdiff < 0.;
    };

    auto eqcmp = [](const XYRotation &r1, const XYRotation &r2) {
        double xdiff = r1[X] - r2[X], ydiff = r1[Y] - r2[Y];
        return std::abs(xdiff) < EPSILON  && std::abs(ydiff) < EPSILON;
    };

    for (size_t fi = 0; fi < facecount; ++fi) {
        Facestats fc{get_triangle_vertices(chull, fi)};

        if (fc.area > area_threshold)  {
            auto q = Eigen::Quaternionf{}.FromTwoVectors(fc.normal, DOWN);
            XYRotation rot = from_transform3f(Transform3f::Identity() * q);
            RotArea ra = {rot, fc.area};

            auto it = std::lower_bound(inputs.begin(), inputs.end(), ra, rotcmp);

            if (it == inputs.end() || !eqcmp(it->rot, rot))
                inputs.insert(it, ra);
        }
    }

    inputs.shrink_to_fit();
    if (!max_count) max_count = inputs.size();
    std::sort(inputs.begin(), inputs.end(),
              [](const RotArea &ra, const RotArea &rb) {
                  return ra.area > rb.area;
              });

    auto ret = reserve_vector<XYRotation>(std::min(max_count, inputs.size()));
    for (const RotArea &ra : inputs) ret.emplace_back(ra.rot);

    return ret;
}

// Find the best score from a set of function inputs. Evaluate for every point.
template<size_t N, class Fn, class It, class StopCond>
std::array<double, N> find_min_score(Fn &&fn, It from, It to, StopCond &&stopfn)
{
    std::array<double, N> ret = {};

    double score = std::numeric_limits<double>::max();

    size_t Nthreads = std::thread::hardware_concurrency();
    size_t dist = std::distance(from, to);
    std::vector<double> scores(dist, score);

    execution::for_each(
        execution::ex_tbb, size_t(0), dist, [&stopfn, &scores, &fn, &from](size_t i) {
            if (stopfn()) return;

            scores[i] = fn(*(from + i));
        },
        dist / Nthreads);

    auto it = std::min_element(scores.begin(), scores.end());

    if (it != scores.end())
        ret = *(from + std::distance(scores.begin(), it));

    return ret;
}

} // namespace

// Declared in Rotfinder.hpp, so it must live outside the anonymous namespace above.
Transform3f rotation_angles_to_transform(const Vec2d &angles)
{
    return to_transform3f({angles.x(), angles.y()});
}


template<unsigned MAX_ITER>
struct RotfinderBoilerplate {
    static constexpr unsigned MAX_TRIES = MAX_ITER;

    int status = 0, prev_status = 0;
    TriangleMesh mesh;
    unsigned max_tries;
    const RotOptimizeParams &params;

    RotfinderBoilerplate(TriangleMesh mesh_to_search, const RotOptimizeParams &p)
        : mesh{std::move(mesh_to_search)}
        , max_tries(p.accuracy() * MAX_TRIES)
        , params{p}
    {}

    void statusfn() {
        int s = status * 100 / max_tries;
        if (s != prev_status) {
            params.statuscb()(s);
            prev_status = s;
        }

        ++status;
    }

    bool stopcond() { return ! params.statuscb()(-1); }
};

// Assemble the mesh with the correct transformation to be used in rotation optimization: the mesh
// of the object with the scaling and mirroring of its first instance applied and no rotation. The
// vertices stay in millimetres, which is what the rest of the engine reads them as: the slicer
// scales a mesh up into the coordinates of a layer itself, so a mesh handed to it in scaled
// coordinates would be measured a million times too big and its polygons would overflow the int32
// of a Point.
TriangleMesh mesh_to_rotate(const Domain::ModelObject &mo)
{
    TriangleMesh mesh = mo.raw_mesh();

    Domain::ModelInstance *mi = mo.instances[0];
    const Domain::Transformation trafo = mi->get_transformation();
    Transform3d trafo_instance = trafo.get_scaling_factor_matrix() * trafo.get_mirror_matrix();
    mesh.transform(trafo_instance);

    return mesh;
}

Vec2d find_best_misalignment_rotation(const Domain::ModelObject &mo,
                                      const RotOptimizeParams   &params)
{
    return find_best_misalignment_rotation(mesh_to_rotate(mo), params);
}

Vec2d find_least_supports_rotation(const Domain::ModelObject &mo,
                                   const RotOptimizeParams   &params)
{
    return find_least_supports_rotation(mesh_to_rotate(mo), params);
}

Vec2d find_min_z_height_rotation(const Domain::ModelObject &mo,
                                 const RotOptimizeParams   &params)
{
    return find_min_z_height_rotation(mesh_to_rotate(mo), params);
}

Vec2d find_least_peel_rotation(const Domain::ModelObject &mo,
                               const RotOptimizeParams   &params)
{
    return find_least_peel_rotation(mesh_to_rotate(mo), params);
}

Vec2d find_no_cups_rotation(const Domain::ModelObject &mo,
                             const RotOptimizeParams   &params)
{
    return find_no_cups_rotation(mesh_to_rotate(mo), params);
}

Vec2d find_miniature_rotation(const Domain::ModelObject &mo,
                              const RotOptimizeParams   &params)
{
    return find_miniature_rotation(mesh_to_rotate(mo), params);
}

Vec2d find_best_misalignment_rotation(const TriangleMesh &mesh,
                                      const RotOptimizeParams &params)
{
    RotfinderBoilerplate<1000> bp{TriangleMesh{mesh}, params};

    // Preparing the optimizer.
    size_t gridsize = std::sqrt(bp.max_tries);
    namespace OptNS = Slic3r::Biz::Algorithms::Optimize;
    OptNS::Optimizer<OptNS::AlgBruteForce> solver(
        OptNS::StopCriteria{}.max_iterations(bp.max_tries)
                           .stop_condition([&bp] { return bp.stopcond(); }),
        gridsize
    );

    // We are searching rotations around only two axes x, y. Thus the
    // problem becomes a 2 dimensional optimization task.
    // We can specify the bounds for a dimension in the following way:
    auto bounds = OptNS::bounds({ {-PI, PI}, {-PI, PI} });

    auto result = solver.to_max().optimize(
        [&bp] (const XYRotation &rot)
        {
            bp.statusfn();
            return get_misalginment_score(bp.mesh, to_transform3f(rot));
        }, OptNS::initvals({0., 0.}), bounds);

    return {result.optimum[0], result.optimum[1]};
}

Vec2d find_least_supports_rotation(const TriangleMesh &mesh,
                                   const RotOptimizeParams &params)
{
    RotfinderBoilerplate<1000> bp{TriangleMesh{mesh}, params};

    // The object rests on the build plate, so only the poses laying a convex hull face flat on
    // the plate have to be checked. This is a much smaller set than the whole XY plane, and every
    // pose in it is an exact face-down orientation, not a grid sample.
    const std::vector<XYRotation> inputs = get_chull_rotations(bp.mesh, bp.max_tries);
    if (inputs.empty()) {
        return Vec2d::Zero();
    }

    bp.max_tries = inputs.size();

    auto objfn = [&bp](const XYRotation &rot) {
        bp.statusfn();
        return get_supportedness_onfloor_score(bp.mesh, to_transform3f(rot));
    };

    XYRotation rot = find_min_score<2>(
        objfn, inputs.cbegin(), inputs.cend(), [&bp] { return bp.stopcond(); }
    );

    return {rot[0], rot[1]};
}

inline BoundingBoxf3 bounding_box_with_tr(const indexed_triangle_set &its,
                                          const Transform3f &tr)
{
    if (its.vertices.empty())
        return {};

    Vec3f bmin = tr * its.vertices.front(), bmax = tr * its.vertices.front();

    for (const Vec3f &p : its.vertices) {
        Vec3f pp = tr * p;
        bmin = pp.cwiseMin(bmin);
        bmax = pp.cwiseMax(bmax);
    }

    return {bmin.cast<double>(), bmax.cast<double>()};
}

Vec2d find_min_z_height_rotation(const TriangleMesh &mesh,
                                 const RotOptimizeParams &params)
{
    RotfinderBoilerplate<1000> bp{TriangleMesh{mesh}, params};

    TriangleMesh chull = tm::convex_hull_3d(bp.mesh);
    auto inputs = reserve_vector<XYRotation>(chull.its.indices.size());
    auto rotcmp = [](const XYRotation &r1, const XYRotation &r2) {
        double xdiff = r1[X] - r2[X], ydiff = r1[Y] - r2[Y];
        return std::abs(xdiff) < EPSILON ? ydiff < 0. : xdiff < 0.;
    };
    auto eqcmp = [](const XYRotation &r1, const XYRotation &r2) {
        double xdiff = r1[X] - r2[X], ydiff = r1[Y] - r2[Y];
        return std::abs(xdiff) < EPSILON  && std::abs(ydiff) < EPSILON;
    };

    for (size_t fi = 0; fi < chull.its.indices.size(); ++fi) {
        Facestats fc{get_triangle_vertices(chull, fi)};

        auto q = Eigen::Quaternionf{}.FromTwoVectors(fc.normal, DOWN);
        XYRotation rot = from_transform3f(Transform3f::Identity() * q);

        auto it = std::lower_bound(inputs.begin(), inputs.end(), rot, rotcmp);

        if (it == inputs.end() || !eqcmp(*it, rot))
            inputs.insert(it, rot);
    }

    inputs.shrink_to_fit();
    bp.max_tries = inputs.size();

    auto objfn = [&bp, &chull](const XYRotation &rot) {
        bp.statusfn();
        Transform3f tr = to_transform3f(rot);
        return BB::sizes(bounding_box_with_tr(chull.its, tr)).z();
    };

    XYRotation rot = find_min_score<2>(objfn, inputs.begin(), inputs.end(), [&bp] {
        return bp.stopcond();
    });

    return {rot[0], rot[1]};
}

namespace {

// The two PLAN B7 goals that need the sliced cross sections rather than a walk over the triangles:
// score the poses that lay a convex hull face flat on the plate by their coarse slices and take the
// cheapest one. The weights of SLA/OrientCrossSection.hpp say what counts.
Vec2d find_cross_section_rotation(const TriangleMesh &mesh,
                                  const RotOptimizeParams &params,
                                  const CrossSectionWeights &weights)
{
    RotfinderBoilerplate<1000> bp{TriangleMesh{mesh}, params};

    // Slicing costs orders of magnitude more than the triangle walks of the other goals, so only
    // the biggest poses are sliced at all. The object rests on the plate, so these are the poses
    // that lay a convex hull face flat on it, as in find_least_supports_rotation() above.
    const std::vector<XYRotation> inputs =
        get_chull_rotations(bp.mesh, cross_section_pose_limit);
    if (inputs.empty()) {
        return Vec2d::Zero();
    }

    bp.max_tries = inputs.size();

    auto objfn = [&bp, &weights](const XYRotation &rot) {
        bp.statusfn();
        return score_cross_sections(bp.mesh, to_transform3f(rot), weights).score;
    };

    XYRotation rot = find_min_score<2>(
        objfn, inputs.cbegin(), inputs.cend(), [&bp] { return bp.stopcond(); }
    );

    return {rot[0], rot[1]};
}

} // namespace

Vec2d find_least_peel_rotation(const TriangleMesh &mesh,
                               const RotOptimizeParams &params)
{
    // Nothing but the peak cross section, which is the peel force the print has to get through.
    return find_cross_section_rotation(mesh, params, CrossSectionWeights{.peak_area = 1.});
}

Vec2d find_no_cups_rotation(const TriangleMesh &mesh,
                            const RotOptimizeParams &params)
{
    // The cup openings dominate, because a cup sealing against the film adds its suction term to
    // every peel it is open for, while the peak area is paid once. The peak area is not dropped
    // completely: with a weight of 0.05 it costs 5 mm2 of peak area to save 1 mm2 of cup opening,
    // so a pose with a cup is only taken over a big enough saving in peak area, and among the poses
    // that have no cup at all the one that peels easiest is still the one that is picked.
    return find_cross_section_rotation(
        mesh, params, CrossSectionWeights{.peak_area = 0.05, .cup_opening = 1.});
}

// ------------------------------------------------------------------------------------------------
// The miniature goal.
//
// A pre-supported head, a helmet or a bust is not one of the shapes the goals above are for. It
// comes with a flat face where a support is not seen - the cut of a neck the bust is glued to the
// body by, the underside of a head, the flat back of a bust - and the way it is printed is decided
// by the person who sculpted it: the detail side up, so the layers cut across the face and not
// along it, a fat support in the neck that the body hides and small ones where the detail needs
// them.
//
// So this pose is not searched for, it is built:
//
// 1. the glue face is found in the mesh (find_glue_face) and its normal is pointed at the plate,
// 2. the model is leaned over by miniature_tilt_degrees about a horizontal axis, which is what
// makes the cross sections of the print ramp in gradually from the narrow cut up to the widest
// part of the piece instead of standing up as a full width disc (less peel, no flat layers that
// would need a raft worth of area),
// 3. the azimuth of that lean is the one thing searched. Among the miniature_tilt_directions
// azimuths, the one that keeps the most detail facing up wins, detail being the surface on the
// side away from the glue face weighted by how finely it is tessellated (a sculpted face is
// many small facets, so the small ones carry the score), and the least area facing the plate
// wins first among those: that area is what has to be supported and what a layer of it holds
// against the film.
//
// The rotation about Z is not part of the search and the model is not turned on the plate. A lean
// about a horizontal axis at an azimuth is a pose with a spin around the vertical axis in it,
// though, and the convention of the engine is R = Ry(y) * Rx(x), which has no spin of its own:
// the two rotations together can only put a piece where they can both be said to put it, so the
// pose of a lean is rebuilt inside them (pose_pointing_glue_face_at) instead of being taken apart
// after the fact. What comes out is the lean where the two rotations agree on it, which is
// everywhere but on the few pieces whose flat face points along an axis of the plate.
namespace {

/// Facets count as coplanar when their normals are within this many degrees of each other. A cut is
/// tessellated, and the facets of it differ by the noise of that tessellation rather than by
/// nothing at all.
constexpr double glue_face_coplanar_deg = 2.;

/// ... and the facets have to lie in the same plane to within this many mm, so that two parallel
/// facets on different sides of a piece are not one region.
constexpr double glue_face_plane_tolerance_mm = 0.02;

/// A cut is small against the piece it is cut into: the neck a bust is glued by and the flat back
/// of one are both a fraction of its surface. A face that is a quarter of the surface is a side of
/// the piece, not somewhere to glue a support in.
constexpr double glue_face_max_area_fraction = 0.25;

/// A region below this many mm² is the noise of a tessellation and not a face.
constexpr double glue_face_min_area_mm2 = 0.1;

/// How far off the extreme of the piece along its own normal a cut may sit and still be at that
/// extreme, as a fraction of its diagonal and with a floor of its own so that a small piece still
/// has a tolerance to speak of.
constexpr double glue_face_extremity_fraction = 0.005;
constexpr double glue_face_extremity_min_mm   = 0.05;

/// A piece with no cut in it is leaned over on the smallest flat region of its lowest tenth
/// instead, which is where the flat face a support can hide in is most likely to be.
constexpr double glue_face_lowest_fraction = 0.1;

/// How far a miniature is leaned over, in degrees, and the range the rule allows. A miniature
/// leans: the glue face flat on the plate makes its first layers as wide as the widest part of the
/// piece, which is a big peel and a cup waiting to happen, while leaning it lets the cross sections
/// ramp in. Past about 30 degrees the lean starts to put a nose or an ear into an overhang that
/// needs a support of its own, and 45 is where the ramp has already done its work.
constexpr double miniature_tilt_min_degrees = 30.;
constexpr double miniature_tilt_degrees     = 35.;
constexpr double miniature_tilt_max_degrees = 45.;

/// The lean in radians, clamped into the range above, so that changing the number above cannot
/// take the rule out of what it says it does.
constexpr double miniature_tilt_rad =
    std::clamp(miniature_tilt_degrees, miniature_tilt_min_degrees, miniature_tilt_max_degrees)
    * PI
    / 180.;

/// A facet leaning below this much down (-0.5 is 60 degrees off the horizontal) faces the plate:
/// it is an overhang the print cannot get to, and an area of it that is not there is an area of
/// support that is not needed.
constexpr double miniature_facing_down_z = -0.5;

/// How many azimuths the lean is tried at. The lean is one number and the piece is not round, so
/// this is the whole search.
constexpr size_t miniature_tilt_directions = 24;

/// How far short of the direction a lean meant the pose may come and still count as leaned, in
/// degrees. The convention of the engine cannot reach every direction (see
/// pose_pointing_glue_face_at below), and a pose that came up short has no lean in it at all, so
/// it must not win over a pose that really is leaned.
constexpr double miniature_pose_reached_deg = 0.5;

/// Poses whose facing down area is within this fraction of the least of them count as equal for
/// that term, so that the detail decides between them instead of a corner of a jaw doing it.
constexpr double miniature_down_area_relaxation = 0.02;

/// The angle between two directions, in degrees.
inline double angle_between_degrees(const Vec3d& one, const Vec3d& other)
{
    const double cos_angle = std::clamp(one.normalized().dot(other.normalized()), -1., 1.);
    return std::acos(cos_angle) * 180. / PI;
}

/// The angle one pose would have to be turned by to become the other one, in degrees.
double pose_gap_degrees(const Transform3f& one, const Transform3f& other)
{
    const Transform3f relative = one.inverse() * other;
    const double cos_angle     = std::clamp(double(relative.linear().trace()) / 2. - 0.5, -1., 1.);
    return std::acos(cos_angle) * 180. / PI;
}

/// One facet of the mesh, measured once. Everything below walks the mesh over and over and needs
/// the same four numbers of every facet, so they are measured in a single walk up front.
struct FacetMeasure
{
    /// Unit, pointing out of the piece. The zero vector for a facet with no area.
    Vec3d normal{Vec3d::Zero()};
    /// The middle of the facet.
    Vec3d centroid{Vec3d::Zero()};
    /// The area of the facet, in mm². A facet with no area (the ring of a sphere at its pole, a
    /// triangle a boolean left behind) measures zero and joins no region.
    double area_mm2{0.};
    /// Where the plane of the facet sits along its own normal.
    double plane_mm{0.};
};

std::vector<FacetMeasure> measure_facets(const TriangleMesh& mesh)
{
    std::vector<FacetMeasure> facets;
    facets.reserve(mesh.its.indices.size());

    for (size_t fi = 0; fi < mesh.its.indices.size(); ++fi) {
        const auto& face = mesh.its.indices[fi];
        const Vec3d p0{mesh.its.vertices[face[0]].cast<double>()};
        const Vec3d p1{mesh.its.vertices[face[1]].cast<double>()};
        const Vec3d p2{mesh.its.vertices[face[2]].cast<double>()};
        const Vec3d cross       = (p1 - p0).cross(p2 - p0);
        const double twice_area = cross.norm();

        // The facet keeps the place of the triangle it came from: the facets of one region are
        // joined on the edges the triangles of the mesh share and are found again by that index.
        FacetMeasure facet;
        facet.centroid = (p0 + p1 + p2) / 3.;
        if (twice_area > 0.) {
            facet.normal   = cross / twice_area;
            facet.area_mm2 = 0.5 * twice_area;
            facet.plane_mm = facet.normal.dot(facet.centroid);
        }
        facets.push_back(facet);
    }

    return facets;
}

/// The sets of facets that are one region each, kept as a parent per facet with the path walked
/// into the root, which is all a union of a few thousand facets needs.
struct DisjointSet
{
    std::vector<size_t> parent;

    explicit DisjointSet(size_t count) : parent(count)
    {
        std::iota(parent.begin(), parent.end(), size_t(0));
    }

    size_t find(size_t x)
    {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x         = parent[x];
        }
        return x;
    }

    void merge(size_t a, size_t b)
    {
        const size_t root_a = find(a), root_b = find(b);
        if (root_a != root_b)
            parent[root_b] = root_a;
    }
};

/// One flat region of the mesh: the area weighted mean of the facets that are coplanar with each
/// other and joined to each other.
struct PlanarRegion
{
    Vec3d normal{Vec3d::Zero()};
    Vec3d centroid{Vec3d::Zero()};
    double area_mm2{0.};
    /// How many facets it is made of. A region of one facet is a triangle, not a face: nothing a
    /// support can be hidden in, and on a curved surface every facet is one of them.
    size_t facets{0};
};

inline uint64_t edge_key(size_t a, size_t b)
{
    return (uint64_t(std::min(a, b)) << 32) | uint64_t(std::max(a, b));
}

/// The flat regions of the mesh: maximal sets of facets whose normals are within
/// glue_face_coplanar_deg of each other, that lie in the same plane, and that are joined by an edge
/// they share. The joining matters: two facets in the same plane that touch nowhere are two faces,
/// and a cut is one of them only where it is connected.
std::vector<PlanarRegion>
planar_regions(const TriangleMesh& mesh, const std::vector<FacetMeasure>& facets)
{
    DisjointSet sets{facets.size()};

    // The facet each edge was seen on, so that two facets sharing one can be joined.
    std::unordered_map<uint64_t, size_t> facet_of_edge;
    facet_of_edge.reserve(facets.size() * 3);

    const double cos_coplanar = std::cos(glue_face_coplanar_deg * PI / 180.);

    for (size_t fi = 0; fi < facets.size(); ++fi) {
        const FacetMeasure& facet = facets[fi];
        const auto& face          = mesh.its.indices[fi];

        for (int e = 0; e < 3; ++e) {
            const uint64_t key = edge_key(face[e], face[(e + 1) % 3]);
            const auto it      = facet_of_edge.find(key);
            if (it == facet_of_edge.end()) {
                facet_of_edge.emplace(key, fi);
                continue;
            }

            const FacetMeasure& other = facets[it->second];
            // A facet with no area has no normal, so it is never coplanar with anything and joins
            // no region (a dot product of zero against a cosine of nearly one fails on its own).
            if (facet.normal.dot(other.normal) >= cos_coplanar
                && std::abs(facet.plane_mm - other.plane_mm) <= glue_face_plane_tolerance_mm)
                sets.merge(fi, it->second);
        }
    }

    std::vector<PlanarRegion> regions;
    std::vector<Vec3d> normal_sum, centroid_sum;
    std::vector<double> area_sum;
    std::unordered_map<size_t, size_t> region_of_root;

    for (size_t fi = 0; fi < facets.size(); ++fi) {
        const FacetMeasure& facet = facets[fi];
        if (!(facet.area_mm2 > 0.))
            continue;

        const size_t root = sets.find(fi);
        const auto it     = region_of_root.find(root);
        size_t region     = 0;
        if (it == region_of_root.end()) {
            region = regions.size();
            region_of_root.emplace(root, region);
            regions.emplace_back();
            normal_sum.emplace_back(Vec3d::Zero());
            centroid_sum.emplace_back(Vec3d::Zero());
            area_sum.emplace_back(0.);
        } else {
            region = it->second;
        }

        normal_sum[region] += facet.normal * facet.area_mm2;
        centroid_sum[region] += facet.centroid * facet.area_mm2;
        area_sum[region] += facet.area_mm2;
        ++regions[region].facets;
    }

    for (size_t region = 0; region < regions.size(); ++region) {
        regions[region].area_mm2 = area_sum[region];
        if (area_sum[region] > 0.) {
            regions[region].normal   = normal_sum[region] / area_sum[region];
            regions[region].centroid = centroid_sum[region] / area_sum[region];
            // The mean of normals of one plane has the length 1 up to the noise of the
            // tessellation; the copy is what makes the extremity test below a plain comparison.
            regions[region].normal.normalize();
        }
    }

    return regions;
}

/// Is this flat region at one of the extremes of the piece along its own normal, with the rest of
/// the piece on the other side of it? A cut is (the neck is at the bottom, the flat back of a bust
/// is at the back); the front of a face is not, and neither is a shoulder, because both have piece
/// on the far side of them along their normal as well.
bool is_at_extremity(const TriangleMesh& mesh, const PlanarRegion& region, double tolerance_mm)
{
    if (region.area_mm2 <= 0.)
        return false;

    double lowest  = std::numeric_limits<double>::max();
    double highest = std::numeric_limits<double>::lowest();
    for (const Vec3f& vertex : mesh.its.vertices) {
        const double along = region.normal.dot(vertex.cast<double>());
        lowest             = std::min(lowest, along);
        highest            = std::max(highest, along);
    }

    const double here = region.normal.dot(region.centroid);
    return highest - here <= tolerance_mm && here - lowest > tolerance_mm;
}

/// The face of a miniature that a support can be hidden in: the cut it is glued to a body by.
struct GlueFace
{
    /// Whether one was found at all. A piece with no flat face has none and is only leaned over.
    bool found{false};
    /// Unit, pointing out of the piece at the face.
    Vec3d normal{Vec3d::Zero()};
    double area_mm2{0.};
    /// Where the plane of the face sits along its own normal, which is how the facets of the cut
    /// are told apart from the rest of the piece.
    double plane_mm{0.};
};

GlueFace find_glue_face(
    const TriangleMesh& mesh,
    const std::vector<FacetMeasure>& facets,
    const std::vector<PlanarRegion>& regions
)
{
    double surface_area_mm2 = 0.;
    for (const FacetMeasure& facet : facets)
        surface_area_mm2 += facet.area_mm2;

    const BoundingBoxf3 bounds = bounding_box_with_tr(mesh.its, Transform3f::Identity());
    const Vec3d sizes{BB::sizes(bounds).cast<double>()};
    const double extremity_mm =
        std::max(glue_face_extremity_min_mm, glue_face_extremity_fraction * sizes.norm());

    // The regions that could be a cut at all: flat enough against the surface of the piece to be a
    // cut rather than a side of it, big enough to be a face, and made of more than one facet.
    // Biggest first, so that the first one of them that really is at an extreme of the piece is the
    // glue face.
    std::vector<const PlanarRegion*> candidates;
    candidates.reserve(regions.size());
    for (const PlanarRegion& region : regions) {
        if (region.facets < 2)
            continue;
        if (region.area_mm2 < glue_face_min_area_mm2)
            continue;
        if (region.area_mm2 > glue_face_max_area_fraction * surface_area_mm2)
            continue;
        candidates.push_back(&region);
    }
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const PlanarRegion* lhs, const PlanarRegion* rhs)
        { return lhs->area_mm2 > rhs->area_mm2; }
    );

    for (const PlanarRegion* region : candidates) {
        if (is_at_extremity(mesh, *region, extremity_mm)) {
            return GlueFace{
                true,
                region->normal,
                region->area_mm2,
                region->normal.dot(region->centroid)
            };
        }
    }

    // Nothing flat at an extreme of the piece: either it has no extreme that is flat (a head that
    // is cut off at an angle, a bust whose neck was modelled round) or nothing flat at all. Take
    // the smallest flat region of the lowest tenth of the piece instead, which is the closest thing
    // to a neck cut it has.
    const double lowest_z = double(bounds.min.z());
    const PlanarRegion* fallback{nullptr};
    for (const PlanarRegion& region : regions) {
        if (region.facets < 2)
            continue;
        if (region.area_mm2 < glue_face_min_area_mm2)
            continue;
        // Only a face that is not looking up: a cut is used from the side the piece is on, and a
        // face that looks up would have to be turned over to be a glue face.
        if (region.normal.z() > 0.)
            continue;
        if (region.centroid.z() > lowest_z + glue_face_lowest_fraction * sizes.z())
            continue;
        if (!fallback || region.area_mm2 < fallback->area_mm2)
            fallback = &region;
    }

    if (fallback) {
        return GlueFace{
            true,
            fallback->normal,
            fallback->area_mm2,
            fallback->normal.dot(fallback->centroid)
        };
    }

    // A piece with no flat face at all (a skull, a scanned bust nobody has cut) has no cut to
    // find. It keeps the pose it is loaded in and is only leaned over, which is still a pose that
    // can be printed and no worse than the one it came in.
    return GlueFace{};
}

/// The pose of the engine's convention (R = Ry(y) * Rx(x)) that points the glue face of the piece
/// at the given direction, which is where a lean wants it.
///
/// Rx(x) is the only one of the two rotations of the convention that moves the y and the z of a
/// normal, so the normal can be swung sideways no further than hypot(n.y, n.z) reaches and the y
/// component of the target says whether the convention reaches it at all: a target further out
/// than that comes back as the pose that gets closest to it, which is a pose without any lean in
/// it. The equation for the swing is n.y cos(x) - n.z sin(x) = target.y() and it has two roots.
/// Both of them put the normal on the target and they differ by a spin around it, so the one
/// nearer to the pose the lean meant is the one that is kept.
XYRotation
pose_pointing_glue_face_at(const Vec3d& glue_normal, const Vec3d& target, const Transform3f& ideal)
{
    const double reach  = std::hypot(glue_normal.y(), glue_normal.z());
    const double wanted = std::clamp(target.y(), -reach, reach);
    const double centre = reach > 0. ? std::atan2(glue_normal.z(), glue_normal.y()) : 0.;
    const double spread = reach > 0. ? std::acos(wanted / reach) : 0.;

    XYRotation pose{0., 0.};
    double gap = std::numeric_limits<double>::max();

    for (const double sign : {1., -1.}) {
        const double x = sign * spread - centre;
        // What is left of the normal once Rx(x) has swung it: its x is its own, its y is the
        // component the equation above fixed and its z is what is left of the pair (y, z).
        const Vec3d swung{
            glue_normal.x(),
            glue_normal.y() * std::cos(x) - glue_normal.z() * std::sin(x),
            glue_normal.y() * std::sin(x) + glue_normal.z() * std::cos(x)
        };
        // Ry(y) turns the pair (x, z) and leaves y alone, so it turns the pair of the swung normal
        // onto the pair of the target. Both of those pairs are the zero vector on the piece whose
        // cut points along an axis, and then there is nothing to turn through.
        const double y = (swung.x() == 0. && swung.z() == 0.) ?
            0. :
            std::atan2(swung.z(), swung.x()) - std::atan2(target.z(), target.x());

        const double here = pose_gap_degrees(to_transform3f({x, y}), ideal);
        if (here < gap) {
            gap  = here;
            pose = {x, y};
        }
    }

    // Into -180 .. 180, the range the other searches hand their rotations out in.
    return {std::remainder(pose[0], 2. * PI), std::remainder(pose[1], 2. * PI)};
}

/// One pose of the miniature goal and the three numbers it is picked by.
struct MiniaturePose
{
    XYRotation rot{0., 0.};
    /// How far short of the direction its lean meant the convention of the engine came, in
    /// degrees. Zero for a pose that really is leaned.
    double reached_deg{0.};
    /// The area of the facets facing the plate, in mm²: what has to be supported.
    double facing_down_mm2{0.};
    /// The detail of the piece still facing up, weighted by how finely it is tessellated.
    double detail_up{0.};
};

/// Score one candidate lean of the miniature goal, with the pose the convention of the engine can
/// actually hand out for it.
void score_miniature_pose(
    const std::vector<FacetMeasure>& facets,
    const GlueFace& glue,
    const Vec3d& glue_normal,
    const Transform3f& rotated,
    MiniaturePose& out
)
{
    const Transform3d pose = rotated.cast<double>();

    for (const FacetMeasure& facet : facets) {
        if (!(facet.area_mm2 > 0.))
            continue;

        const Vec3d normal = pose * facet.normal;
        // The cut itself is on the plate and is the one part of the piece that never needs a
        // support of its own, so it is not counted as facing down: it would be the biggest area of
        // the pose facing the plate and it would be the same for every lean.
        const bool on_the_cut =
            glue.found && std::abs(facet.plane_mm - glue.plane_mm) <= glue_face_plane_tolerance_mm;
        if (!on_the_cut && normal.z() < miniature_facing_down_z)
            out.facing_down_mm2 += facet.area_mm2;

        // The detail is the surface on the side away from the glue face: everything the piece
        // shows the world instead of the plate. Its score is the area of it that still faces up,
        // weighted by the square root of the area of the facet, which is the same weight the
        // supportedness score of the least supports goal uses and for the same reason: a sculpted
        // face is a lot of small facets, so the small ones are where the detail is.
        if (facet.normal.dot(glue_normal) < 0.)
            out.detail_up += std::sqrt(facet.area_mm2) * std::max(0., normal.z());
    }
}

} // namespace

Vec2d find_miniature_rotation(const TriangleMesh& mesh, const RotOptimizeParams& params)
{
    RotfinderBoilerplate<1000> bp{TriangleMesh{mesh}, params};

    if (bp.mesh.its.vertices.empty() || bp.mesh.its.indices.empty()) {
        return Vec2d::Zero();
    }

    const std::vector<FacetMeasure> facets = measure_facets(bp.mesh);
    const GlueFace glue = find_glue_face(bp.mesh, facets, planar_regions(bp.mesh, facets));

    // A piece with no flat face keeps the pose it is loaded in: the identity rotation lays the
    // -Z of the mesh down, which is what the fallback below does with the normal it reports.
    const Vec3d glue_normal = glue.found ? glue.normal : Vec3d(0., 0., -1.);
    const Eigen::Quaternionf lay_down{
        glue.found ?
            Eigen::Quaternionf{}.FromTwoVectors(glue_normal.cast<float>(), DOWN) :
            Eigen::Quaternionf::Identity()
    };

    // The lean is the only thing searched, the azimuths of it go around the plate once, and they
    // are the progress of the search as well.
    bp.max_tries = unsigned(miniature_tilt_directions);

    std::vector<MiniaturePose> poses;
    poses.reserve(miniature_tilt_directions);
    for (size_t i = 0; i < miniature_tilt_directions; ++i) {
        const double azimuth = 2 * PI * double(i) / double(miniature_tilt_directions);
        const Vec3f axis{float(std::cos(azimuth)), float(std::sin(azimuth)), 0.f};
        // The pose the lean means: the piece laid down on its cut and tipped over. It is built
        // here and not only measured against, because the poses the convention of the engine
        // cannot reach have to be told from the ones it can.
        const Transform3f ideal{
            Transform3f::Identity()
            * (Eigen::AngleAxisf(float(miniature_tilt_rad), axis) * lay_down)
        };
        const Vec3d target{(ideal * glue_normal.cast<float>()).cast<double>()};

        MiniaturePose& pose      = poses.emplace_back();
        pose.rot                 = pose_pointing_glue_face_at(glue_normal, target, ideal);
        const Transform3f leaned = to_transform3f(pose.rot);
        // How far the convention of the engine came short of the lean, which is what tells a pose
        // that is really leaned from one that came back flat on the plate.
        const Vec3d landed{(leaned * glue_normal.cast<float>()).cast<double>()};
        pose.reached_deg = angle_between_degrees(landed, target);
        score_miniature_pose(facets, glue, glue_normal, leaned, pose);
        bp.statusfn();

        if (bp.stopcond())
            break;
    }

    if (poses.empty())
        return Vec2d::Zero();

    // A pose the convention could not lean has no lean in it and must not win over one that has:
    // a piece whose flat face points along an axis of the plate can only be leaned to one side.
    // Those poses are looked at anyway when none of the azimuths could be reached.
    const auto is_leaned = [](const MiniaturePose& pose)
    { return pose.reached_deg <= miniature_pose_reached_deg; };
    const bool any_leaned = std::any_of(poses.cbegin(), poses.cend(), is_leaned);

    // What faces the plate first, the detail among the poses that are as good as each other in
    // that. The piece is never scored against a print and nothing is sliced.
    double least_facing_down = std::numeric_limits<double>::max();
    for (const MiniaturePose& pose : poses)
        if (!any_leaned || is_leaned(pose))
            least_facing_down = std::min(least_facing_down, pose.facing_down_mm2);

    const double as_good_as_the_best =
        least_facing_down * (1. + miniature_down_area_relaxation) + glue_face_plane_tolerance_mm;

    const MiniaturePose* best{nullptr};
    for (const MiniaturePose& pose : poses) {
        if (any_leaned && !is_leaned(pose))
            continue;
        if (pose.facing_down_mm2 > as_good_as_the_best)
            continue;
        if (!best || pose.detail_up > best->detail_up)
            best = &pose;
    }

    // A search that was cancelled before it scored anything still answers with a pose, as the
    // other searches do: the first lean of the azimuths.
    if (!best)
        best = &poses.front();

    return {best->rot[0], best->rot[1]};
}
}} // namespace Slic3r::sla
