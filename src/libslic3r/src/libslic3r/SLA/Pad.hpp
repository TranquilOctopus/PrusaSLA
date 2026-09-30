#ifndef SLA_PAD_HPP
#define SLA_PAD_HPP

#include <vector>
#include <functional>
#include <cmath>
#include <string>

#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"

struct indexed_triangle_set;

namespace Slic3r::sla {

using ThrowOnCancel = std::function<void(void)>;

/// Calculate the polygon representing the silhouette.
void pad_blueprint(
    const indexed_triangle_set &mesh,       // input mesh
    Domain::ExPolygons&        output,     // Output will be merged with
    const std::vector<float> &,     // Exact Z levels to sample
    ThrowOnCancel thrfn = [] {}); // Function that throws if cancel was requested

void pad_blueprint(
    const indexed_triangle_set &mesh,
    Domain::ExPolygons&         output,
    float         samplingheight = 0.1f,  // The height range to sample
    float         layerheight    = 0.05f, // The sampling height
    ThrowOnCancel thrfn          = [] {});

struct PadConfig {
    double wall_thickness_mm = 1.;
    // How thick the slab on the build plate the raft stands on is, which need not be the
    // thickness of the walls above it. Zero keeps the floor as thick as the walls, which is the
    // raft the generator has always built.
    double floor_thickness_mm = 0.;
    double wall_height_mm = 1.;
    double max_merge_dist_mm = 50;
    double wall_slope = std::atan(1.0);          // Universal constant for Pi/4
    double brim_size_mm = 1.6;
    // How far the top edge of the outer wall is bevelled in, so the pad has a thin lip that can
    // be pried off the build plate. Zero keeps the sharp edge. The bevel is as deep as it is wide.
    double edge_taper_mm = 0.;

    /// What the inside of the raft is filled with between the top skin and the build plate. The
    /// pattern is cut out of the slab, but the rim around it is as thick as the material between
    /// two cells and a solid skin is left under the top face, so the object never rests on a hole
    /// and the raft stays printable. None is the solid slab the generator has always built.
    struct Infill {
        Domain::sla::RaftInfillType type = Domain::sla::RaftInfillType::None;
        // Clear size of one open cell, the gap between two neighbouring ribs, in mm.
        double spacing_mm = 2.;
        // Thickness of the material between two cells in mm, which is also the width of the
        // solid rim that keeps the pattern inside the raft wall.
        double wall_mm = 0.4;
        // How much solid material is left under the top face of the raft in mm.
        double skin_mm = 0.5;

        operator bool() const { return type != Domain::sla::RaftInfillType::None; }
    } infill;

    struct EmbedObject {
        double object_gap_mm = 1.;
        double stick_stride_mm = 10.;
        double stick_width_mm = 0.5;
        double stick_penetration_mm = 0.1;
        bool enabled = false;
        bool everywhere = false;
        operator bool() const { return enabled; }
    } embed_object;

    inline PadConfig() = default;
    inline PadConfig(double thickness,
                     double height,
                     double mergedist,
                     double slope)
        : wall_thickness_mm(thickness)
        , wall_height_mm(height)
        , max_merge_dist_mm(mergedist)
        , wall_slope(slope)
    {}

    inline double bottom_offset() const
    {
        return (wall_thickness_mm + wall_height_mm) / std::tan(wall_slope);
    }

    inline double wing_distance() const
    {
        return wall_height_mm / std::tan(wall_slope);
    }

    /// The thickness of the slab the raft stands on. Zero is the wall thickness, so a raft with
    /// no floor thickness of its own is the one the generator has always built.
    inline double floor_thickness() const
    {
        return floor_thickness_mm > 0. ? floor_thickness_mm : wall_thickness_mm;
    }

    inline double full_height() const
    {
        return wall_height_mm + floor_thickness();
    }

    /// Returns the elevation needed for compensating the pad.
    inline double required_elevation() const { return wall_thickness_mm; }

    std::string validate() const;
};

void create_pad(
    const Domain::ExPolygons& support_contours,
    const Domain::ExPolygons& model_contours,
    indexed_triangle_set &output_mesh,
    const PadConfig &             = PadConfig(),
    ThrowOnCancel throw_on_cancel = [] {});

} // namespace Slic3r::sla

#endif // SLABASEPOOL_HPP
