#ifndef BRANCHINGTREESLA_HPP
#define BRANCHINGTREESLA_HPP

#include <algorithm>

#include "libslic3r/BranchingTree/BranchingTree.hpp"
#include "SupportTreeBuilder.hpp"

namespace Slic3r { namespace sla {
class SupportTreeBuilder;
struct SupportableMesh;

// How much of the configured widening factor one mm of weight is worth. Scales
// the input value 'widening_factor:<0, 1>' to produce resonable widening
// behaviour.
inline constexpr double branching_widening_scale = 0.05;

// The radius the branching tree builds a branch of a node with. The weight of a
// node is the length of the longest chain of branches merged into it, so a node
// deep in the tree is built as a trunk of the whole subtree it holds up, and the
// pillars under a tall model are the fattest thing in it. 'pillar_radius_cap' is
// the radius such a branch may not get fatter than, as a multiple of the radius
// of the head it carries (M4.5a): the cap can only cut the widening, since the
// radius it is measured from is the radius of that head, which is the thinnest a
// branch ever is. Zero means no cap, which is the tree as it was built before
// the key existed.
inline double branch_radius(const SupportTreeConfig &cfg,
                            double                    r_min,
                            double                    weight)
{
    double r = r_min + branching_widening_scale * cfg.pillar_widening_factor * weight;

    // A cap of one is the tightest there is: the widening off and the branch at
    // the radius of the head it carries. A cap below that would be thinner than
    // the head, which is no branch at all, so it is read as one.
    const double cap = cfg.pillar_radius_cap;

    if (cap > 0. && r_min > 0.)
        r = std::min(r, std::max(1., cap) * r_min);

    return r;
}

void create_branching_tree(SupportTreeBuilder& builder, const SupportableMesh &sm);

}} // namespace Slic3r::sla

#endif // BRANCHINGTREESLA_HPP
