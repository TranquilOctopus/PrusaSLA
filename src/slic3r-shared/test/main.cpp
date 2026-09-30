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

#include <catch2/catch_session.hpp>
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/SecretStoreDummy.hpp"
#include "Slic3r/Log.hpp"

#if !defined(__has_feature)
#define __has_feature(x) 0
#endif

#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
extern "C" {
    const char* __lsan_default_suppressions()
    {
        return "leak:TPrsStd_DriverTable::Get\n"; // OpenCASCADE singleton, not a real leak.
    }
}
#endif

using Slic3r::App::Platform::StdMainThreadDispatcher;
using Slic3r::Biz::SecretStoreDummy;

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
} // namespace
#endif // _WIN32

int main( int argc, char* argv[] ) {
#ifdef _WIN32
    AddVectoredExceptionHandler(1, print_crash_stack);
#endif // _WIN32

    Slic3r::init_logging();
    Slic3r::set_log_level(0);

    using Slic3r::Biz::Platform::PlatformServices;

    PlatformServices::instance().set_main_thread_dispatcher(
        std::make_unique<StdMainThreadDispatcher>()
    );

    std::unique_ptr<SecretStoreDummy> store_dummy{std::make_unique<SecretStoreDummy>()};
    PlatformServices::instance().set_secret_store(std::move(store_dummy));

    const int result = Catch::Session().run( argc, argv );

    return result;
}
