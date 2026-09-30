# Agent instructions: PrusaSLA

This repository is a fork of PrusaSlicer 3.0 (alpha) that is being turned into an **SLA-focused slicer**. It's C++20 built with CMake. The UI uses ImGui with Yoga layout, and the desktop shell uses wxWidgets.

## Where the work is
- **Todo list (source of truth):** `doc/sla-fork/ROADMAP.md`. Work on exactly one todo per session.
- **Architecture map:** `doc/sla-fork/ARCHITECTURE-SLA.md`. Where the SLA code is, by layer and by subsystem: config, supports, slicing, export, resin import, tests.
- **Background and rules:** `doc/sla-fork/PLAN.md`. Read section 1 (codebase map), 2.1 (color palette), 3.3 (hotspot files) and 3.5 (definition of done).
- **Build notes:** `doc/sla-fork/BUILD.md` (created by todo M0.1). Until it exists, follow `doc/Build.md`.

## Workflow for a todo
1. Read the todo and check that every ID in its `needs` is ticked. If one isn't, stop and report.
2. If the todo is tagged `[human]`, don't attempt it. Report what the person needs to do.
3. If it's sized `L`, split it into sub-todos in ROADMAP.md first, commit the split, then work on the first sub-todo.
4. Create branch `sla/<ID>-<short-slug>` from `sla/main` (or from `master` if `sla/main` doesn't exist yet).
5. Implement it, adding tests for new behavior.
6. Build **only the targets you need.** Full app builds are slow.
   - Engine (`src/libslic3r`): `cmake --build build-default --target sla_print_tests --config RelWithDebInfo`
   - App and Biz layers (`src/slic3r-shared`): `cmake --build build-default --target slic3r-shared-tests --config RelWithDebInfo`
   - Run the resulting test binaries. Compare failures with `doc/sla-fork/baseline-tests.md`.
7. Tick the todo in ROADMAP.md **in the same commit**, with a short result note. Commit messages start with the todo ID, e.g. `M3.4: Chitubox .cfg reader`.
8. If you're blocked, leave the box unticked, add `  Blocked: <reason>` under the todo, and commit that.

## Hard rules
- **Layering:** dependencies go `App → Biz → Domain` (see `src/slic3r-shared/README.md`). `App` stays platform-agnostic, and platform code lives in `slic3r-app-desktop` / `slic3r-platform-wx`.
- **Legacy code:** `src/slic3r/GUI` isn't built. Use it only as a reference when porting, and don't add to it.
- **Hotspot files** (PLAN 3.3), such as `IGizmo.hpp`, `PlaterRenderModule.cpp`, `ConfigDefsSLA.cpp`, `Theme.cpp` and `SLAResult.hpp`: keep edits minimal and add new work in new files where possible.
- **Colors:** use `Platform::Color` theme tokens only. Palette hex values live only in the palette table in `Theme.cpp`. The allowed colors are `#CAD2C5`, `#84A98C`, `#52796F`, `#354F52` and `#2F3E46`, plus the warning amber and error red. See PLAN 2.1.
- **Upstream mergeability:** don't delete FFF code. Hide it behind the SLA-first setting.
- **Strings:** mark user-visible strings for translation with `L("…")` / `_u8L("…")`.
- **Style:** follow `doc/CodeStyle.md` and apply `.clang-format` to the lines you change.
- **Third-party content:** don't commit vendor resin or printer profiles, or models you don't have redistribution rights for. Real samples go in the gitignored `local-samples/`, and test fixtures are written from scratch. Don't bypass encryption in foreign file formats. For the M7 support research dataset, follow the research-only rules in ROADMAP.md M7: files stay in place, never leave this computer, are never opened in agent context, and only aggregate statistics are committed.
- **Commits:** don't push, force-push or rewrite `master` or `sla/main` unless the person running the session asks.
- **Never slice from a tool or UI action.** Only the Slice button slices. Supports use `libslic3r/SLASupportTool.hpp` on a worker thread; nothing may call slice/slice_bed/auto-slicing for SLA.
- **Benchmark and local models:** committed output names models by manifest id only (`bm01`...), never file names or paths; `[.local]` tests are skipped unless `SLA_LOCAL_SAMPLES` is set.

## Traps that cost a build
These broke MSVC builds more than once; check your diff for them before committing.
- Qualify `Biz::` / `Domain::` names and include every header you use (a forward declaration does not convert to its base class).
- `_u8L` drops non-ASCII bytes: keep …, ·, ° and friends outside the translated string.
- Yoga `Text` has no default constructor (pass the text); `ComboBox` needs an explicit `std::string` name; `_fpx` needs `using Slic3r::App::Yoga::operator""_fpx`.
- Catch2: no `||`/`&&` or ternaries inside `CHECK`/`REQUIRE`; `Approx` needs `catch_approx.hpp`. Store `e.what()` in a `std::string`, never a `const char*` past the catch block.
- Eigen: don't mix float/double or a 4x4 `.matrix()` with a 3x3 (`.linear()`).
- A new pure virtual must be implemented by every subclass, including ones added on other branches; a new source/test file is listed in CMake exactly once.
- Docs (`ROADMAP.md`, `README.md`) use CRLF: keep it. Never edit inside the README `PROGRESS` markers; run `doc/sla-fork/tools/readme_progress.py`.
- Coding agents: stay inside your worktree, never `cd` and never write `..` in a path (the agent sandbox rejects it and the job ends); scratch files go in `.agent-scratch/`.
- Read the SLA code map, `doc/sla-fork/ARCHITECTURE-SLA.md`, before exploring the tree.
