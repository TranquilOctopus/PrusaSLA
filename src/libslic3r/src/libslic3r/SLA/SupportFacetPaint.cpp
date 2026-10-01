#include "libslic3r/SLA/SupportFacetPaint.hpp"

#include "libslic3r/SLASupportTool.hpp"

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/FacetsAnnotation.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/TriangleSelector.hpp"
#include "admesh/stl.h"

#include <algorithm>

namespace Slic3r::sla {

namespace {

using Domain::TriangleSelector::TriangleStateType;

/// Overhang samples and island points are placed on the outline of the surface they belong to, and
/// the outline of a painted facet is also the outline of its own region in the layer, so a sample of
/// a blocked surface lies exactly on the border of the very region that has to catch it. The
/// blocker regions are therefore grown by this much before they are tested, so that a sample on the
/// border counts as blocked and a sample a little off the region is caught as well: a region and the
/// layer of the model it belongs to are two views of the same surface, not one.
constexpr double blocker_region_margin_mm = 0.2;

/// True when the painting can belong to @p mesh, that is when every triangle it names is a triangle
/// of that mesh. A painting that was made for another mesh can reach this point with a project file
/// whose volume kept its painting after its mesh was replaced, so the caller refuses such data
/// instead of reading it against the wrong mesh.
bool painting_matches(const Domain::TriangleSelector::TriangleSplittingData &painting,
                      const indexed_triangle_set                          &mesh)
{
    return std::all_of(painting.triangles_to_split.begin(), painting.triangles_to_split.end(),
                       [&mesh](const Domain::TriangleSelector::TriangleBitStreamMapping &entry) {
                           return entry.triangle_idx >= 0 &&
                                  static_cast<size_t>(entry.triangle_idx) < mesh.indices.size();
                       });
}

/// The facets of every part that are painted as @p state, placed in the frame of the merged mesh the
/// support points are generated in. A part with nothing painted is skipped, so an unpainted model
/// costs one empty triangle set per state.
indexed_triangle_set painted_facets(const SupportToolModelMesh &parts,
                                    const Domain::Transform3d &object_to_world,
                                    TriangleStateType         state)
{
    indexed_triangle_set result;
    for (const SupportToolModelMesh::Part &part : parts.parts) {
        if (part.mesh == nullptr || part.painting.triangles_to_split.empty())
            continue;

        // A painting that was not made for the mesh of this part has no facets here: it is
        // refused instead of read, because the facets it names are the facets of another mesh.
        if (!painting_matches(part.painting, part.mesh->its))
            continue;

        // The strict read is the one of the FFF support painting (PrintObject::
        // project_and_append_custom_facets): the paint tool splits the triangles it brushes over, and
        // only the strict read gives the pieces of a split triangle.
        indexed_triangle_set facets =
            Biz::Algorithms::FacetsAnnotation::get_facets_strict(part.painting, *part.mesh, state);
        if (facets.empty())
            continue;

        its_transform(facets, object_to_world * part.matrix);
        Domain::its_merge(result, facets);
    }
    return result;
}

/// The layers the generator works on as the slabs of the mesh slicer: one entry more than there are
/// layers, the first one below the first layer, so that the slab of the first layer is the one of its
/// layer, and one more above the last layer, so that a facet above the last layer, like the top of
/// the model, still has a slab to be projected into. A facet of a slab shows up in the entry of that
/// slab, which is the layer it belongs to, and the slab of a layer is the entry one past it (see
/// painted_facet_regions).
std::vector<float> layer_slabs(const std::vector<float> &heights)
{
    std::vector<float> slabs;
    if (heights.empty())
        return slabs;

    // The generator works on the middle of the layers, so a layer is half a layer height around it.
    const double half_layer_height =
        0.5 * (heights.size() > 1 ? static_cast<double>(heights[1] - heights[0]) : 0.);

    slabs.reserve(heights.size() + 2);
    slabs.push_back(float(heights.front() - half_layer_height));
    for (float height : heights)
        slabs.push_back(float(height + half_layer_height));
    slabs.push_back(float(heights.back() + 2. * half_layer_height));

    return slabs;
}

/// The regions of the layers a set of painted facets covers, one entry per layer.
///
/// The facets are projected into the slabs of the layers instead of being cut on the layers
/// themselves, the way PrintObject::project_and_append_custom_facets does it for the FFF supports: a
/// facet that is horizontal, and the facet of the top of the model, cross no layer at all, and
/// slicing them on the layers would lose them.
///
/// The slicer projects a facet that faces up onto the upper plane of the slab it is in and a facet
/// that faces down onto the lower plane of it, so the two projections of one slab are the same entry
/// of their list: entry i+1 of both of them is the slab of layer i, the way layer_slabs builds it.
/// A horizontal facet of a model crosses no plane of the grid at all, so the slicer projects it onto
/// the plane it lies on, and that plane is a boundary of a slab: a facet that faces up goes into the
/// entry of the slab below the plane, so the top of a model belongs to the topmost layer the model
/// has, and a facet that faces down into the entry of the slab above it, so the bottom of a model
/// belongs to the first one.
std::vector<Domain::ExPolygons> painted_facet_regions(const indexed_triangle_set &facets,
                                                     const std::vector<float>           &heights,
                                                     const std::function<void(void)> &throw_on_cancel)
{
    const std::vector<float> slabs = layer_slabs(heights);
    if (facets.empty() || slabs.empty())
        return {};

    std::vector<Domain::Polygons> from_top, from_bottom;
    slice_mesh_slabs(facets, slabs, Domain::Transform3d::Identity(), &from_top, &from_bottom,
                     throw_on_cancel);

    std::vector<Domain::ExPolygons> regions(heights.size());
    for (size_t layer_id = 0; layer_id < heights.size(); ++layer_id) {
        Domain::Polygons collected;
        auto collect = [&collected](const std::vector<Domain::Polygons> &from, size_t index) {
            if (index >= from.size())
                return;
            collected.insert(collected.end(), from[index].begin(), from[index].end());
        };
        collect(from_top, layer_id + 1);
        collect(from_bottom, layer_id + 1);
        if (collected.empty())
            continue;

        Domain::ExPolygons merged;
        merged.reserve(collected.size());
        for (Domain::Polygon &polygon : collected) {
            // Clipper reads a contour that is turned the other way round as a hole, and the union
            // below would make a region out of nothing.
            if (polygon.area() < 0)
                std::reverse(polygon.begin(), polygon.end());
            merged.emplace_back(std::move(polygon));
        }

        regions[layer_id] = merged.size() > 1 ? union_ex(merged) : Domain::ExPolygons{std::move(merged.front())};
    }

    return regions;
}

/// The regions of one state in every layer, and whether there is any region at all.
void collect_painted_regions(const indexed_triangle_set   &facets,
                            const std::vector<float>             &heights,
                            const std::function<void(void)>      &throw_on_cancel,
                            bool                                  into_blockers,
                            std::vector<SupportFacetPaint::Layer> &out,
                            bool                                 &has_regions)
{
    has_regions = false;
    std::vector<Domain::ExPolygons> regions = painted_facet_regions(facets, heights, throw_on_cancel);
    for (size_t layer_id = 0; layer_id < regions.size() && layer_id < out.size(); ++layer_id) {
        if (regions[layer_id].empty())
            continue;
        if (into_blockers)
            out[layer_id].blockers = offset_ex(regions[layer_id], float(scale_(blocker_region_margin_mm)));
        else
            out[layer_id].enforcers = std::move(regions[layer_id]);
        has_regions = true;
    }
}

} // namespace

const Domain::ExPolygons &SupportFacetPaint::enforcers(size_t layer_id) const
{
    static const Domain::ExPolygons none;
    if (layer_id >= layers.size())
        return none;
    return layers[layer_id].enforcers;
}

const Domain::ExPolygons &SupportFacetPaint::blockers(size_t layer_id) const
{
    static const Domain::ExPolygons none;
    if (layer_id >= layers.size())
        return none;
    return layers[layer_id].blockers;
}

bool SupportFacetPaint::is_blocked(size_t layer_id, const Domain::Point &point) const
{
    // A point on the border of a blocked region counts as blocked: a head of a support point has a
    // size and a blocked facet has no room for it.
    return Biz::Algorithms::ExPolygon::contains(blockers(layer_id), point);
}

SupportFacetPaint support_facet_paint(const SupportToolModelMesh        &parts,
                                      const Domain::Transform3d        &object_to_world,
                                      const std::vector<float>         &heights,
                                      const std::function<void(void)> &throw_on_cancel)
{
    SupportFacetPaint paint;
    // Every rule that reads the paint is off when nothing is painted, so the empty paint is returned
    // before the layers are even allocated.
    const indexed_triangle_set enforcer_facets =
        painted_facets(parts, object_to_world, TriangleStateType::ENFORCER);
    const indexed_triangle_set blocker_facets =
        painted_facets(parts, object_to_world, TriangleStateType::BLOCKER);
    if (enforcer_facets.empty() && blocker_facets.empty())
        return paint;

    paint.layers.resize(heights.size());

    // slice_mesh_slabs() calls the cancel function while it slices, an empty one would throw.
    std::function<void(void)> cancel = throw_on_cancel;
    if (!cancel)
        cancel = []() {};

    // The enforcers are collected first, so that a facet painted as an enforcer and as a blocker at
    // the same time is blocked: the generator keeps the blockers of a layer over the points of its
    // enforcers (see support_enforced_regions).
    collect_painted_regions(enforcer_facets, heights, cancel, false, paint.layers, paint.has_enforcer_regions);
    collect_painted_regions(blocker_facets, heights, cancel, true, paint.layers, paint.has_blocker_regions);

    return paint;
}

} // namespace Slic3r::sla
