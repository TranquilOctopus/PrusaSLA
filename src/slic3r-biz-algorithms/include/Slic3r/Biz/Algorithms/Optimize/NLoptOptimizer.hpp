#ifndef NLOPTOPTIMIZER_HPP
#define NLOPTOPTIMIZER_HPP

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#endif
#include <nlopt.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <tbb/task_arena.h>

#include "Slic3r/Biz/Algorithms/Optimize/Optimizer.hpp"

namespace Slic3r::Biz::Algorithms::Optimize {

namespace detail {

// NLopt's stochastic algorithms (ESCH, the genetic search of the support tree,
// ISRES, CRS, MLSL without LDS, ...) draw from NLopt's Mersenne twister, which
// nlopt_srand() seeds. NLopt keeps that generator in thread local storage when
// it is built with its THREADLOCAL option (on by default, and on in the
// dependency build of this repository), so every thread has a stream of its
// own: a search seeded on the thread it runs on draws exactly the numbers of its
// own seed, whatever runs on the other threads. The deterministic algorithms
// (Subplex, Nelder-Mead, COBYLA, MLSL with LDS) draw nothing.
//
// One thing can still feed a search the numbers of another one on the same
// thread: a thread that waits for a parallel loop inside the objective may be
// handed a task of an outer loop, and that task may start a search of its own,
// which reseeds this thread's stream and draws from it before the first search
// continues. optimize() therefore runs the search in an isolated TBB region (see
// there). Nothing else is needed for a repeatable search, so the searches of the
// support tree run in parallel, and so do the ray casts inside each of them.
//
// Whether the generator really is per thread is asked of the NLopt library at
// hand once per process: one genetic search on a thread of its own, from one
// seed, with and without another thread reseeding NLopt at its first
// evaluation. The searches visit the same points exactly when the other thread
// cannot touch this thread's generator.
inline std::vector<double> nlopt_probe_trace(bool disturb)
{
    struct Probe { std::vector<double> xs; bool disturb; };
    Probe probe{{}, disturb};

    std::thread searcher([&probe] {
        nlopt_opt o = nlopt_create(NLOPT_GN_ESCH, 1);
        if (!o)
            return;

        double lb = 0., ub = 1., x = .5, f = 0.;
        nlopt_set_lower_bounds(o, &lb);
        nlopt_set_upper_bounds(o, &ub);
        nlopt_set_maxeval(o, 200);
        nlopt_set_min_objective(o, [](unsigned, const double *in, double *, void *data) {
            auto &p = *static_cast<Probe *>(data);
            if (p.disturb && p.xs.empty())
                std::thread([] { nlopt_srand(2); }).join();
            p.xs.emplace_back(in[0]);
            return (in[0] - .3) * (in[0] - .3);
        }, &probe);

        nlopt_srand(1);
        nlopt_optimize(o, &x, &f);
        nlopt_destroy(o);
    });
    searcher.join();

    return probe.xs;
}

inline bool nlopt_rng_is_thread_local()
{
    static const bool per_thread = [] {
        std::vector<double> quiet = nlopt_probe_trace(false);
        return !quiet.empty() && quiet == nlopt_probe_trace(true);
    }();

    return per_thread;
}

// Only for an NLopt that shares one generator between all threads: then a
// search has to own it from the seeding to its end, and that is this lock. The
// isolation of optimize() also keeps a thread holding it from being handed a
// task that would ask for it again.
inline std::mutex &nlopt_rng_lock()
{
    static std::mutex mtx;
    return mtx;
}

// Helper types for NLopt algorithm selection in template contexts
template<nlopt_algorithm alg> struct NLoptAlg {};

// NLopt can combine multiple algorithms if one is global an other is a local
// method. This is how template specializations can be informed about this fact.
template<nlopt_algorithm gl_alg, nlopt_algorithm lc_alg = NLOPT_LN_NELDERMEAD>
struct NLoptAlgComb {};

template<class M> struct IsNLoptAlg {
    static const constexpr bool value = false;
};

template<nlopt_algorithm a> struct IsNLoptAlg<NLoptAlg<a>> {
    static const constexpr bool value = true;
};

template<nlopt_algorithm a1, nlopt_algorithm a2>
struct IsNLoptAlg<NLoptAlgComb<a1, a2>> {
    static const constexpr bool value = true;
};

// NLopt can wrap any of its algorithms to use the augmented lagrangian method
// for deriving an object function from all equality and inequality constraints
// This way one can use algorithms that do not support these constraints natively
template<class Alg> struct NLoptAUGLAG {};

template<nlopt_algorithm a1, nlopt_algorithm a2>
struct IsNLoptAlg<NLoptAUGLAG<NLoptAlgComb<a1, a2>>> {
    static const constexpr bool value = true;
};

template<nlopt_algorithm a> struct IsNLoptAlg<NLoptAUGLAG<NLoptAlg<a>>> {
    static const constexpr bool value = true;
};

template<class M, class T = void>
using NLoptOnly = std::enable_if_t<IsNLoptAlg<M>::value, T>;

template<class M> struct GetNLoptAlg_ {
    static constexpr nlopt_algorithm Local = NLOPT_NUM_ALGORITHMS;
    static constexpr nlopt_algorithm Global = NLOPT_NUM_ALGORITHMS;
    static constexpr bool IsAUGLAG = false;
};

template<nlopt_algorithm a> struct GetNLoptAlg_<NLoptAlg<a>> {
    static constexpr nlopt_algorithm Local = NLOPT_NUM_ALGORITHMS;
    static constexpr nlopt_algorithm Global = a;
    static constexpr bool IsAUGLAG = false;
};

template<nlopt_algorithm g, nlopt_algorithm l>
struct GetNLoptAlg_<NLoptAlgComb<g, l>> {
    static constexpr nlopt_algorithm Local = l;
    static constexpr nlopt_algorithm Global = g;
    static constexpr bool IsAUGLAG = false;
};

template<class M> constexpr nlopt_algorithm GetNLoptAlg_Global = GetNLoptAlg_<std::remove_cvref_t<M>>::Global;
template<class M> constexpr nlopt_algorithm GetNLoptAlg_Local = GetNLoptAlg_<std::remove_cvref_t<M>>::Local;
template<class M> constexpr bool IsAUGLAG = GetNLoptAlg_<std::remove_cvref_t<M>>::IsAUGLAG;

template<class M> struct GetNLoptAlg_<NLoptAUGLAG<M>> {
    static constexpr nlopt_algorithm Local = GetNLoptAlg_Local<M>;
    static constexpr nlopt_algorithm Global = GetNLoptAlg_Global<M>;
    static constexpr bool IsAUGLAG = true;
};

enum class OptDir { MIN, MAX }; // Where to optimize

struct NLoptRAII { // Helper RAII class for nlopt_opt
    nlopt_opt ptr = nullptr;

    template<class...A> explicit NLoptRAII(A&&...a)
    {
        ptr = nlopt_create(std::forward<A>(a)...);
    }

    NLoptRAII(const NLoptRAII&) = delete;
    NLoptRAII(NLoptRAII&&) = delete;
    NLoptRAII& operator=(const NLoptRAII&) = delete;
    NLoptRAII& operator=(NLoptRAII&&) = delete;

    ~NLoptRAII() { nlopt_destroy(ptr); }
};

// Wrap each element of the tuple tup into a wrapper class W and return
// a new tuple with each element being of type W<T_i> where T_i is the type of
// i-th element of tup.
template<template<class> class W, class...Args>
auto wrap_tup(const std::tuple<Args...> &tup)
{
    return std::tuple<W<Args>...>(tup);
}

template<class M, class = NLoptOnly<M>>
class NLoptOpt {
    StopCriteria m_stopcr;
    StopCriteria m_loc_stopcr;
    OptDir m_dir = OptDir::MIN;

    // The seed of the search. It is given to NLopt's generator when the search
    // runs rather than when it is asked for: the generator is the one of the
    // thread the search runs on, which need not be the thread that set the seed.
    long m_seed = 0;

    static constexpr double ConstraintEps = 1e-6;

    template<class Fn> struct OptData {
        Fn fn;
        NLoptOpt *self = nullptr;
        nlopt_opt opt_raw = nullptr;

        OptData(const Fn &f): fn{f} {}

        OptData(const Fn &f, NLoptOpt *s, nlopt_opt nlopt_raw)
            : fn{f}, self{s}, opt_raw{nlopt_raw} {}
    };

    template<class Fn, size_t N>
    static double optfunc(unsigned n, const double *params,
                          double *gradient, void *data)
    {
        assert(n == N);

        auto tdata = static_cast<OptData<Fn>*>(data);

        if (tdata->self->m_stopcr.stop_condition())
            nlopt_force_stop(tdata->opt_raw);

        auto funval = to_arr<N>(params);

        double scoreval = 0.;
        using RetT = decltype(tdata->fn(funval));
        if constexpr (std::is_convertible_v<RetT, ScoreGradient<N>>) {
            ScoreGradient<N> score = tdata->fn(funval);
            for (size_t i = 0; i < n; ++i) gradient[i] = (*score.gradient)[i];
            scoreval = score.score;
        } else {
            scoreval = tdata->fn(funval);
        }

        return scoreval;
    }

    template<class Fn, size_t N>
    static double constrain_func(unsigned n, const double *params,
                                 double *gradient, void *data)
    {
        assert(n == N);

        auto tdata = static_cast<OptData<Fn>*>(data);
        auto funval = to_arr<N>(params);

        return tdata->fn(funval);
    }

    template<size_t N>
    static void set_up(NLoptRAII &nl,
                       const Bounds<N> &bounds,
                       const StopCriteria &stopcr)
    {
        std::array<double, N> lb, ub;

        for (size_t i = 0; i < N; ++i) {
            lb[i] = bounds[i].min();
            ub[i] = bounds[i].max();
        }

        nlopt_set_lower_bounds(nl.ptr, lb.data());
        nlopt_set_upper_bounds(nl.ptr, ub.data());

        double abs_diff = stopcr.abs_score_diff();
        double rel_diff = stopcr.rel_score_diff();
        double stopval = stopcr.stop_score();
        if(!std::isnan(abs_diff)) nlopt_set_ftol_abs(nl.ptr, abs_diff);
        if(!std::isnan(rel_diff)) nlopt_set_ftol_rel(nl.ptr, rel_diff);
        if(!std::isnan(stopval))  nlopt_set_stopval(nl.ptr, stopval);

        if(stopcr.max_iterations() > 0)
            nlopt_set_maxeval(nl.ptr, stopcr.max_iterations());
    }

    template<class Fn, size_t N, class...EqFns, class...IneqFns>
    Result<N> optimize(NLoptRAII &nl, Fn &&fn, const Input<N> &initvals,
                       const std::tuple<EqFns...> &equalities,
                       const std::tuple<IneqFns...> &inequalities)
    {
        Result<N> r;

        OptData<Fn> data {fn, this, nl.ptr};

        auto do_for_each_eq = [this, &nl](auto &arg) {
            arg.self = this;
            arg.opt_raw = nl.ptr;
            using F = decltype(arg.fn);
            nlopt_add_equality_constraint (nl.ptr, constrain_func<F, N>, &arg, ConstraintEps);
        };

        auto do_for_each_ineq = [this, &nl](auto &arg) {
            arg.self = this;
            arg.opt_raw = nl.ptr;
            using F = decltype(arg.fn);
            nlopt_add_inequality_constraint (nl.ptr, constrain_func<F, N>, &arg, ConstraintEps);
        };

        auto eq_data = wrap_tup<OptData>(equalities);
        std::apply([&](auto&... args) { (do_for_each_eq(args), ...); }, eq_data);

        auto ineq_data = wrap_tup<OptData>(inequalities);
        std::apply([&](auto&... args) { (do_for_each_ineq(args), ...); }, ineq_data);

        switch(m_dir) {
        case OptDir::MIN:
            nlopt_set_min_objective(nl.ptr, optfunc<Fn, N>, &data); break;
        case OptDir::MAX:
            nlopt_set_max_objective(nl.ptr, optfunc<Fn, N>, &data); break;
        }

        r.optimum = initvals;

        // The same seed for every call, and no other search drawing from the
        // generator in between: the same search on the same input then returns
        // the same result whatever the thread it runs on and whatever runs next
        // to it. The generator is the calling thread's own (see
        // nlopt_rng_is_thread_local), so the seed goes to this thread, and only
        // an NLopt with one generator for the process needs the lock.
        std::unique_lock<std::mutex> lk{nlopt_rng_lock(), std::defer_lock};
        if (!nlopt_rng_is_thread_local())
            lk.lock();

        nlopt_srand(static_cast<unsigned long>(m_seed));

        // The objective of a search of the support tree runs the model queries,
        // and pinhead_mesh_hit and beam_mesh_hit are loops over the samples of a
        // ring under the TBB policy. A thread which waits for such a loop may be
        // handed any task of its arena, including a task of the support tree's
        // own loops (the pinheads of add_pinheads, the model facing heads of
        // routing_to_model), and such a task starts a search of its own on this
        // thread: it would reseed this thread's generator and draw from it in the
        // middle of this search (and, with the lock, ask for the lock a second
        // time, which std::mutex refuses with "resource deadlock would occur").
        // Inside an isolated region the waiting thread only runs tasks spawned in
        // that region, i.e. the ray casts of this search, which draw nothing,
        // while the other threads are free to help with them. The search runs on
        // the calling thread either way.
        tbb::this_task_arena::isolate([&] {
            r.resultcode = nlopt_optimize(nl.ptr, r.optimum.data(), &r.score);
        });

        return r;
    }

public:

    template<class Fn, size_t N, class...EqFns, class...IneqFns>
    Result<N> optimize(Fn&& f,
                       const Input<N> &initvals,
                       const Bounds<N>& bounds,
                       const std::tuple<EqFns...> &equalities,
                       const std::tuple<IneqFns...> &inequalities)
    {
        if constexpr (IsAUGLAG<M>) {
            NLoptRAII nl_wrap{NLOPT_AUGLAG, N};
            set_up(nl_wrap, bounds, get_criteria());

            NLoptRAII nl_glob{GetNLoptAlg_Global<M>, N};
            set_up(nl_glob, bounds, get_criteria());
            nlopt_set_local_optimizer(nl_wrap.ptr, nl_glob.ptr);

            if constexpr (GetNLoptAlg_Local<M> < NLOPT_NUM_ALGORITHMS) {
                NLoptRAII nl_loc{GetNLoptAlg_Local<M>, N};
                set_up(nl_loc, bounds, m_loc_stopcr);
                nlopt_set_local_optimizer(nl_glob.ptr, nl_loc.ptr);

                return optimize(nl_wrap, std::forward<Fn>(f), initvals,
                                equalities, inequalities);
            } else {
                return optimize(nl_wrap, std::forward<Fn>(f), initvals,
                                equalities, inequalities);
            }
        } else {
            NLoptRAII nl_glob{GetNLoptAlg_Global<M>, N};
            set_up(nl_glob, bounds, get_criteria());

            if constexpr (GetNLoptAlg_Local<M> < NLOPT_NUM_ALGORITHMS) {
                NLoptRAII nl_loc{GetNLoptAlg_Local<M>, N};
                set_up(nl_loc, bounds, m_loc_stopcr);
                nlopt_set_local_optimizer(nl_glob.ptr, nl_loc.ptr);

                return optimize(nl_glob, std::forward<Fn>(f), initvals,
                                equalities, inequalities);
            } else {
                return optimize(nl_glob, std::forward<Fn>(f), initvals,
                                equalities, inequalities);
            }
        }

        assert(false);

        return {};
    }

    explicit NLoptOpt(const StopCriteria &stopcr_glob = {})
        : m_stopcr(stopcr_glob)
    {}

    void set_criteria(const StopCriteria &cr) { m_stopcr = cr; }
    const StopCriteria &get_criteria() const noexcept { return m_stopcr; }

    void set_loc_criteria(const StopCriteria &cr) { m_loc_stopcr = cr; }
    const StopCriteria &get_loc_criteria() const noexcept { return m_loc_stopcr; }

    void set_dir(OptDir dir) noexcept { m_dir = dir; }
    void seed(long s) { m_seed = s; }
};

template<class Alg> struct AlgFeatures_ {
    static constexpr bool SupportsInequalities = false;
    static constexpr bool SupportsEqualities   = false;
};

} // namespace detail;

template<class Alg> constexpr bool SupportsEqualities =
    detail::AlgFeatures_<std::remove_cvref_t<Alg>>::SupportsEqualities;

template<class Alg> constexpr bool SupportsInequalities =
    detail::AlgFeatures_<std::remove_cvref_t<Alg>>::SupportsInequalities;

// Optimizers based on NLopt.
template<class M> class Optimizer<M, detail::NLoptOnly<M>> {
    detail::NLoptOpt<M> m_opt;

public:

    Optimizer& to_max() { m_opt.set_dir(detail::OptDir::MAX); return *this; }
    Optimizer& to_min() { m_opt.set_dir(detail::OptDir::MIN); return *this; }

    template<class Func, size_t N, class...EqFns, class...IneqFns>
    Result<N> optimize(Func&& func,
                       const Input<N> &initvals,
                       const Bounds<N>& bounds,
                       const std::tuple<EqFns...> &eq_constraints = {},
                       const std::tuple<IneqFns...> &ineq_constraint = {})
    {
        static_assert(std::tuple_size_v<std::tuple<EqFns...>> == 0
                          || SupportsEqualities<M>,
                      "Equality constraints are not supported.");

        static_assert(std::tuple_size_v<std::tuple<IneqFns...>> == 0
                          || SupportsInequalities<M>,
                      "Inequality constraints are not supported.");

        return m_opt.optimize(std::forward<Func>(func), initvals, bounds,
                              eq_constraints,
                              ineq_constraint);
    }

    explicit Optimizer(StopCriteria stopcr = {}) : m_opt(stopcr) {}

    Optimizer &set_criteria(const StopCriteria &cr)
    {
        m_opt.set_criteria(cr); return *this;
    }

    const StopCriteria &get_criteria() const { return m_opt.get_criteria(); }

    void seed(long s) { m_opt.seed(s); }

    void set_loc_criteria(const StopCriteria &cr) { m_opt.set_loc_criteria(cr); }
    const StopCriteria &get_loc_criteria() const noexcept { return m_opt.get_loc_criteria(); }
};

// Predefinded NLopt algorithms
using AlgNLoptGenetic     = detail::NLoptAlgComb<NLOPT_GN_ESCH>;
using AlgNLoptSubplex     = detail::NLoptAlg<NLOPT_LN_SBPLX>;
using AlgNLoptSimplex     = detail::NLoptAlg<NLOPT_LN_NELDERMEAD>;
using AlgNLoptCobyla      = detail::NLoptAlg<NLOPT_LN_COBYLA>;
using AlgNLoptDIRECT      = detail::NLoptAlg<NLOPT_GN_DIRECT>;
using AlgNLoptORIG_DIRECT = detail::NLoptAlg<NLOPT_GN_ORIG_DIRECT>;
using AlgNLoptISRES       = detail::NLoptAlg<NLOPT_GN_ISRES>;
using AlgNLoptAGS         = detail::NLoptAlg<NLOPT_GN_AGS>;

using AlgNLoptMLSL_Subplx    = detail::NLoptAlgComb<NLOPT_GN_MLSL_LDS, NLOPT_LN_SBPLX>;
using AlgNLoptMLSL_Cobyla    = detail::NLoptAlgComb<NLOPT_GN_MLSL, NLOPT_LN_COBYLA>;
using AlgNLoptGenetic_Subplx = detail::NLoptAlgComb<NLOPT_GN_ESCH, NLOPT_LN_SBPLX>;

// To craft auglag algorithms (constraint support through object function transformation)
using detail::NLoptAUGLAG;

namespace detail {

template<> struct AlgFeatures_<AlgNLoptCobyla> {
    static constexpr bool SupportsInequalities = true;
    static constexpr bool SupportsEqualities   = true;
};

template<> struct AlgFeatures_<AlgNLoptISRES> {
    static constexpr bool SupportsInequalities = true;
    static constexpr bool SupportsEqualities   = false;
};

template<> struct AlgFeatures_<AlgNLoptORIG_DIRECT> {
    static constexpr bool SupportsInequalities = true;
    static constexpr bool SupportsEqualities   = false;
};

template<> struct AlgFeatures_<AlgNLoptAGS> {
    static constexpr bool SupportsInequalities = true;
    static constexpr bool SupportsEqualities   = true;
};

template<class M> struct AlgFeatures_<NLoptAUGLAG<M>> {
    static constexpr bool SupportsInequalities = true;
    static constexpr bool SupportsEqualities   = true;
};

} // namespace detail

}

#endif // NLOPTOPTIMIZER_HPP
