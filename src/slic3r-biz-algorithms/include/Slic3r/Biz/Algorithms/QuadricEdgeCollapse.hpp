#ifndef slic3r_quadric_edge_collapse_hpp_
#define slic3r_quadric_edge_collapse_hpp_

// paper: https://people.eecs.berkeley.edu/~jrs/meshpapers/GarlandHeckbert2.pdf
// sum up: https://users.csc.calpoly.edu/~zwood/teaching/csc570/final06/jseeba/
// inspiration: https://github.com/sp4cerat/Fast-Quadric-Mesh-Simplification

#include <cstdint> // uint32_t
#include <functional>

struct indexed_triangle_set;

namespace Slic3r::Biz::Algorithms {

// The defaults below are named functions and not lambdas on purpose. MSVC mangles
// the lambdas of a default function argument with a counter that is per namespace, so
// the same mangled name can stand for different lambdas in different translation units
// and the linker keeps one body: the void `[]() {}` of statusfn replaced the body of
// `return false` and is_stoped returned whatever was left in the return register.
namespace detail {
inline bool never_stop() { return false; }
inline void no_status(int) {}
} // namespace detail

/// <summary>
/// Simplify mesh by Quadric metric
/// </summary>
/// <param name="its">IN/OUT triangle mesh to be simplified.</param>
/// <param name="triangle_count">Wanted triangle count.</param>
/// <param name="max_error">Maximal Quadric for reduce.
/// When nullptr then max float is used
/// Output: Last used ErrorValue to collapse edge</param>
/// <param name="is_stoped">Could stop process of calculation from outside.</param>
/// <param name="statusfn">Give a feed back to user about progress. Values 1 - 100</param>
void its_quadric_edge_collapse(
    indexed_triangle_set&     its,
    uint32_t                  triangle_count  = 0,
    float *                   max_error       = nullptr,
    std::function<bool(void)> is_stoped = &detail::never_stop,
    std::function<void(int)>  statusfn = &detail::no_status);

} // namespace Slic3r::Biz::Algorithms
#endif // slic3r_quadric_edge_collapse_hpp_
