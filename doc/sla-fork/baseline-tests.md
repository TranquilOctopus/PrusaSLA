# Baseline test results (2026-09-17)

Recorded from a clean checkout at commit `6ffa7e3499`, Release build, following the verified build guide in [BUILD.md](BUILD.md).

## sla_print_tests

- **Result:** all tests passed
- **Assertions:** 12117
- **Test cases:** 40
- **Time:** 1.1 minutes
- **Exit code:** 0

Binary: `build-default\tests\sla_print\Release\sla_print_tests.exe`

## slic3r-shared-tests

- **Result:** all tests passed
- **Assertions:** 7573
- **Test cases:** 444
- **Time:** 1.5 minutes
- **Exit code:** 0

Binary: `build-default\src\slic3r-shared\Release\slic3r-shared-tests.exe`

## Summary

No pre-existing failures: any future failure is a regression introduced by our own changes.

## Note: Python research suite

The Python research suite in `tools/support-research/` is separate from the C++ test binaries above.

- **Tests:** 75
- **Time:** about 76 seconds
- Runs in a local Python venv; not part of the CMake build or CI pipeline.