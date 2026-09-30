#ifndef SLA_HOLLOWING_LATTICE_HPP
#define SLA_HOLLOWING_LATTICE_HPP

#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "libslic3r/SLA/JobController.hpp"

#include <vector>

namespace Slic3r {
namespace sla {

// The structure left standing inside the cavity of a hollow print. A hollow print with a thin wall
// can deform, and a lattice of struts inside the cavity stiffens it while the resin the hollowing
// saves stays saved. None is the plain cavity of today, so it is the default and a print that does
// not ask for a lattice is the hollow print it has always been.
struct HollowingInfillConfig
{
    Domain::sla::HollowingInfillType type = Domain::sla::HollowingInfillType::None;
    double spacing_mm                     = 3.; // axis to axis distance of two neighbouring struts
    double strut_mm                       = 0.4; // the side of a square strut
};

// The axes of the struts of one direction inside the interval [from, to]: as many as fit at the
// wanted spacing, and as symmetric about the middle of the interval as that many allow. This is
// where the struts of a pattern stand, which is what places a drain hole on one of them.
std::vector<double> hollowing_lattice_axes(double from, double to, double spacing_mm);

// The struts of the lattice as they stand in the box, one mesh per strut direction. The struts of
// one direction never touch each other, so every mesh is a set of disjoint closed solids, which is
// what a mesh boolean can take as it is. A Grid is one direction (the columns of a square grid,
// along Z), a Cubic is three (the same grid plus columns along X and along Y). None, or a box or
// a spacing that leaves no room for a strut, gives no meshes at all.
std::vector<indexed_triangle_set> make_hollowing_lattice(
    const Domain::BoundingBox3d &bb,
    const HollowingInfillConfig &cfg,
    const JobController &ctl = {}
);

// Cut the struts out of the cavity: what is left of it is what the hollowing step subtracts from
// the model, so the struts are printed as solid material inside the print and the struts reach the
// wall of the model wherever the cavity touches it. The drain holes are cut after the hollowing
// step, so a strut that stands across a hole is opened by the hole and cannot plug it.
// Returns false, and leaves the cavity as it was, when there is no lattice to cut or when a mesh
// boolean of it failed.
bool subtract_lattice_from_cavity(
    indexed_triangle_set &cavity,
    const HollowingInfillConfig &cfg,
    const JobController &ctl = {}
);

} // namespace sla
} // namespace Slic3r

#endif // SLA_HOLLOWING_LATTICE_HPP
