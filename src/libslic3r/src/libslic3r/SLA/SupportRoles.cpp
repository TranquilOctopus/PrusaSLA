#include "libslic3r/SLA/SupportRoles.hpp"

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"

#include <Eigen/Dense>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <vector>

namespace Slic3r::sla {

using Domain::Point;
using Domain::Vec2d;
using Domain::Vec3d;
using Domain::SLA::SupportPoint;
using Domain::SLA::SupportPoints;
using Role = SupportPoint::Role;

namespace {

// The number of directions the surface around a point is looked at in. A fixed number, so the same
// model always gives the same roles, whatever order the points arrive in.
constexpr int RING_SAMPLES = 8;

// How far off a surface every ray of this file starts, in mm. A ray cast from a surface can hit the
// very triangle it starts on, which measures nothing.
constexpr double RAY_START_OFFSET = 0.05; // [in mm]

// How deep a probe starts below the spot it looks at, from the deepest to the shallowest, in mm.
// The start has to be inside the material for the ray to come out at the first surface above the
// spot, and not at the far side of a thin part or at nothing at all. How deep the material is under
// a spot is only known once the ray is cast, so the deepest start that is inside wins: that is what
// measures a rivet under a thick plate against the plate and a point on a thin plate against its own
// thickness. The shallowest start is inside the material wherever the point is on the surface, so a
// spot that is measured at all is measured by one of them.
constexpr std::array<double, 4> PROBE_DEPTHS{1.0, 0.5, 0.2, 0.05}; // [in mm]

// The rings around a point the surface is sampled on, in mm. The far ring is the plane a bump of
// R4.5 is measured against, and the two rings inside it say whether the surface around the point is
// in that plane at all, which a curve is not and a flat surface with a rivet on it is.
constexpr double NEAR_RING_RADIUS   = 1.0; // [in mm]
constexpr double MIDDLE_RING_RADIUS = 1.6; // [in mm]
constexpr double FAR_RING_RADIUS    = 2.6; // [in mm]

// The distances from a point that sits on small surface detail the plain surface next to it is
// looked for at, in mm, nearest first.
constexpr std::array<double, 4> MOVE_OFFSETS{0.4, 0.6, 0.8, 1.0}; // [in mm]

/// Two directions in the plane of the surface around a point, so that the rings around it can be
/// told apart in that plane. Fixed for a given normal, so the answer does not depend on the order
/// the points are visited in.
struct TangentPlane
{
    Vec3d first;
    Vec3d second;
};

TangentPlane tangent_plane(const Vec3d& n)
{
    // Any direction will do, as long as it is not nearly the normal itself, or the cross product
    // below would be almost zero.
    const Vec3d seed  = std::abs(n.x()) < 0.9 ? Vec3d{1., 0., 0.} : Vec3d{0., 1., 0.};
    const Vec3d first = seed.cross(n).normalized();
    return {first, n.cross(first).normalized()};
}

/// The surface a probe found, and where it sits around the point it was looked for: @a u and @a v
/// along the two directions of the ring, @a h along the outward normal of the point. Not valid
/// where the probe hit nothing.
struct SurfaceSample
{
    Vec3d pos;
    Vec3d normal;
    double u   = 0.;
    double v   = 0.;
    double h   = 0.;
    bool valid = false;
};

/// The plane of the surface around a point, in the directions of the ring it was fitted on.
struct LocalSurface
{
    // The height of the fitted plane at the point itself, measured along the outward normal of the
    // point. A point on a bump stands out of the plane the surface around it is in, so the plane
    // there is on the far side of it and this is negative; a point in a dent gives the opposite.
    double at_origin = 0.;
    Vec2d slope{0., 0.};

    double height_at(double u, double v) const
    {
        return at_origin + slope.x() * u + slope.y() * v;
    }
};

/// The part of the model a point is on: the layer closest to it that is within one layer height,
/// and the part on that layer the point lies in.
struct LayerAtPoint
{
    const Layer* layer    = nullptr;
    const LayerPart* part = nullptr;

    bool found() const
    {
        return part != nullptr;
    }
};

/// The outward normal of the surface a point is on, i.e. the normal of the face closest to it. The
/// faces of a model point out of the material (see the drain hole suggestions), so this is the way
/// out of the model at the point. Zero where the point is not on the model at all, and then there
/// is nothing here to measure and the caller says so.
Vec3d outward_normal_at(const AABBMesh& mesh, const Vec3d& p)
{
    int face_id = -1;
    Vec3d closest;
    mesh.squared_distance(p, face_id, closest);
    if (face_id < 0)
        return Vec3d::Zero();

    // A face with no area has no normal to give, and normalizing that would be a NaN.
    const Vec3d n = mesh.normal_by_face_id(face_id);
    return n.allFinite() && n.squaredNorm() > 1e-18 ? n : Vec3d::Zero();
}

/// Whether there is a normal of the surface at a point at all, i.e. whether the point is on the
/// model.
bool has_normal(const Vec3d& n)
{
    return n.allFinite() && n.squaredNorm() > 1e-18;
}

/// How deep the material goes under a point: the ray from just inside the surface into the model
/// comes out where the model ends, and that distance is the thickness of the feature the point is
/// on (R4.4). Infinite where the ray never comes out, i.e. on a mesh that is open there, which is
/// not thin by this measure.
double thickness_at(const AABBMesh& mesh, const Vec3d& p, const Vec3d& n)
{
    const AABBMesh::hit_result hit = mesh.query_ray_hit(p - n * RAY_START_OFFSET, -n);
    // The ray starts the offset inside the surface, which is not thickness of the feature.
    return hit.is_hit() ?
        std::max(0., hit.distance() - RAY_START_OFFSET) :
        std::numeric_limits<double>::infinity();
}

/// Where in the plane around a point a probe looks.
Vec3d ring_offset(const TangentPlane& plane, double radius, int index)
{
    const double angle = 2. * std::numbers::pi * double(index) / double(RING_SAMPLES);
    return radius * (std::cos(angle) * plane.first + std::sin(angle) * plane.second);
}

/// The surface a probe finds by looking for it from under a spot around a point: the ray starts
/// some depth below the spot inside the material and goes the way the outward normal of the point
/// says, so it comes out at the first surface above that spot. Not valid where there is nothing to
/// hit, which is what the silhouette of the object and an overhang that no ray from below can see
/// both look like.
SurfaceSample probe_surface(
    const AABBMesh& mesh,
    const Vec3d& p,
    const Vec3d& n,
    const TangentPlane& plane,
    const Vec3d& offset
)
{
    for (const double depth : PROBE_DEPTHS) {
        const AABBMesh::hit_result hit = mesh.query_ray_hit(p + offset - n * depth, n);
        // No surface above this spot at all, or the ray entered the model instead of leaving it,
        // which says the start was not inside the material: a shallower start may still be.
        if (!hit.is_hit() || !hit.is_inside())
            continue;

        SurfaceSample sample;
        sample.pos   = hit.position();
        sample.valid = true;

        const Vec3d d = sample.pos - p;
        sample.u      = d.dot(plane.first);
        sample.v      = d.dot(plane.second);
        sample.h      = d.dot(n);
        return sample;
    }

    return {};
}

/// The best fit plane of the samples of a ring, which is what R4.5 measures the bump of a point
/// against. Empty where there are too few samples, or samples in too few directions to tell a
/// plane from a line, which is what a point at the rim of a silhouette or on a knife edge gives.
std::optional<LocalSurface> fit_plane(const std::vector<SurfaceSample>& samples)
{
    if (samples.size() < 3)
        return std::nullopt;

    Eigen::Matrix3d normal_equations = Eigen::Matrix3d::Zero();
    Eigen::Vector3d sums             = Eigen::Vector3d::Zero();
    for (const SurfaceSample& s : samples) {
        const Eigen::Vector3d row{1., s.u, s.v};
        normal_equations += row * row.transpose();
        sums += row * s.h;
    }

    const Eigen::FullPivLU<Eigen::Matrix3d> lu(normal_equations);
    if (!lu.isInvertible())
        return std::nullopt;

    const Eigen::Vector3d plane = lu.solve(sums);
    if (!plane.allFinite())
        return std::nullopt;

    LocalSurface surface;
    surface.at_origin = plane[0];
    surface.slope     = Vec2d{plane[1], plane[2]};
    return surface;
}

/// The plane of the surface around a point, if the surface around it is a flat one. The far ring
/// says which plane, the two rings inside it whether the surface is really in it: a curve of a
/// sphere or a cylinder leaves them off the plane as well, so the two are told apart by the
/// neighbourhood and not by the distance of the point alone.
std::optional<LocalSurface>
probe_surface_around(const AABBMesh& mesh, const Vec3d& p, const Vec3d& n, double flatness)
{
    const TangentPlane plane = tangent_plane(n);

    std::vector<SurfaceSample> far;
    far.reserve(RING_SAMPLES);
    for (int i = 0; i < RING_SAMPLES; ++i) {
        SurfaceSample sample =
            probe_surface(mesh, p, n, plane, ring_offset(plane, FAR_RING_RADIUS, i));
        if (sample.valid)
            far.push_back(std::move(sample));
    }

    const std::optional<LocalSurface> fitted = fit_plane(far);
    if (!fitted.has_value())
        return std::nullopt;

    for (const double radius : {NEAR_RING_RADIUS, MIDDLE_RING_RADIUS}) {
        for (int i = 0; i < RING_SAMPLES; ++i) {
            const SurfaceSample sample =
                probe_surface(mesh, p, n, plane, ring_offset(plane, radius, i));
            // Nothing to measure in this direction is not evidence of anything, the plane is decided
            // by the samples that are there.
            if (!sample.valid)
                continue;
            if (std::abs(sample.h - fitted->height_at(sample.u, sample.v)) > flatness)
                return std::nullopt;
        }
    }

    return fitted;
}

/// Whether the point stands out of the surface around it, i.e. sits on small surface detail such as
/// a rivet or a stud (R4.5). The height is measured along the outward normal of the point, so a
/// stud under a plate counts as high and a dent in the plate as low.
///
/// It has to be small, so there is a height above which it is not: a point that stands further out
/// of the surface around it than a point may move to get off it hangs in a gap under something else
/// (under the brim of a hat, in the throat of a cup) rather than on a detail it could step onto the
/// plain surface next to, and R4.5 asks for neither a move nor a minimum tip there.
bool is_raised_detail(const LocalSurface& surface, double bulge_mm, double max_height_mm)
{
    const double height = -surface.at_origin;
    return height > bulge_mm && height <= max_height_mm;
}

/// The point of a layer part the way a scaled point is built from an unscaled one, so the parts can
/// be asked whether they hold it.
Point layer_point(const Vec3d& p)
{
    return {Biz::Algorithms::Scaling::scaled(p.x()), Biz::Algorithms::Scaling::scaled(p.y())};
}

/// Whether the model has material at a spot on the layers around it, which is what a support moved
/// there still carries: the same island or the same overhang, not some other surface nearby. The
/// window is as tall as the distance a point may move, so the plate a rivet hangs under is in it
/// even though the rivet itself is on none of these layers.
bool carries_the_same_part(const Layers& layers, const Vec3d& q, double z_window)
{
    const Point xy = layer_point(q);
    for (const Layer& layer : layers) {
        if (std::abs(double(layer.print_z) - q.z()) > z_window)
            continue;
        for (const LayerPart& part : layer.parts)
            if (part.shape_extent.contains(xy)
                && Biz::Algorithms::ExPolygon::contains(*part.shape, xy))
                return true;
    }

    return false;
}

/// The plain surface next to small surface detail, for a point that sits on it (R4.5). The nearest
/// spot that is on the surface around the point rather than on the detail itself, faces down at
/// least as steeply as the point did, and still carries the same island or overhang. Only the spot
/// comes back: the head radius of the point is not touched, the support is the same one, only where
/// it holds the model moves.
std::optional<SurfaceSample> find_plain_spot(
    const AABBMesh& mesh,
    const Layers& layers,
    const Vec3d& p,
    const Vec3d& n,
    const LocalSurface& surface,
    double layer_height,
    const SupportRoleThresholds& thresholds
)
{
    const TangentPlane plane = tangent_plane(n);
    std::optional<SurfaceSample> best;
    double best_distance = std::numeric_limits<double>::max();

    for (const double offset : MOVE_OFFSETS) {
        for (int i = 0; i < RING_SAMPLES; ++i) {
            SurfaceSample sample = probe_surface(mesh, p, n, plane, ring_offset(plane, offset, i));
            if (!sample.valid)
                continue;

            const double distance = (sample.pos - p).norm();
            if (distance > thresholds.detail_move_radius_mm || distance >= best_distance)
                continue;

            // On the surface around the point rather than on the detail: as far off the plane the
            // neighbourhood was fitted to as that plane may be trusted, which is what keeps the
            // search off a second rivet and on the plain surface between two.
            if (std::abs(sample.h - surface.height_at(sample.u, sample.v))
                > thresholds.detail_flatness_mm)
                continue;

            // Still a surface a support can hold: facing down at least as steeply as the point was,
            // so it carries what the point carried and not less of it.
            sample.normal = outward_normal_at(mesh, sample.pos);
            if (!has_normal(sample.normal) || sample.normal.z() > n.z())
                continue;

            // And still the same island or overhang.
            if (!carries_the_same_part(
                    layers,
                    sample.pos,
                    std::max(thresholds.detail_move_radius_mm, layer_height)
                ))
                continue;

            best          = std::move(sample);
            best_distance = distance;
        }
    }

    return best;
}

/// The area of a layer part, in mm2: an ExPolygon area comes back in scaled units, which are a
/// millionth of a millimetre.
double part_area_mm2(const LayerPart& part)
{
    constexpr double scaling_sq =
        Biz::Algorithms::Scaling::SCALING_FACTOR * Biz::Algorithms::Scaling::SCALING_FACTOR;
    return Biz::Algorithms::ExPolygon::area(*part.shape) * scaling_sq;
}

/// How wide the feature a layer part is, in mm, as the larger of the two sides of its footprint.
/// The whole footprint is what matters and not its narrow way: a part that fits inside the threshold
/// on both sides is a speck a support under it would take with it, however long it reaches.
double part_footprint_mm(const LayerPart& part)
{
    const Point size = Biz::Algorithms::BoundingBox::sizes(part.shape_extent);
    return std::max(
        Biz::Algorithms::Scaling::unscaled<double>(size.x()),
        Biz::Algorithms::Scaling::unscaled<double>(size.y())
    );
}

/// The part of the model a point lies on: the layer closest to the point that is within one layer
/// height of it, and the part on that layer that holds the point. Nothing when there is none, and
/// then there is no island and no overhang here to measure.
LayerAtPoint layer_part_at(const Layers& layers, const Vec3d& p, double layer_height)
{
    const auto next = std::lower_bound(
        layers.begin(),
        layers.end(),
        float(p.z()),
        [](const Layer& layer, float z) { return layer.print_z < z; }
    );

    // The layers are ordered by their height and the point is on the surface of one of them, so the
    // one to look at is the one found and its neighbours. A few of them, because a point that was
    // moved onto the surface can sit more than one layer height away from where it was sampled.
    const size_t centre = static_cast<size_t>(next - layers.begin());
    const size_t first  = centre > 3 ? centre - 3 : 0;
    const size_t last   = std::min(centre + 3, layers.size());

    const Point xy = layer_point(p);
    LayerAtPoint result;
    double closest = std::numeric_limits<double>::max();
    for (size_t i = first; i < last; ++i) {
        const Layer& layer    = layers[i];
        const double distance = std::abs(double(layer.print_z) - p.z());
        if (distance > layer_height || distance >= closest)
            continue;
        for (const LayerPart& part : layer.parts) {
            if (!part.shape_extent.contains(xy)
                || !Biz::Algorithms::ExPolygon::contains(*part.shape, xy))
                continue;
            result.part  = &part;
            result.layer = &layer;
            closest      = distance;
            break;
        }
    }

    return result;
}

/// Whether the part of the model a point stands on leads up to a narrow one over the first layers
/// above it, which is what the tip of a spike looks like: the feature tapers away above the point
/// instead of carrying weight there.
bool narrow_above(const LayerPart& part, const SupportRoleThresholds& thresholds)
{
    const LayerPart* current = &part;
    for (size_t i = 0; i < thresholds.spike_layers; ++i) {
        // Nothing above this part: the model ends here, so there is no narrower part above it.
        if (current->next_parts.empty())
            return false;
        current = &*current->next_parts.front();
        if (part_footprint_mm(*current) < thresholds.thin_feature_mm)
            return true;
    }

    return false;
}

/// Whether a point is on a thin, fragile feature, which is what makes its support take the minimum
/// tip so that removing the support does not snap the feature off (R4.4).
bool is_fragile(
    const Vec3d& p,
    double thickness,
    const Layers& layers,
    double layer_height,
    const SupportRoleThresholds& thresholds
)
{
    // The thickness of the feature itself, measured by the ray into the model from under the point.
    if (thickness < thresholds.thin_feature_mm)
        return true;

    const LayerAtPoint at = layer_part_at(layers, p, layer_height);
    if (!at.found())
        return false; // the point is on no part of any layer: nothing here to be thin

    // The island or the overhang part the point is on is a speck: narrower than the tip that would
    // be put under it, however tall it is.
    if (part_footprint_mm(*at.part) < thresholds.thin_feature_mm)
        return true;

    // The tip of a spike: nothing is below the point, and the part it stands on is narrow over the
    // first layers above it. A part that widens right away is the top of a thin pillar or of a
    // small island, not the tip of a spike.
    return at.part->prev_parts.empty() && narrow_above(*at.part, thresholds);
}

} // namespace

void classify_support_point_roles(
    Domain::SLA::SupportPoints& points,
    const AABBMesh& mesh,
    const Layers& layers,
    double layer_height,
    const SupportRoleThresholds& thresholds,
    ThrowOnCancel throw_on_cancel
)
{
    if (points.empty() || !(layer_height > 0.))
        return;

    // R4.1: the lowest point of the object in the pose it is printed in, and every island point
    // within a couple of layers of it. A model with no island point at all has no anchor here, and
    // then the band below is out of reach of every point.
    double anchor_z = std::numeric_limits<double>::infinity();
    for (const SupportPoint& point : points)
        if (point.is_island())
            anchor_z = std::min(anchor_z, double(point.pos.z()));
    const double anchor_top = anchor_z + thresholds.anchor_layers * layer_height;

    for (SupportPoint& point : points) {
        throw_on_cancel();

        Vec3d p = point.pos.cast<double>();
        Vec3d n = outward_normal_at(mesh, p);
        if (!has_normal(n)) {
            // A point that is not on the model has no surface to measure. It carries nothing that
            // says it is thin, and it is no island either, so it is an overhang like any other.
            point.role = Role::Overhang;
            continue;
        }

        double thickness = thickness_at(mesh, p, n);

        // R4.5 comes first, because a point on a rivet or a stud holds whatever is under it from the
        // plain spot next to it, and that spot is what the rest of the rules are then asked about.
        const std::optional<LocalSurface> around =
            probe_surface_around(mesh, p, n, thresholds.detail_flatness_mm);
        if (around.has_value()
            && is_raised_detail(
                *around,
                thresholds.detail_bulge_mm,
                thresholds.detail_move_radius_mm
            ))
        {
            const std::optional<SurfaceSample> spot =
                find_plain_spot(mesh, layers, p, n, *around, layer_height, thresholds);
            if (!spot.has_value()) {
                // Nowhere plain to move to, so the point stays on the detail and takes the minimum
                // tip, which is the other half of R4.5.
                point.role = Role::Fragile;
                continue;
            }

            p         = spot->pos;
            n         = spot->normal;
            point.pos = p.cast<float>();
            thickness = thickness_at(mesh, p, n);
        }

        // R4.4 and R4.5 both land here: a thin feature, and a point on detail that cannot move.
        if (is_fragile(p, thickness, layers, layer_height, thresholds)) {
            point.role = Role::Fragile;
            continue;
        }

        if (point.is_island()) {
            // R4.3, and with R4.1 the size of the island the point starts: the lowest island of the
            // object is carried by its heavy anchors, every other island by its own size.
            if (p.z() <= anchor_top) {
                point.role = Role::Anchor;
                continue;
            }

            const LayerAtPoint at = layer_part_at(layers, p, layer_height);
            const double area     = at.found() ? part_area_mm2(*at.part) : 0.;
            point.role = area < thresholds.small_island_area_mm2 ? Role::SmallIsland : Role::Island;
            continue;
        }

        // R4.6: an overhang and everything else the rules say nothing about is a light support.
        point.role = Role::Overhang;
    }
}

} // namespace Slic3r::sla
