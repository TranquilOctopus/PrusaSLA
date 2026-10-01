#ifndef SLA_JOBCONTROLLER_HPP
#define SLA_JOBCONTROLLER_HPP

#include <functional>
#include <string>

namespace Slic3r { namespace sla {

// The defaults below are named functions and not lambdas on purpose. MSVC mangles
// the lambdas of a default member initializer and of a default function argument
// with a counter that is per namespace, not per class, so the same mangled name
// can stand for different lambdas in different translation units and the linker
// keeps one body. A void `[]() {}` then replaced the `return false` body and the
// default stop condition returned whatever was left in the return register.
namespace detail {
inline void no_status(unsigned, const std::string&) {}
inline bool never_stop() { return false; }
inline void never_cancel() {}
} // namespace detail

/// A Control structure for the support calculation. Consists of the status
/// indicator callback and the stop condition predicate.
struct JobController
{
    using StatusFn = std::function<void(unsigned, const std::string&)>;
    using StopCond = std::function<bool(void)>;
    using CancelFn = std::function<void(void)>;

    // This will signal the status of the calculation to the front-end
    StatusFn statuscb = &detail::no_status;

    // Returns true if the calculation should be aborted.
    StopCond stopcondition = &detail::never_stop;

    // Similar to cancel callback. This should check the stop condition and
    // if true, throw an appropriate exception. (TriangleMeshSlicer needs this)
    // consider it a hard abort. stopcondition is permits the algorithm to
    // terminate itself
    CancelFn cancelfn = &detail::never_cancel;
};

}} // namespace Slic3r::sla

#endif // JOBCONTROLLER_HPP
