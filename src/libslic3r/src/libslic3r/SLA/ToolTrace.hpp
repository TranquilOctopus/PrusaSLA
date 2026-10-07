#ifndef SLA_TOOLTRACE_HPP
#define SLA_TOOLTRACE_HPP

#include <cstdio>
#include <cstdlib>
#include <chrono>

namespace Slic3r::sla::tool_trace {

// Read once at first use; no cost when SLA_TOOL_TRACE is unset.
inline bool enabled() {
    static const bool trace = std::getenv("SLA_TOOL_TRACE") != nullptr;
    return trace;
}

// Call once at the start of the traced function to capture the start time.
// Returns the captured start time point.
inline std::chrono::steady_clock::time_point start() {
    return std::chrono::steady_clock::now();
}

// Print a trace line: label, milliseconds since start, and whether stop() is true.
template <typename StopFn>
inline void trace_ms(const char* label,
                     std::chrono::steady_clock::time_point t0,
                     const StopFn& stop) {
    if (!enabled()) return;
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count();
    bool stopped = stop ? stop() : false;
    std::fprintf(stderr, "[SLA_TOOL_TRACE] %s: %lld ms, stop=%d\n", label, (long long)ms, stopped);
}

// Overload without stop function.
inline void trace_ms(const char* label,
                     std::chrono::steady_clock::time_point t0) {
    if (!enabled()) return;
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count();
    std::fprintf(stderr, "[SLA_TOOL_TRACE] %s: %lld ms, stop=n/a\n", label, (long long)ms);
}

} // namespace Slic3r::sla::tool_trace

#endif // SLA_TOOLTRACE_HPP