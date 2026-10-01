
#pragma once

#include <type_traits>
#include <utility>
#include <cstddef>
#include <iterator>
#include <algorithm>

namespace Slic3r::Biz::Algorithms::Execution {

namespace detail {

// How many SequentialRegion objects the calling thread is inside of. A thread
// local one, because what is being switched off is the parallelism of the
// thread that asks for it, not of the process.
inline thread_local unsigned int sequential_nesting = 0;

} // namespace detail

// A thread which may not leave its own task raises this guard for as long as it
// may not: every execution policy then runs its loops on the calling thread, in
// the order of the range, whatever the policy is. A parallel loop is not only
// slower that way, it also makes the calling thread wait, and a thread that waits
// for a parallel loop may be handed any task of its arena - including another
// task of a loop it is already inside. That is fatal for a thread which holds a
// lock across the loop: the task it is handed may ask for that same lock, and
// std::mutex refuses a second lock by the thread that owns it with "resource
// deadlock would occur". The NLopt searches of the support tree hold the lock of
// the one process wide NLopt generator for the whole search, and the objective
// of a search runs the model queries, which are loops (see
// Slic3r/Biz/Algorithms/Optimize/NLoptOptimizer.hpp).
//
// The guard counts, so a nested region is left by the inner one only, and it
// belongs to the thread which raised it: another thread's loops are not touched.
class SequentialRegion
{
public:
    SequentialRegion() noexcept { ++detail::sequential_nesting; }
    ~SequentialRegion() { --detail::sequential_nesting; }

    SequentialRegion(const SequentialRegion &) = delete;
    SequentialRegion &operator=(const SequentialRegion &) = delete;
};

// Whether the calling thread is inside a SequentialRegion, i.e. whether the
// loops of an execution policy are to run on the calling thread.
inline bool in_sequential_region() noexcept
{
    return detail::sequential_nesting > 0;
}

// Override for valid execution policies
template<class EP>
struct IsExecutionPolicy_ : public std::false_type
{};

template<class EP>
constexpr bool IsExecutionPolicy = IsExecutionPolicy_<std::remove_cvref_t<EP>>::value;

template<class EP, class T = void>
using ExecutionPolicyOnly = std::enable_if_t<IsExecutionPolicy<EP>, T>;

template<class T, class O = T>
using IteratorOnly = std::enable_if_t<
    !std::is_same_v<typename std::iterator_traits<T>::value_type, void>, O
>;

template<class T, class O = T>
using IntegerOnly = std::enable_if_t<std::is_integral<T>::value, O>;

// This struct needs to be specialized for each execution policy.
// See ExecutionSeq.hpp and ExecutionTBB.hpp for example.
template<class EP, class En = void>
struct Traits
{};

template<class EP>
using AsTraits = Traits<std::remove_cvref_t<EP>>;

// Each execution policy should declare two types of mutexes. A a spin lock and
// a blocking mutex. These types should satisfy the BasicLockable concept.
template<class EP>
using SpinningMutex = typename AsTraits<EP>::SpinningMutex;
template<class EP>
using BlockingMutex = typename AsTraits<EP>::BlockingMutex;

// Query the available threads for concurrency.
template<class EP, class = ExecutionPolicyOnly<EP>>
size_t max_concurrency(const EP& ep)
{
    return AsTraits<EP>::max_concurrency(ep);
}

// foreach loop with the execution policy passed as argument. Granularity can
// be specified explicitly. max_concurrency() can be used for optimal results.
template<class EP, class It, class Fn, class = ExecutionPolicyOnly<EP>>
void for_each(const EP& ep, It from, It to, Fn&& fn, size_t granularity = 1)
{
    AsTraits<EP>::for_each(ep, from, to, std::forward<Fn>(fn), std::max(granularity, size_t(1)));
}

// A reduce operation with the execution policy passed as argument.
// mergefn has T(const T&, const T&) signature
// accessfn has T(I) signature if I is an integral type and
// T(const I::value_type &) if I is an iterator type.
template<class EP, class I, class MergeFn, class T, class AccessFn, class = ExecutionPolicyOnly<EP>>
T reduce(
    const EP& ep,
    I from,
    I to,
    const T& init,
    MergeFn&& mergefn,
    AccessFn&& accessfn,
    size_t granularity = 1
)
{
    return AsTraits<EP>::reduce(
        ep, from, to, init, std::forward<MergeFn>(mergefn), std::forward<AccessFn>(accessfn),
        std::max(granularity, size_t(1))
    );
}

// An overload of reduce method to be used with iterators as 'from' and 'to'
// arguments. Access functor is omitted here.
template<class EP, class I, class MergeFn, class T, class = ExecutionPolicyOnly<EP>>
T reduce(const EP& ep, I from, I to, const T& init, MergeFn&& mergefn, size_t granularity = 1)
{
    return reduce(
        ep, from, to, init, std::forward<MergeFn>(mergefn), [](const auto& i) { return i; },
        std::max(granularity, size_t(1))
    );
}
} // namespace Slic3r::Biz::Algorithms::Execution
