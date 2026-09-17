# PrusaSLA build and synchronization

## Build bootstrap (verified 2026-09-17)

### Tools

- Visual Studio 2022 Community 17.14 with the "Desktop development with C++" workload (MSVC 14.44, Windows SDK 10.0.26100)
- CMake 4.4.3 (on PATH as `C:\Program Files\CMake\bin`)

### Environment

**ALWAYS build from the "Developer PowerShell for VS 2022" (or run `Enter-VsDevShell`).** Otherwise `WindowsSdkDir`/`WindowsSDKVersion` are unset, `cmake/modules/WinSDK.cmake` finds no WinRT headers, and configure fails with:

```
SLIC3R_ENABLE_WIN10_MESH_REPAIR is ON, but the Windows WinRT headers could not be found
```

### Dependencies

```powershell
cmake -S deps -B deps/build -G "Visual Studio 17 2022" -A x64 -DDEP_DEBUG=OFF
cmake --build deps/build --config Release -- /m:1 /nodeReuse:false
```

- `DEP_DEBUG` defaults to `ON` on Windows and triggers a second, hours-long Debug pass of every dependency that this project does not use; turn it off.
- 48 of the 50 dependencies build on Windows (OpenCSG and OpenSSL are `EXCLUDE_FROM_ALL` there).
- Took about 5 hours; installs into `deps/build/destdir/usr/local`.

### Application

```powershell
$env:_CL_ = "/MP2"
cmake --preset default -DSLIC3R_PCH=OFF -DSLIC3R_RELEASE_DEBUG_SYMBOLS=OFF -DCMAKE_PREFIX_PATH="<repo>/deps/build/destdir/usr/local"
cmake --build build-default --target sla_print_tests --config Release -- /m:1 /nodeReuse:false
cmake --build build-default --target slic3r-shared-tests --config Release -- /m:1 /nodeReuse:false
```

- `SLIC3R_PCH=OFF` is required: the `default` preset sets `SLIC3R_PCH=ON` and `CMakeLists.txt:18` hard-errors on MSVC ("SLIC3R_PCH was explicitly requested, but they are currently not supported on MSVC").
- Memory: this machine has 8 GB RAM / 8 threads. Set `$env:_CL_ = "/MP2"` before building and pass `/m:1`. Full parallelism (18 `cl.exe` processes) exhausted RAM and failed OCCT's `TKSTEP` with `MSB4018 System.OutOfMemoryException`.
- Disk: the dependency build plus an application build needs roughly 20 GB free. A `RelWithDebInfo` build filled the disk and failed with "error C2471: cannot update program database"; `Release` with `SLIC3R_RELEASE_DEBUG_SYMBOLS=OFF` is what we use. Safe to delete after a successful dependency build: `deps/build/builds`, `deps/build/_d` (if `DEP_DEBUG` was on), and `build-default`; never delete `deps/build/destdir`.

### Build times (measured here)

| Target | Time |
|---|---|
| `sla_print_tests` | 3h51m |
| `slic3r-shared-tests` | 30m |
| **Total** | **263 minutes** at `/MP2` |

### Test binaries

- `build-default\tests\sla_print\Release\sla_print_tests.exe`
- `build-default\src\slic3r-shared\Release\slic3r-shared-tests.exe`

## Git setup

`origin` is the fork; `upstream` is Prusa's source repository, as specified by PLAN D3:

| Remote | Fetch URL | Push URL |
|---|---|---|
| `origin` | `https://github.com/TranquilOctopus/PrusaSLA.git` | Same as fetch |
| `upstream` | `https://github.com/prusa3d/PrusaSlicer.git` | `DISABLED` (existing local safeguard, preserved) |

`sla/main` was created from `master` at `25ffc61ffe47d9f4ab5e9b449c0b3bcf65778ecf`. A concurrent process subsequently advanced it to `ffe36667eb97b31b42056bce1966634364d3a4b8`; that change was preserved. M0.3 work lives on `sla/M0.3-git-setup`. Remote configuration is local Git metadata and must be repeated in other clones.

For another clone, inspect `git remote -v` first. Add a missing remote with `git remote add upstream https://github.com/prusa3d/PrusaSlicer.git`; if it already exists, use `git remote set-url upstream https://github.com/prusa3d/PrusaSlicer.git`. Create `sla/main` with `git branch sla/main master` only if it does not exist. Never reset an existing branch to repeat setup.

## Sync procedure (manual, not executed this session)

Run from the repository root in PowerShell, executing each command only after the previous one succeeds:

1. Run `git status --short`. Stop if there are local changes; preserve them before syncing.
2. Run `git fetch upstream`. This updates remote-tracking refs, not local `master` or `sla/main`.
3. Run `git branch -r` and confirm the intended remote branch exists. If `upstream/master` is absent, stop and ask which remote branch to use.
4. Run `git log --oneline sla/main..upstream/master` and `git diff --stat sla/main upstream/master` to review the candidate changes.
5. Create a new review branch: `git switch -c sla/sync-YYYYMMDD sla/main`, choosing a unique date/suffix.
6. Run `git merge --no-ff --no-commit upstream/master`. Review changes and resolve conflicts on this branch only; `git merge --abort` abandons a conflicted or pending merge. An already-up-to-date result needs no commit.
7. Once M0.1 and M0.2 are complete, build and run the affected tests using the verified build instructions and baseline. Do not claim a sync is verified while the required build is unavailable.
8. Inspect `git status`, `git diff`, `git diff --cached`, and `git log --oneline -10`. Commit the reviewed merge only with authorization. Integration into `sla/main` is a separate owner-approved action.

Do not push, force-push, or rewrite `master` or `sla/main`. No fetch, merge from a remote, or network-access check was performed for M0.3; verification covered local remote configuration, branch creation history, and this documented procedure.