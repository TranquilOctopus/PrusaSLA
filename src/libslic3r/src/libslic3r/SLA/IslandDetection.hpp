#pragma once

#include <string>
#include <vector>

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::SLA {

/// Below this an area is a speck of the slicing and not a finding [in mm2]. The smallest area
/// detect_islands() reports, and the line the support point generator uses to tell a floating
/// speck that needs a support point from one too small to bother with (M4.4a).
inline constexpr double min_island_area_mm2 = 0.05;

/// A region of a layer that has nothing solid below it and can therefore fall off the build.
struct IslandHit
{
    size_t layer_index = 0;
    Domain::Vec2d centroid = Domain::Vec2d::Zero();
    double area_mm2 = 0.0;
    /// The island itself, in scaled plate coordinates, as it was found in the layer.
    Domain::ExPolygon region;
    /// The model object the island was attributed to, invalid while nothing was attributed.
    Domain::ObjectID object_id{};
    /// The name of that model object, empty while nothing was attributed.
    std::string object_name;
};

/// What one model object occupies on one layer, in the same scaled plate coordinates as the
/// layers handed to detect_islands(). Model and support polygons belong together: an island may
/// as well sit on the supports of an object as on its body.
struct ObjectLayer
{
    Domain::ObjectID object_id{};
    std::string name;
    Domain::ExPolygons slices;
};

/// The model object an island was attributed to. Both fields stay empty for an island that no
/// object of its layer holds any of, e.g. one that only sits on the pad.
struct IslandOwner
{
    Domain::ObjectID object_id{};
    std::string name;
};

/**
 * @brief Find every region of a layer that has no overlap with the layer below.
 *
 * The layers are the merged print layers, so an island is found wherever the whole print has a
 * floating piece, no matter which model it came from. attribute_island() then names that model.
 */
std::vector<IslandHit> detect_islands(const std::vector<Domain::ExPolygons>& layers, double min_area_mm2);

/**
 * @brief The object of the layer that holds most of the island's area.
 *
 * Two models that touch (or overlap) share a layer polygon, so the layer alone cannot say which
 * one an island is on: the object that covers the most of the island wins, which stays right when
 * only a part of the island hangs over its neighbour. An empty owner comes back when no object of
 * the layer covers the island at all.
 */
IslandOwner
attribute_island(const IslandHit& island, const std::vector<ObjectLayer>& object_layers);

} // namespace Slic3r::SLA