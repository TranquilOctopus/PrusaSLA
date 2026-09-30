#ifdef _WIN32
// The lean Windows includes, as everywhere else in the tree.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <libassert/assert.hpp>
#include <cstdio>
#endif // _WIN32

#include <catch_main.hpp>

#ifdef _WIN32
namespace {
// Catch2 installs a structured exception handler of its own, so a crash is only ever reported as
// "SIGSEGV - Segmentation violation signal" and the interesting part - where it happened - is
// lost. A vectored handler runs before Catch2's one, so we get to print the native stack and then
// hand the exception on untouched, leaving Catch2's reporting (and the exit code) as it was.
LONG CALLBACK print_crash_stack(EXCEPTION_POINTERS* info)
{
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_STACK_OVERFLOW ||
        code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_INT_DIVIDE_BY_ZERO) {
        fprintf(stderr, "\n=== native crash 0x%08lx, stack:\n%s\n", code,
                libassert::stacktrace().c_str());
        fflush(stderr);
    }
    return EXCEPTION_CONTINUE_SEARCH; // Let Catch2 report it as before.
}

// main() comes from catch_main.hpp (CATCH_CONFIG_MAIN), so the handler is installed from a static
// initializer instead of at the top of main.
const PVOID s_crash_stack_handler = AddVectoredExceptionHandler(1, print_crash_stack);
} // namespace
#endif // _WIN32
