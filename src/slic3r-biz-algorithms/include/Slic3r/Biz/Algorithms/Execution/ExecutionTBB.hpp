#ifndef EXECUTIONTBB_HPP
#define EXECUTIONTBB_HPP

#include <mutex>

#include <tbb/spin_mutex.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include <tbb/task_arena.h>

#include "Slic3r/Biz/Algorithms/Execution/Execution.hpp"

namespace Slic3r::Biz::Algorithms::Execution {

struct ExecutionTBB {};
template<> struct IsExecutionPolicy_<ExecutionTBB> : public std::true_type {};

// Execution policy using Intel TBB library under the hood.
static constexpr ExecutionTBB ex_tbb = {};

template<> struct Traits<ExecutionTBB> {
private:

    template<class Fn, class It>
    static IteratorOnly<It, void> loop_(const tbb::blocked_range<It> &range, Fn &&fn)
    {
        for (auto &el : range) fn(el);
    }

    template<class Fn, class I>
    static IntegerOnly<I, void> loop_(const tbb::blocked_range<I> &range, Fn &&fn)
    {
        for (I i = range.begin(); i < range.end(); ++i) fn(i);
    }

public:
    using SpinningMutex = tbb::spin_mutex;
    using BlockingMutex = std::mutex;

    template<class It, class Fn>
    static void for_each(const ExecutionTBB &,
                         It from, It to, Fn &&fn, size_t granularity)
    {
        // Inside a SequentialRegion the loop belongs to the calling thread: same
        // range, same order, no waiting, so the thread is never handed a task of
        // some other loop while it holds whatever it holds. See SequentialRegion.
        if (in_sequential_region()) {
            loop_(tbb::blocked_range{from, to, granularity}, std::forward<Fn>(fn));
            return;
        }

        tbb::parallel_for(tbb::blocked_range{from, to, granularity},
                          [&fn](const auto &range) {
            loop_(range, std::forward<Fn>(fn));
        });
    }

    template<class I, class MergeFn, class T, class AccessFn>
    static T reduce(const ExecutionTBB &,
                    I          from,
                    I          to,
                    const T   &init,
                    MergeFn  &&mergefn,
                    AccessFn &&accessfn,
                    size_t     granularity = 1
                    )
    {
        // The same one thread reduction as above, and the one way round: the
        // identity is the left operand of the final merge, as it is in TBB.
        if (in_sequential_region()) {
            T acc = init;
            loop_(tbb::blocked_range{from, to, granularity},
                  [&](auto &i) { acc = mergefn(acc, accessfn(i)); });
            return mergefn(init, acc);
        }

        return tbb::parallel_reduce(
            tbb::blocked_range{from, to, granularity}, init,
            [&](const auto &range, T subinit) {
                T acc = subinit;
                loop_(range, [&](auto &i) { acc = mergefn(acc, accessfn(i)); });
                return acc;
            },
            std::forward<MergeFn>(mergefn));
    }

    static size_t max_concurrency(const ExecutionTBB &)
    {
        return tbb::this_task_arena::max_concurrency();
    }
};

}

#endif // EXECUTIONTBB_HPP
