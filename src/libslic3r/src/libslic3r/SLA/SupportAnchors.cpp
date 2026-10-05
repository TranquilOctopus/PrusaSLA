#include "libslic3r/SLA/SupportAnchors.hpp"

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"

#include <Eigen/Dense>
#include <Eigen/Geometry>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Slic3r::sla {

using Domain::Vec2d;
using Domain::Vec3d;
using Domain::SLA::SupportPoint;
using Domain::SLA::SupportPoints;
using Domain::SLA::SupportPointType;
using Role = SupportPoint::Role;

namespace {

// How often a walk over the facets, the spots or the points of a model asks whether the caller wants
// to stop (M4.16).
constexpr size_t cancel_check_every = 256;

void ask_cancel(size_t i, const ThrowOnCancel& throw_on_cancel)
{
    if ((i % cancel_check_every) == 0)
        throw_on_cancel();
}

// How far off the surface the ray that measures a thickness starts, in mm. A ray cast from a surface
// can hit the very facet it starts on, which measures nothing. The offset M7.8.2 uses for it.
constexpr double ray_start_offset = 0.05; // [in mm]

// How deep a probe starts below the spot it looks at, from the deepest to the shallowest, in mm. The
// start has to be inside the material for the ray to come out at the first surface above the spot,
// and not at the far side of a thin part or at nothing at all. How deep the material is under a spot
// is only known once the ray is cast, so the deepest start that is inside wins: the shallowest start
// is inside the material wherever the spot is on the surface, so a spot that is measured at all is
// measured by one of them. The same starts and the same reason as M7.8.2.
constexpr std::array<double, 4> probe_depths{1.0, 0.5, 0.2, 0.05}; // [in mm]

// The rings the surface around a spot is probed on, in mm, and how many directions each of them is
// probed in. The far ring is the ~3 mm neighbourhood R4.2 asks a spot to be flat over and R4.5
// measures a bump against, the near ring says whether the surface around the spot is in that plane at
// all: a curve of a sphere or of a cylinder and a rivet standing on a plate are told apart by the
// neighbourhood and not by the distance of the surface from a plane, which both leave it at. A fixed
// number of directions, so that the same model always gets the same anchors.
constexpr double near_ring_mm = 1.6; // [in mm]
constexpr double far_ring_mm  = 3.0; // [in mm]
constexpr int ring_samples    = 8;

// The two keys of R4.2 that come before the distance to the anchors already placed are compared in
// steps of this and not as they are: a spot that is twice as far from the anchors already placed is
// the better one even where the flat area around it is a tenth smaller, because eight anchors on one
// spot of a plate carry that plate no better than one.
constexpr double flat_area_step_mm2 = 10.; // [in mm2]
constexpr double height_step_mm     = 1.; // [in mm]

// A cell of the uniform grid the spots are looked for on, in the x-y plane. The cells are counted
// from the corner of the model, so that they are never negative and a hash of them is a hash of
// small numbers.
using Cell2 = std::pair<std::int32_t, std::int32_t>;

/// The hash of a cell of the grid, for the maps keyed by one. The standard library has no
/// std::hash for a pair of coordinates, so an unordered container of them asks for one: without it
/// MSVC stops on "term does not evaluate to a function taking 1 arguments" and no other standard
/// library gives a diagnostic at all. The two indices are mixed into one bucket index the way
/// Boost's hash_combine does, by two large odd primes, which spreads a whole row or column of cells
/// over the buckets; both indices are small non-negative numbers here (see cell_index), so nothing
/// of the address of a cell takes part.
struct Cell2Hash
{
    size_t operator()(const Cell2& cell) const noexcept
    {
        return (size_t(std::get<0>(cell)) * 73856093u) ^ (size_t(std::get<1>(cell)) * 19349663u);
    }
};

/// The map the cells of the grid are looked up in. Every container of this pass keyed by a cell
/// goes through this alias, so that none of them can be written without the hasher above.
using CellMap = std::unordered_map<Cell2, std::vector<int>, Cell2Hash>;

std::int32_t cell_index(double value, double origin, double spacing)
{
    const double index = std::floor((value - origin) / spacing);
    if (!std::isfinite(index) || index < 0.)
        return 0;
    // A model of a few hundred kilometres would be needed to reach this, and this is not one.
    return std::int32_t(std::min(index, double(std::numeric_limits<std::int32_t>::max())));
}

/// Whether a role is one of the two a heavy anchor of R4.2 carries: what the pass keeps apart from
/// each other, and what every anchor of a very large model is.
bool is_anchor(Role role)
{
    return role == Role::Anchor || role == Role::AnchorLarge;
}

/// The same, as the question to ask of a point.
bool is_anchor_point(const SupportPoint& point)
{
    return is_anchor(point.role);
}

/// Whether a point may become the anchor of a spot instead of a new point being added beside it: a
/// point of the generator that already stands on the spot, and not one the user placed by hand. A
/// fragile point never may (a heavy anchor on a thin feature is the one support that snaps it off,
/// R4.4), and an anchor is an anchor already.
bool may_be_promoted(const SupportPoint& point)
{
    if (point.type == SupportPointType::manual_add)
        return false;
    return point.role == Role::Overhang || point.role == Role::Island;
}

bool any_point(const SupportPoint&)
{
    return true;
}

/// One facet of the model: where it is, which way it faces, and whether it is a facet a heavy anchor
/// may stand on at all.
struct Facet
{
    Vec3d corner[3] = {};
    Vec3d centre;
    Vec3d normal; // of unit length, pointing out of the material
    int face             = 0; // the face of the mesh, in the order the mesh has it
    bool faces_the_plate = false;
};

/// The measures of one model the spots are decided by, worked out once for the whole pass so that the
/// walks over the facets and over the spots do not carry a list of numbers around.
struct ModelMeasures
{
    double lowest_z_mm   = 0.; // [in mm]
    double lower_band_mm = 0.; // the height above it a spot may be at, [in mm]
};

/// The facets of a model, without the ones that are of no use to an anchor: a facet with no area has
/// no normal to place a spot on, and a facet that points away from the build plate carries no anchor
/// of R4.2.
std::vector<Facet> facets_of(
    const AABBMesh& mesh,
    const ModelMeasures& measures,
    const SupportAnchorThresholds& thresholds
)
{
    const std::vector<Domain::Vec3f>& vertices = mesh.vertices();
    const std::vector<Domain::Index3>& indices = mesh.indices();

    std::vector<Facet> facets;
    facets.reserve(indices.size());
    for (size_t face = 0; face < indices.size(); ++face) {
        const Domain::Index3& ix = indices[face];
        Facet facet;
        facet.corner[0] = vertices[ix[0]].cast<double>();
        facet.corner[1] = vertices[ix[1]].cast<double>();
        facet.corner[2] = vertices[ix[2]].cast<double>();
        const Vec3d cross =
            (facet.corner[1] - facet.corner[0]).cross(facet.corner[2] - facet.corner[0]);
        const double twice_area = cross.norm();
        if (!(twice_area > 0.) || !std::isfinite(twice_area))
            continue;

        facet.normal = cross / twice_area;
        facet.centre = (facet.corner[0] + facet.corner[1] + facet.corner[2]) / 3.;
        facet.face   = int(face);

        // R4.2 "facing the plate": the surface has to point at the build plate this far down, and
        // R4.1 and R4.2 want the anchors in the lower part of the model, because they hold the print
        // early.
        facet.faces_the_plate = facet.normal.z() <= thresholds.down_facing_normal_z
            && facet.centre.z() - measures.lowest_z_mm <= measures.lower_band_mm;

        facets.push_back(facet);
    }
    return facets;
}

/// Two directions in the plane of the surface around a spot, so that the rings around it can be told
/// apart in that plane. Fixed for a given normal, so that the answer does not depend on the order the
/// spots are visited in.
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

/// Where in the plane around a spot a probe looks.
Vec3d ring_offset(const TangentPlane& plane, double radius, int index)
{
    const double angle = 2. * std::numbers::pi * double(index) / double(ring_samples);
    return radius * (std::cos(angle) * plane.first + std::sin(angle) * plane.second);
}

/// Where a probe came out of the model. Not valid where there is nothing to hit, which is what the
/// silhouette of the model, a spot near its rim and a bump standing on a plate all look like.
struct Probe
{
    Vec3d pos;
    bool valid = false;
};

/// The surface a probe finds by looking for it from under a spot around the one being looked at: the
/// ray starts some depth below that spot inside the material and goes the way the outward normal of
/// the spot says, so it comes out at the first surface above it. A ray that hits a face from the
/// wrong side is not a surface above the spot but one it looks at from inside, and it is not taken:
/// that is the far side of a thin part, the roof of a cavity and an open mesh.
Probe probe_above(const AABBMesh& mesh, const Vec3d& p, const Vec3d& n, const Vec3d& offset)
{
    for (const double depth : probe_depths) {
        const AABBMesh::hit_result hit = mesh.query_ray_hit(p + offset - n * depth, n);
        if (!hit.is_hit() || !hit.is_inside())
            continue;
        return {hit.position(), true};
    }
    return {};
}

/// Whether the surface around a spot is flat and free of the detail R4.2 keeps its anchors off: every
/// ray that leaves the model from under a spot around this one has to come out in the plane the spot
/// is on. This is the measure M7.8.2 moves a point off a stud with (R4.5), and it is asked of the
/// surface and not of the facets, so a rivet or a stud a millimetre across is found whatever the mesh
/// it is cut into facets of, and a surface that is only finely tessellated is not mistaken for
/// detail. Nothing to measure in a direction means the same thing: a spot at the rim of a model, on
/// the cap of a bump, or under the roof of a cavity has no flat surface around it to hold anything.
bool is_flat_around(
    const AABBMesh& mesh,
    const Vec3d& p,
    const Vec3d& n,
    const SupportAnchorThresholds& thresholds
)
{
    const TangentPlane plane = tangent_plane(n);
    for (const double radius : {near_ring_mm, far_ring_mm}) {
        for (int i = 0; i < ring_samples; ++i) {
            const Probe probe = probe_above(mesh, p, n, ring_offset(plane, radius, i));
            if (!probe.valid)
                return false;
            if (std::abs((probe.pos - p).dot(n)) > thresholds.detail_flatness_mm)
                return false;
        }
    }
    return true;
}

/// How thick the feature under a spot is, measured by a ray from the surface into the model: the ray
/// that comes out on the other side has travelled the thickness of what it went through (R4.4), the
/// same measure M7.8.2 classifies its points by. Infinite where the ray never comes out, i.e. on a
/// mesh that is open there, which is not thin by this measure.
double thickness_at(const AABBMesh& mesh, const Vec3d& p, const Vec3d& n)
{
    const AABBMesh::hit_result hit = mesh.query_ray_hit(p - n * ray_start_offset, -n);
    return hit.is_hit() ?
        std::max(0., hit.distance() - ray_start_offset) :
        std::numeric_limits<double>::infinity();
}

/// The grid of the plane the candidate spots are looked for on: one spot per cell, at its centre, so
/// that the spots of a flat area are spread evenly and two of them are never closer than the
/// spacing of the grid.
struct SpotGrid
{
    Vec2d origin   = Vec2d::Zero();
    double spacing = 3.; // [in mm]

    Cell2 cell_of(double x, double y) const
    {
        return Cell2{cell_index(x, origin.x(), spacing), cell_index(y, origin.y(), spacing)};
    }

    Vec2d centre_of(const Cell2& cell) const
    {
        return {
            origin.x() + (double(std::get<0>(cell)) + 0.5) * spacing,
            origin.y() + (double(std::get<1>(cell)) + 0.5) * spacing
        };
    }

    /// The cells within @p radius of a point, which is how the spots around a spot are found.
    template <typename Fn>
    void around(double x, double y, double radius, Fn&& visit) const
    {
        const Cell2 centre = cell_of(x, y);
        const int span     = int(std::ceil(radius / spacing));
        for (int dx = -span; dx <= span; ++dx)
            for (int dy = -span; dy <= span; ++dy)
                visit(Cell2{std::get<0>(centre) + dx, std::get<1>(centre) + dy});
    }

    /// The cells the footprint of a facet covers, which is where its spots are: the facet of a large
    /// flat area can be far from every spot it holds, so the facets are indexed by their own
    /// footprint and not by their centre.
    template <typename Fn>
    void cells_of(const Facet& facet, Fn&& visit) const
    {
        double lowest_x = facet.corner[0].x(), highest_x = lowest_x;
        double lowest_y = facet.corner[0].y(), highest_y = lowest_y;
        for (int i = 1; i < 3; ++i) {
            lowest_x  = std::min(lowest_x, facet.corner[i].x());
            highest_x = std::max(highest_x, facet.corner[i].x());
            lowest_y  = std::min(lowest_y, facet.corner[i].y());
            highest_y = std::max(highest_y, facet.corner[i].y());
        }

        const Cell2 first = cell_of(lowest_x, lowest_y);
        const Cell2 last  = cell_of(highest_x, highest_y);
        for (std::int32_t x = std::get<0>(first); x <= std::get<0>(last); ++x)
            for (std::int32_t y = std::get<1>(first); y <= std::get<1>(last); ++y)
                visit(Cell2{x, y});
    }
};

/// Whether a spot of the grid is inside a facet. A spot on the edge of a facet belongs to none of
/// them: it is on the seam between two, and the facet on the other side of the seam offers it.
bool holds(const Facet& facet, const Vec2d& xy)
{
    const double area2 =
        (facet.corner[1].x() - facet.corner[0].x()) * (facet.corner[2].y() - facet.corner[0].y())
        - (facet.corner[2].x() - facet.corner[0].x()) * (facet.corner[1].y() - facet.corner[0].y());
    if (!(std::abs(area2) > 0.))
        return false;

    for (int i = 0; i < 3; ++i) {
        const int next  = (i + 1) % 3;
        const double ax = facet.corner[i].x(), ay = facet.corner[i].y();
        const double bx = facet.corner[next].x(), by = facet.corner[next].y();
        // Inside the facet is the side of this edge the third corner is on, and the edge itself is on
        // the seam between the facets, which belongs to neither of them.
        if (((bx - ax) * (xy.y() - ay) - (by - ay) * (xy.x() - ax)) * area2 <= 0.)
            return false;
    }
    return true;
}

/// Where a spot of the grid stands on the facet that holds it: the grid runs in the x-y plane, so
/// the spot is where the normal of the facet through the point of the grid above it meets the facet.
Vec3d stands_on(const Facet& facet, const Vec2d& xy)
{
    const Vec3d above{xy.x(), xy.y(), facet.centre.z()};
    const double along = (above - facet.centre).dot(facet.normal);
    return above - facet.normal * along;
}

/// One place a heavy anchor of R4.2 may stand, with the three things the rule ranks the candidates
/// by.
struct Spot
{
    Vec3d pos;
    Vec3d normal;
    double flat_area_mm2      = 0.; // the flat, low-detail surface around the spot, [in mm2]
    double height_mm          = 0.; // above the lowest point of the model, [in mm]
    double anchor_distance_mm = 0.; // to the nearest anchor the model has, [in mm]
    bool taken                = false; // an anchor was placed here, or the spot was passed over
};

/// The spots the flat, low-detail, plate-facing surface of the model offers: the centre of every cell
/// of the grid that a facet facing the plate holds, whose surface around the spot is flat and whose
/// feature is not a thin one.
std::vector<Spot> make_spots(
    const AABBMesh& mesh,
    const std::vector<Facet>& facets,
    const SpotGrid& grid,
    const ModelMeasures& measures,
    const SupportAnchorThresholds& thresholds,
    const ThrowOnCancel& throw_on_cancel
)
{
    CellMap cells;
    for (size_t i = 0; i < facets.size(); ++i) {
        ask_cancel(i, throw_on_cancel);
        if (!facets[i].faces_the_plate)
            continue;
        grid.cells_of(facets[i], [&cells, i](const Cell2& cell) { cells[cell].push_back(int(i)); });
    }

    std::vector<Spot> spots;
    spots.reserve(cells.size());
    size_t visited = 0;
    for (const auto& cell_of_facets : cells) {
        ask_cancel(visited++, throw_on_cancel);
        const Cell2& cell = cell_of_facets.first;
        const Vec2d xy    = grid.centre_of(cell);

        // The facet the spot stands on: the first of those that hold it in the order of the mesh, so
        // that the same model always offers the same spots.
        const Facet* holder = nullptr;
        for (const int index : cell_of_facets.second) {
            const Facet& facet = facets[std::size_t(index)];
            if (holder != nullptr && facet.face >= holder->face)
                continue;
            if (holds(facet, xy))
                holder = &facet;
        }
        if (holder == nullptr)
            continue;

        Spot spot;
        spot.pos       = stands_on(*holder, xy);
        spot.normal    = holder->normal;
        spot.height_mm = spot.pos.z() - measures.lowest_z_mm;
        if (spot.height_mm < 0. || spot.height_mm > measures.lower_band_mm)
            continue;

        // R4.4 and R4.9: no heavy anchor on a thin, fragile feature, the one surface a heavy anchor is
        // never right on. R4.2 makes no exception of its own here, the exception of the rulebook is
        // the lowest-point anchor of R4.1, which this pass does not place. The detail of R4.5 is the
        // measure below, and M7.8.5 will ask the same question of a whole region in detailed surface
        // once it names the role for it.
        if (thickness_at(mesh, spot.pos, spot.normal) < thresholds.thin_feature_mm)
            continue;

        // R4.2 "flat, low-detail". The probe asks the surface and not the facets: the cap of a bump
        // has the flat surface it stands on a fraction of a millimetre above itself, which is off the
        // plane of the spot, and a spot at the rim of the model has no surface around it at all.
        if (!is_flat_around(mesh, spot.pos, spot.normal, thresholds))
            continue;

        spots.push_back(spot);
    }

    return spots;
}

/// The spots by the cell of the grid they stand in, which is how the flat area around a spot and the
/// spots beside a new anchor are found.
CellMap index_spots(const std::vector<Spot>& spots, const SpotGrid& grid)
{
    CellMap cells;
    for (size_t i = 0; i < spots.size(); ++i)
        cells[grid.cell_of(spots[i].pos.x(), spots[i].pos.y())].push_back(int(i));
    return cells;
}

/// How much flat, low-detail surface is around each spot, the first of the three things R4.2 ranks
/// the candidates by. The spots are measured against each other and not against the facets, so this
/// says how deep in a flat area a spot is: a spot beside a bump, a step or the rim of the model has
/// fewer spots around it, because those are not spots at all.
void measure_flat_areas(
    std::vector<Spot>& spots,
    const SpotGrid& grid,
    const CellMap& cells,
    double radius,
    const ThrowOnCancel& throw_on_cancel
)
{
    const double radius_sq = radius * radius;
    for (size_t i = 0; i < spots.size(); ++i) {
        ask_cancel(i, throw_on_cancel);
        size_t count = 0;
        grid.around(
            spots[i].pos.x(),
            spots[i].pos.y(),
            radius,
            [&](const Cell2& cell)
            {
                const auto found = cells.find(cell);
                if (found == cells.end())
                    return;
                for (const int index : found->second)
                    if ((spots[std::size_t(index)].pos - spots[i].pos).squaredNorm() <= radius_sq)
                        ++count;
            }
        );
        spots[i].flat_area_mm2 = double(count) * grid.spacing * grid.spacing;
    }
}

/// How far a spot is from the anchors the model already has, the third of the three things R4.2 ranks
/// the candidates by. Only the anchors are looked at: the rule is about keeping the heavy anchors
/// apart from each other and not about the light points between them. The answer is a lookup in the
/// cells around the spot, and a spot with no anchor in them is as far from every anchor as any other
/// spot without one: the spots beside an anchor that has just been placed are pushed back on purpose
/// (see demote_around below), which is what spreads the anchors.
class AnchorIndex
{
public:
    AnchorIndex(const SupportPoints& points, const SpotGrid& grid) : m_points(&points), m_grid(grid)
    {
        for (size_t i = 0; i < points.size(); ++i)
            if (is_anchor(points[i].role))
                m_cells[grid.cell_of(points[i].pos.x(), points[i].pos.y())].push_back(int(i));
    }

    double nearest(const Vec3d& p, double radius) const
    {
        const Cell2 centre = m_grid.cell_of(p.x(), p.y());
        const int span     = int(std::ceil(radius / m_grid.spacing));
        double best        = std::numeric_limits<double>::max();
        for (int dx = -span; dx <= span; ++dx)
            for (int dy = -span; dy <= span; ++dy) {
                const auto found =
                    m_cells.find(Cell2{std::get<0>(centre) + dx, std::get<1>(centre) + dy});
                if (found == m_cells.end())
                    continue;
                for (const int index : found->second) {
                    const Vec3d at = m_points->at(std::size_t(index)).pos.cast<double>();
                    best           = std::min(best, (at - p).norm());
                }
            }
        return best == std::numeric_limits<double>::max() ?
            std::numeric_limits<double>::infinity() :
            best;
    }

private:
    const SupportPoints* m_points = nullptr;
    SpotGrid m_grid;
    CellMap m_cells;
};

/// The spots around an anchor that has just been placed are not tried before the ones further away
/// from it. That is what spreads the anchors of R4.2 over the flat area instead of stacking them on
/// the one spot with the most flat surface around it.
void demote_around(
    std::vector<Spot>& spots,
    const SpotGrid& grid,
    const CellMap& cells,
    const Vec3d& p,
    double radius
)
{
    const double radius_sq = radius * radius;
    grid.around(
        p.x(),
        p.y(),
        radius,
        [&](const Cell2& cell)
        {
            const auto found = cells.find(cell);
            if (found == cells.end())
                return;
            for (const int index : found->second)
                if ((spots[std::size_t(index)].pos - p).squaredNorm() <= radius_sq)
                    spots[std::size_t(index)].anchor_distance_mm = 0.;
        }
    );
}

/// The order the spots are tried in (R4.2): the flat area around the spot, then how low the spot is
/// on the model, then how far it is from the anchors that are already there, and the place of the
/// spot itself last, so that the same model always gets the same anchors in the same order.
bool better_spot(const Spot& a, const Spot& b)
{
    const auto area_of = [](const Spot& spot)
    { return long(std::floor(spot.flat_area_mm2 / flat_area_step_mm2)); };
    const auto height_of = [](const Spot& spot)
    { return long(std::floor(spot.height_mm / height_step_mm)); };

    if (area_of(a) != area_of(b))
        return area_of(a) > area_of(b);
    if (height_of(a) != height_of(b))
        return height_of(a) < height_of(b);
    if (std::abs(a.anchor_distance_mm - b.anchor_distance_mm) > 1e-6)
        return a.anchor_distance_mm > b.anchor_distance_mm;
    if (std::abs(a.pos.x() - b.pos.x()) > 1e-6)
        return a.pos.x() < b.pos.x();
    if (std::abs(a.pos.y() - b.pos.y()) > 1e-6)
        return a.pos.y() < b.pos.y();
    return a.pos.z() < b.pos.z();
}

/// The point nearest to a spot that @p accept takes, and nothing when there is none within @p radius.
/// Two points the same distance from the spot are settled by the order they are in, so that the
/// answer does not depend on which of them the walk reached first.
template <typename Accept>
SupportPoint* nearest_within(SupportPoints& points, const Vec3d& p, double radius, Accept accept)
{
    const double radius_sq  = radius * radius;
    SupportPoint* best      = nullptr;
    double best_distance_sq = radius_sq;
    for (SupportPoint& point : points) {
        if (!accept(point))
            continue;
        const double distance_sq = (point.pos.cast<double>() - p).squaredNorm();
        if (distance_sq >= best_distance_sq)
            continue;
        best             = &point;
        best_distance_sq = distance_sq;
    }
    return best;
}

/// How many heavy anchors R4.2 asks for: two for a footprint that fits in 30 x 30 mm, one more for
/// every 400 mm2 of footprint above that, and never more than eight.
size_t anchor_count(
    const SupportAnchorThresholds& thresholds,
    double footprint_x_mm,
    double footprint_y_mm
)
{
    const double small = thresholds.small_footprint_mm * thresholds.small_footprint_mm;
    const double area  = footprint_x_mm * footprint_y_mm;

    double count = double(thresholds.small_footprint_anchors);
    if (area > small && thresholds.footprint_per_extra_anchor_mm2 > 0.)
        count += std::floor((area - small) / thresholds.footprint_per_extra_anchor_mm2);
    if (count > double(thresholds.max_anchors))
        count = double(thresholds.max_anchors);
    if (!(count > 0.))
        count = 0.;
    return size_t(count);
}

} // namespace

AnchorPlacement add_heavy_anchors(
    SupportPoints& points,
    const AABBMesh& mesh,
    const SupportAnchorThresholds& thresholds,
    float head_front_radius,
    ThrowOnCancel throw_on_cancel
)
{
    AnchorPlacement result;
    if (mesh.vertices().empty() || mesh.indices().empty() || !(thresholds.spot_spacing_mm > 0.))
        return result;

    const Eigen::AlignedBox<float, 3> box = mesh.bounding_box();
    const double lowest_x                 = double(box.min().x());
    const double lowest_y                 = double(box.min().y());
    const double lowest_z                 = double(box.min().z());
    const double footprint_x              = double(box.max().x()) - lowest_x;
    const double footprint_y              = double(box.max().y()) - lowest_y;
    if (!std::isfinite(footprint_x) || !std::isfinite(footprint_y) || !std::isfinite(lowest_z))
        return result;

    // R4.2: how many anchors the footprint of the model asks for, and whether the model is a very
    // large one, whose anchors take the largest tip (T0.6) instead of the heavy one.
    result.footprint_mm2 = footprint_x * footprint_y;
    result.wanted        = anchor_count(thresholds, footprint_x, footprint_y);
    result.large =
        footprint_x > thresholds.large_footprint_mm && footprint_y > thresholds.large_footprint_mm;

    const Role anchor_role = result.large ? Role::AnchorLarge : Role::Anchor;
    if (result.large)
        // The size table of the rulebook gives T0.6 to the anchors of a very large object, the
        // lowest point of R4.1 among them: the rule is about the anchors of such a model and not
        // about the ones this pass adds only.
        for (SupportPoint& point : points)
            if (point.role == Role::Anchor)
                point.role = anchor_role;

    // The measures of this model, the facets of it that face the plate, and the spots they offer.
    ModelMeasures measures;
    measures.lowest_z_mm   = lowest_z;
    measures.lower_band_mm = std::max(
        thresholds.min_lower_band_mm,
        thresholds.lower_band_ratio * (double(box.max().z()) - lowest_z)
    );

    const std::vector<Facet> facets = facets_of(mesh, measures, thresholds);

    // The grid of the spots, which is the spacing of the rule where the footprint of the model is
    // small enough for it and as wide as the cap of spots allows where it is not.
    double spacing = thresholds.spot_spacing_mm;
    if (thresholds.max_candidate_spots > 0) {
        const double area_per_spot = result.footprint_mm2 / double(thresholds.max_candidate_spots);
        if (area_per_spot > spacing * spacing)
            spacing = std::sqrt(area_per_spot);
    }
    const SpotGrid grid{Vec2d{lowest_x, lowest_y}, spacing};
    std::vector<Spot> spots = make_spots(mesh, facets, grid, measures, thresholds, throw_on_cancel);
    result.candidates       = spots.size();
    if (spots.empty() || result.wanted == 0)
        return result;

    const CellMap cells = index_spots(spots, grid);
    measure_flat_areas(spots, grid, cells, thresholds.spot_area_radius_mm, throw_on_cancel);
    const AnchorIndex anchors(points, grid);
    for (Spot& spot : spots)
        spot.anchor_distance_mm = anchors.nearest(spot.pos, thresholds.spread_radius_mm);

    // The heavy anchors themselves (R4.2): at most one per spot, and at most the number the
    // footprint of the model asks for.
    for (size_t round = 0; round < result.wanted; ++round) {
        ask_cancel(round, throw_on_cancel);

        int best = -1;
        for (size_t i = 0; i < spots.size(); ++i) {
            if (spots[i].taken)
                continue;
            if (best < 0 || better_spot(spots[i], spots[std::size_t(best)]))
                best = int(i);
        }
        if (best < 0)
            break; // every spot of the model has been tried

        Spot& spot = spots[std::size_t(best)];
        spot.taken = true;

        // Two anchors a spacing apart would carry the same flat area twice, and the point of R4.2 is
        // the areas and not the number of supports on them.
        if (nearest_within(points, spot.pos, thresholds.spot_spacing_mm, is_anchor_point)
            != nullptr)
            continue;

        // A point of the generator that stands on this spot holds it better than a new point beside
        // it: one support of one area, and it is the one that is already there.
        SupportPoint* held =
            nearest_within(points, spot.pos, thresholds.promote_radius_mm, may_be_promoted);
        if (held != nullptr) {
            held->role = anchor_role;
            ++result.placed;
            ++result.promoted;
            demote_around(
                spots,
                grid,
                cells,
                held->pos.cast<double>(),
                thresholds.spread_radius_mm
            );
            continue;
        }

        // Nothing stands near the spot and the generator has no point on it, so the anchor is a new
        // point, at least a spacing away from every other point of the model.
        if (nearest_within(points, spot.pos, thresholds.spot_spacing_mm, any_point) != nullptr)
            continue;

        SupportPoint anchor;
        anchor.pos               = spot.pos.cast<float>();
        anchor.head_front_radius = head_front_radius;
        // The anchor stands under the model, which is what an island point of the generator is, and
        // the tool sizes a point like any other point the generator made.
        anchor.type = SupportPointType::island;
        anchor.role = anchor_role;
        points.push_back(anchor);
        ++result.placed;
        ++result.added;
        demote_around(spots, grid, cells, spot.pos, thresholds.spread_radius_mm);
    }

    return result;
}

} // namespace Slic3r::sla
