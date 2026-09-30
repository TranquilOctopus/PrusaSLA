#ifndef SLA_SUPPORTFACETPAINT_HPP
#define SLA_SUPPORTFACETPAINT_HPP

#include <functional>
#include <vector>

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::sla {

struct SupportToolModelMesh; // forward decl, see libslic3r/SLASupportTool.hpp

/// @brief The facets the user painted on a model, as the regions of the layers the support point
/// generator works on.
///
/// Painting a facet as an enforcer means "put support points here", painting it as a blocker means
/// "no support point here", the way the paint tool of the FFF supports does it. Both are kept as the
/// regions the painted facets cover in every layer, so that the generator can test a support point
/// in the layer it was made in, exactly like it tests the enforcer and blocker modifier volumes of
/// the slice pipeline.
///
/// A model with nothing painted gives an empty paint, and every rule reading it is then off, so the
/// support points of an unpainted model are the points of the algorithm without painting.
struct SupportFacetPaint
{
    struct Layer
    {
        Domain::ExPolygons enforcers; // regions that must have support points
        Domain::ExPolygons blockers;  // regions that must have support points, in no case
    };

    // One entry per layer of the grid, in the order of the slice heights.
    std::vector<Layer> layers;
    bool              has_enforcer_regions = false;
    bool              has_blocker_regions  = false;

    /// @brief True when no facet was painted as an enforcer or as a blocker.
    bool empty() const { return !has_enforcer_regions && !has_blocker_regions; }

    /// @brief The enforcer regions of the layer, empty for a layer that has none.
    const Domain::ExPolygons &enforcers(size_t layer_id) const;

    /// @brief The blocker regions of the layer, empty for a layer that has none.
    const Domain::ExPolygons &blockers(size_t layer_id) const;

    /// @brief True when the point, in the scaled coordinates of the layer, is in a blocker region.
    bool is_blocked(size_t layer_id, const Domain::Point &point) const;
};

/// @brief The painted facets of the model parts, as the regions of the layers the support points are
/// generated on.
///
/// @param parts the model parts with the painting of each, see SupportToolModelMesh
/// @param object_to_world places the model's mesh, the painted facets follow their own volume matrix
/// @param heights the grid of slice heights, the one the support points are generated on
/// @param throw_on_cancel called while the painted facets are sliced
SupportFacetPaint support_facet_paint(const SupportToolModelMesh        &parts,
                                      const Domain::Transform3d        &object_to_world,
                                      const std::vector<float>         &heights,
                                      const std::function<void(void)> &throw_on_cancel);

} // namespace Slic3r::sla

#endif // SLA_SUPPORTFACETPAINT_HPP
