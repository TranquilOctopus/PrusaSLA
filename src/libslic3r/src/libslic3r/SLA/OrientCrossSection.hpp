#pragma once

// libslic3r's Point.hpp declares Slic3r::Transform3f, the rotation the search hands in.
#include <libslic3r/Point.hpp>

#include "Slic3r/Domain/TriangleMesh.hpp"

#include <cstddef>

namespace Slic3r::sla {

// The two terms the cross-section based auto orientation goals of PLAN B7 are scored on, both
// estimated from the mesh instead of from a print (no config, no slice).
//
// The weights are dimensionless and the score they add up to is in mm², so the score can be read as
// "the peak cross section this pose is worth, plus what its cups cost".
struct CrossSectionWeights
{
    // The biggest cross section over the height, the peak peel force term of SLA/LayerStats.hpp.
    // 1 for the least peel goal, which is nothing else.
    double peak_area = 1.;

    // The openings of the suction cups (PLAN B5b, the suction term of the peel force). 1 for the no
    // cups goal, which is the point of that one.
    double cup_opening = 0.;
};

struct CrossSectionScore
{
    // The biggest of the coarse slices of the rotated mesh, in mm². This is the layer that decides
    // the peel force of the print, the one the whole print has to get through.
    double peak_area_mm2 = 0.;

    // The opening areas of every cup SLA/CavityDetection.hpp finds on the coarse slices, in mm².
    // A cup seals against the vat film and pins a vacuum there on every peel it is open for.
    double cup_opening_mm2 = 0.;

    // The weighted sum of the two above, which is what the search minimises.
    double score = 0.;
};

/// Slicing planes this far apart, in mm. Coarse on purpose: the peak area of a pose does not move
/// much between planes 1 mm apart, and the search only needs the pose, not the print.
constexpr double cross_section_slice_step_mm = 1.;

/// A pose no taller than this many mm still gets a plane every cross_section_slice_step_mm. A
/// taller one is sliced at a coarser step instead, so that a tall pose costs the same as a short
/// one. The peak area and a cup opening of a pose this big do not need a 1 mm grid to show up.
constexpr double cross_section_plane_height_mm = 64.;

/// Only this many poses are tried by the goals that slice the mesh: slicing costs orders of
/// magnitude more than the triangle walks of the other goals. The poses are the ones that lay a
/// convex hull face flat on the plate (as everywhere else in the auto orientation), capped to the
/// biggest ones, which are the ones a person would put the object down on.
constexpr size_t cross_section_pose_limit = 24;

/**
 * @brief Score one candidate orientation by its coarse cross sections.
 *
 * @param mesh The mesh to rotate, in the coordinates auto_orient() searches: the mesh of the object
 *        with the scaling and mirroring of its instance applied and no rotation. Its vertices are in
 *        millimetres, which is the coordinate system a model keeps its mesh in and the one the
 *        slicer cuts at, so the plane heights and their step are millimetres as well. The slicer
 *        scales the mesh up into the coordinates of a layer itself, which is where the areas of the
 *        slices come back in, so they are scaled back down into mm² here.
 * @param rotation The candidate rotation, R = Ry(y) * Rx(x), as angle radians.
 * @param weights How much the peak cross section and the cup openings count.
 * @return The two terms and the weighted sum. An empty mesh, or one too flat to slice, scores zero.
 */
CrossSectionScore score_cross_sections(
    const Domain::TriangleMesh& mesh,
    const Transform3f& rotation,
    const CrossSectionWeights& weights
);

} // namespace Slic3r::sla
