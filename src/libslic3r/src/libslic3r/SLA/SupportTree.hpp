#ifndef SLA_SUPPORTTREE_HPP
#define SLA_SUPPORTTREE_HPP

#include <math.h>
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"
#include "libslic3r/SLA/Pad.hpp"

#include "admesh/stl.h"

namespace Slic3r {

namespace sla {
struct JobController;

struct SupportTreeConfig
{
    bool   enabled = true;

    // Type of the support tree, for
    Domain::sla::SupportTreeType tree_type = Domain::sla::SupportTreeType::Default;

    // Radius in mm of the pointing side of the head.
    double head_front_radius_mm = 0.2;

    // How much the pinhead has to penetrate the model surface
    double head_penetration_mm = 0.5;

    // Radius of the back side of the 3d arrow.
    double head_back_radius_mm = 0.5;

    double head_fallback_radius_mm = 0.25;

    // Width in mm from the back sphere center to the front sphere center.
    double head_width_mm = 1.0;

    // How to connect pillars
    Domain::sla::PillarConnectionMode pillar_connection_mode = Domain::sla::PillarConnectionMode::dynamic;

    // Whether neighbouring pillars may be linked to each other at all. With bracing off
    // every pillar has to reach the ground on its own.
    bool brace_enable = true;

    // Diameter of a brace between two pillars in mm. Zero means a brace is as thick as the
    // pillar it hangs on, which is what the tree has always built.
    double brace_diameter_mm = 0.;

    // The lowest height above the plate a brace between two pillars may reach. Zero means
    // as low as the pillar bases allow.
    double brace_start_height_mm = 0.;

    // Only generate pillars that can be routed to ground
    bool ground_facing_only = false;

    // TODO: unimplemented at the moment. This coefficient will have an impact
    // when bridges and pillars are merged. The resulting pillar should be a bit
    // thicker than the ones merging into it. How much thicker? I don't know
    // but it will be derived from this value.
    double pillar_widening_factor = .5;

    // Radius in mm of the pillar base.
    double base_radius_mm = 2.0;

    // The height of the pillar base cone in mm.
    double base_height_mm = 1.0;

    // The shape of the pillar base: a cone that flares gradually, a straight cylinder of the
    // base diameter or a thin flat disc under a straight pillar.
    Domain::sla::SupportBaseShape base_shape = Domain::sla::SupportBaseShape::Cone;

    // The default angle for connecting support sticks and junctions.
    double bridge_slope = M_PI/4;

    // The max length of a bridge in mm
    double max_bridge_length_mm = 10.0;

    // The max distance of a pillar to pillar link.
    double max_pillar_link_distance_mm = 10.0;

    // The elevation in Z direction upwards. This is the space between the pad
    // and the model object's bounding box bottom.
    double object_elevation_mm = 10;
    
    // The shortest distance between a pillar base perimeter from the model
    // body. This is only useful when elevation is set to zero.
    double pillar_base_safety_distance_mm = 0.5;
    
    unsigned max_bridges_on_pillar = 3;

    double max_weight_on_model_support = 10.f;

    double head_fullwidth() const {
        return 2 * head_front_radius_mm + head_width_mm +
               2 * head_back_radius_mm - head_penetration_mm;
    }

    double safety_distance() const { return safety_distance_mm; }
    double safety_distance(double r) const
    {
        return std::min(safety_distance_mm, r * safety_distance_mm / head_back_radius_mm);
    }

    // The upper bound for a per point pillar diameter (the support presets):
    // a preset may not ask for a pillar fatter than this multiple of the
    // configured one. See sla::head_back_radius().
    double head_back_radius_limit_mm() const
    {
        return SupportTreeConfig::max_pillar_radius_factor *
               std::max(head_back_radius_mm, head_fallback_radius_mm);
    }

    // /////////////////////////////////////////////////////////////////////////
    // Compile time configuration values (candidates for runtime)
    // /////////////////////////////////////////////////////////////////////////

    // The max Z angle for a normal at which it will get completely ignored.
    static const double constexpr normal_cutoff_angle = 150.0 * M_PI / 180.0;

    // The safety gap between a support structure and model body. For support
    // struts smaller than head_back_radius, the safety distance is scaled
    // down accordingly. see method safety_distance()
    static const double constexpr safety_distance_mm = 0.5;

    // How much fatter than the configured pillar radius a support point's own
    // pillar diameter may ask to be. See head_back_radius_limit_mm().
    static const double constexpr max_pillar_radius_factor = 4.;

    static const double constexpr max_solo_pillar_height_mm = 15.0;
    static const double constexpr max_dual_pillar_height_mm = 35.0;
    static const double constexpr optimizer_rel_score_diff = 1e-10;
    static const unsigned constexpr optimizer_max_iterations = 2000;
    static const unsigned constexpr pillar_cascade_neighbors = 3;
    
};

enum class MeshType { Support, Pad };

struct SupportableMesh
{
    AABBMesh          emesh;
    std::shared_ptr<const Domain::SLA::SupportPoints> pts;
    SupportTreeConfig cfg;
    PadConfig         pad_cfg;
    double            zoffset = 0.;
};

inline double ground_level(const SupportableMesh &sm)
{
    double lvl = sm.zoffset -
                 !bool(sm.pad_cfg.embed_object) * sm.cfg.enabled * sm.cfg.object_elevation_mm +
                  bool(sm.pad_cfg.embed_object) * sm.pad_cfg.wall_thickness_mm;

    return lvl;
}

// Radius of a brace between two pillars. A zero brace diameter means the brace is as thick
// as the pillar it belongs to.
inline double brace_radius(const SupportTreeConfig &cfg, double pillar_radius)
{
    return cfg.brace_diameter_mm > 0. ? 0.5 * cfg.brace_diameter_mm : pillar_radius;
}

// The lowest z a brace between two pillars may reach: the taller of the pillar base and the
// configured brace start height, both measured from the plate.
inline double brace_start_z(const SupportableMesh &sm)
{
    return ground_level(sm) + std::max(sm.cfg.base_height_mm, sm.cfg.brace_start_height_mm);
}

indexed_triangle_set create_support_tree(const SupportableMesh &mesh,
                                         const JobController   &ctl);

indexed_triangle_set create_pad(const SupportableMesh      &model_mesh,
                                const indexed_triangle_set &support_mesh,
                                const JobController        &ctl);

std::vector<Domain::ExPolygons> slice(const indexed_triangle_set &support_mesh,
                                      const indexed_triangle_set &pad_mesh,
                                      const std::vector<float>   &grid,
                                      float                       closing_radius,
                                      const JobController        &ctl);

} // namespace sla
} // namespace Slic3r

#endif // SLASUPPORTTREE_HPP
