#include <libslic3r/SLA/CavityDetection.hpp>

#include <libslic3r/ClipperUtils.hpp>

#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <numeric>
#include <vector>

namespace Slic3r::SLA {

namespace {

using Domain::ExPolygon;
using Domain::ExPolygons;
using Domain::Point;
using Domain::Vec2d;

/// Coordinates are scaled up, so a scaled area is a mm² area divided by SCALING_FACTOR squared.
constexpr double sf = Biz::Algorithms::Scaling::SCALING_FACTOR;

/// Areas below this are noise. 1e-6 mm² is a square micrometre, far under anything a printer can
/// build, and above what the clipper arithmetic leaves behind as dust.
constexpr double eps_area_mm2 = 1e-6;

double area_mm2(const ExPolygons& regions)
{
    double total = 0.;
    for (const ExPolygon& region : regions)
        total += std::abs(region.area()) * sf * sf;
    return total;
}

/// The centre of mass of a set of regions, in mm, weighted by area.
Vec2d centroid_mm(const ExPolygons& regions)
{
    double total = 0.;
    Vec2d sum    = Vec2d::Zero();
    for (const ExPolygon& region : regions) {
        const double area = region.area();
        if (area <= 0.)
            continue;
        const Point centre = region.contour.centroid();
        sum += Vec2d(unscaled<double>(centre.x()), unscaled<double>(centre.y())) * area;
        total += area;
    }
    if (total > 0.)
        return Vec2d(sum / total);
    return Vec2d::Zero();
}

/// The empty regions of a layer: every hole of every ExPolygon, less the solid of the ExPolygons
/// that stand inside one of them (a post in a hollow print, a support in a pocket). Holes of one
/// layer never overlap each other: the layer is a union, so the result is one region per void.
ExPolygons holes_of(const ExPolygons& layer)
{
    ExPolygons voids;
    for (const ExPolygon& region : layer) {
        voids.reserve(voids.size() + region.holes.size());
        for (const Domain::Polygon& hole : region.holes) {
            ExPolygon ccw;
            ccw.contour = hole;
            std::reverse(ccw.contour.points.begin(), ccw.contour.points.end());
            voids.push_back(std::move(ccw));
        }
    }
    return diff_ex(voids, layer);
}

/// The bounding box of a set of regions, kept so that the pairing of two layers can throw out the
/// pairs that cannot touch before it runs a boolean operation on them.
struct Bounds
{
    Domain::coord_t min_x = 0;
    Domain::coord_t min_y = 0;
    Domain::coord_t max_x = 0;
    Domain::coord_t max_y = 0;
    bool empty            = true;
};

Bounds bounds_of(const ExPolygon& region)
{
    Bounds bounds;
    for (const Point& point : region.contour.points) {
        if (bounds.empty) {
            bounds.min_x = bounds.max_x = point.x();
            bounds.min_y = bounds.max_y = point.y();
            bounds.empty                = false;
        } else {
            bounds.min_x = std::min(bounds.min_x, point.x());
            bounds.max_x = std::max(bounds.max_x, point.x());
            bounds.min_y = std::min(bounds.min_y, point.y());
            bounds.max_y = std::max(bounds.max_y, point.y());
        }
    }
    return bounds;
}

Bounds bounds_of(const ExPolygons& regions)
{
    Bounds bounds;
    for (const ExPolygon& region : regions) {
        const Bounds one = bounds_of(region);
        if (one.empty)
            continue;
        if (bounds.empty) {
            bounds = one;
            continue;
        }
        bounds.min_x = std::min(bounds.min_x, one.min_x);
        bounds.max_x = std::max(bounds.max_x, one.max_x);
        bounds.min_y = std::min(bounds.min_y, one.min_y);
        bounds.max_y = std::max(bounds.max_y, one.max_y);
    }
    return bounds;
}

bool may_collide(const Bounds& lhs, const Bounds& rhs)
{
    if (lhs.empty || rhs.empty)
        return false;
    return lhs.min_x <= rhs.max_x
        && rhs.min_x <= lhs.max_x
        && lhs.min_y <= rhs.max_y
        && rhs.min_y <= lhs.max_y;
}

/// Do two regions share area, as opposed to only touching along a boundary?
bool overlaps(const ExPolygons& lhs, const ExPolygons& rhs)
{
    return area_mm2(intersection_ex(lhs, rhs)) > eps_area_mm2;
}

/// Union-find over the holes of two consecutive layers: a hole that overlaps a region carried over
/// from below is that same region one layer higher, and regions that merge into one hole above
/// merge here as well.
class DisjointSet
{
    std::vector<size_t> m_parent;

public:
    explicit DisjointSet(size_t count) : m_parent(count)
    {
        std::iota(m_parent.begin(), m_parent.end(), size_t(0));
    }

    size_t find(size_t index)
    {
        while (m_parent[index] != index) {
            m_parent[index] = m_parent[m_parent[index]];
            index           = m_parent[index];
        }
        return index;
    }

    void unite(size_t lhs, size_t rhs)
    {
        const size_t l = find(lhs);
        const size_t r = find(rhs);
        if (l != r)
            m_parent[r] = l;
    }
};

/// One empty region followed from layer to layer, for as long as it stays a hole.
struct RegionTrack
{
    ExPolygons region; //< the region on the layer it was last seen on
    Bounds bounds; //< the bounding box of that region
    size_t first_layer = 0; //< the lowest layer the region is a hole of
    size_t last_layer  = 0; //< the highest layer the region is a hole of
    double volume_mm3  = 0.; //< the hole area times the layer thickness, summed over the layers
    ExPolygons opening; //< the part of the region on the first layer with no solid under it
};

struct Group
{
    std::vector<size_t> tracks; //< the tracks of the layer below that end up in this region
    std::vector<size_t> holes; //< the holes of this layer the region continues as
};

/// Start a track for a hole of @p layer that nothing below carries over, so its opening is the part
/// of it the layer under @p layer does not cover. A hole on the first layer is open below in full.
RegionTrack start_track(const ExPolygons& region,
                        size_t layer_idx,
                        const std::vector<ExPolygons>& layers)
{
    RegionTrack track;
    track.region      = region;
    track.bounds      = bounds_of(region);
    track.first_layer = layer_idx;
    if (layer_idx == 0)
        track.opening = region;
    else
        track.opening = diff_ex(region, layers[layer_idx - 1]);
    return track;
}

/// Fold every track of a group into one: they are disjoint regions, so their volumes add up, the
/// pocket reaches back to the lowest of their first layers, and it is as open below as the most
/// open of them.
RegionTrack merge_tracks(const std::vector<RegionTrack>& tracks, const std::vector<size_t>& members)
{
    RegionTrack merged = tracks[members.front()];
    for (const size_t member : members) {
        if (member == members.front())
            continue; // already in merged
        const RegionTrack& track = tracks[member];
        merged.volume_mm3 += track.volume_mm3;
        if (track.first_layer < merged.first_layer)
            merged.first_layer = track.first_layer;
        if (area_mm2(track.opening) > area_mm2(merged.opening))
            merged.opening = track.opening;
    }
    return merged;
}

/// Classify a region that is no longer a hole of the layer above it and report it when it is a
/// problem. @p solid_above is that layer.
///
/// The region is closed above when @p solid_above covers all of it; a region that opens into the
/// outside there is a vent and traps nothing. Closed above, the region is a cup when its opening is
/// wide enough to seal against the vat film, and trapped resin when it has no opening at all.
void finish_track(const RegionTrack& track,
                  const ExPolygons& solid_above,
                  const CavityDetectionOptions& opts,
                  CavityAnalysis& out)
{
    if (area_mm2(diff_ex(track.region, solid_above)) > eps_area_mm2)
        return; // open above: the pocket vents, nothing is trapped against the film

    const double opening_area_mm2 = area_mm2(track.opening);

    if (opening_area_mm2 >= opts.min_cup_opening_mm2) {
        out.cups.push_back(CupHit{
            .first_layer      = track.first_layer,
            .last_layer       = track.last_layer,
            .opening_centroid = centroid_mm(track.opening),
            .opening_area_mm2 = opening_area_mm2,
            .volume_mm3       = track.volume_mm3,
        });
        return;
    }

    if (opening_area_mm2 <= eps_area_mm2 && track.volume_mm3 >= opts.min_trapped_volume_mm3) {
        out.trapped_resin.push_back(TrappedResinHit{
            .first_layer = track.first_layer,
            .last_layer  = track.last_layer,
            .centroid    = centroid_mm(track.region),
            .volume_mm3  = track.volume_mm3,
        });
    }
    // An opening narrower than the threshold is neither: too small to be a cup, and it connects the
    // pocket to the vat, so the resin can leave (this is the drain hole of a hollow print).
}

} // namespace

CavityAnalysis detect_cavities(const std::vector<ExPolygons>& layers,
                              const std::vector<float>& layer_thicknesses_mm,
                              const CavityDetectionOptions& opts)
{
    CavityAnalysis out;
    if (layers.empty())
        return out;

    const auto thickness_of = [&layer_thicknesses_mm](size_t idx)
    {
        return idx < layer_thicknesses_mm.size() ? double(layer_thicknesses_mm[idx]) : 0.;
    };

    // The regions of the previous layer that are still holes here.
    std::vector<RegionTrack> tracks;

    for (size_t layer_idx = 0; layer_idx < layers.size(); ++layer_idx) {
        const ExPolygons holes = holes_of(layers[layer_idx]);
        if (tracks.empty() && holes.empty())
            continue;

        DisjointSet sets(tracks.size() + holes.size());
        std::vector<Bounds> hole_bounds;
        hole_bounds.reserve(holes.size());
        for (const ExPolygon& hole : holes)
            hole_bounds.push_back(bounds_of(hole));
        for (size_t t = 0; t < tracks.size(); ++t)
            for (size_t h = 0; h < holes.size(); ++h)
                if (may_collide(tracks[t].bounds, hole_bounds[h])
                    && overlaps(tracks[t].region, ExPolygons{holes[h]}))
                    sets.unite(t, tracks.size() + h);

        std::map<size_t, Group> groups;
        for (size_t t = 0; t < tracks.size(); ++t)
            groups[sets.find(t)].tracks.push_back(t);
        for (size_t h = 0; h < holes.size(); ++h)
            groups[sets.find(tracks.size() + h)].holes.push_back(h);

        std::vector<RegionTrack> next;
        next.reserve(groups.size());
        for (const auto& entry : groups) {
            const Group& group = entry.second;
            if (group.holes.empty()) {
                // The region is no longer a hole of this layer: either the solid closed over it
                // (a cup or a cavity) or it opened into the outside (a vent).
                finish_track(tracks[group.tracks.front()], layers[layer_idx], opts, out);
                continue;
            }

            ExPolygons region = {holes[group.holes.front()]};
            for (size_t i = 1; i < group.holes.size(); ++i)
                region = union_ex(region, holes[group.holes[i]]);

            RegionTrack track = group.tracks.empty() ?
                start_track(region, layer_idx, layers) :
                merge_tracks(tracks, group.tracks);
            track.region      = std::move(region);
            track.bounds      = bounds_of(track.region);
            track.last_layer  = layer_idx;
            track.volume_mm3 += area_mm2(track.region) * thickness_of(layer_idx);
            next.push_back(std::move(track));
        }
        tracks = std::move(next);
    }

    // The regions still holes on the last layer are open above, so nothing is reported for them.

    const auto by_first_layer = [](const auto& lhs, const auto& rhs)
    { return lhs.first_layer < rhs.first_layer; };
    std::sort(out.cups.begin(), out.cups.end(), by_first_layer);
    std::sort(out.trapped_resin.begin(), out.trapped_resin.end(), by_first_layer);

    return out;
}

std::vector<float> cup_suction_area_mm2(const std::vector<CupHit>& cups, size_t layer_count)
{
    std::vector<float> areas(layer_count, 0.f);
    for (const CupHit& cup : cups) {
        // The opening is counted on every layer the cup is a hole of, which is from the layer
        // that holds the opening up to the one below the roof.
        for (size_t layer = cup.first_layer; layer <= cup.last_layer && layer < layer_count; ++layer)
            areas[layer] += static_cast<float>(cup.opening_area_mm2);
    }
    return areas;
}

} // namespace Slic3r::SLA
