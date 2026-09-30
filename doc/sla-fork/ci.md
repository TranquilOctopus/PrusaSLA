# SLA CI (PLAN G1, todo M0.14)

`.github/workflows/sla-ci.yml` is the fork's CI: it builds the deps, builds and runs the two test
binaries, runs the M0.13 benchmark harness over the committed models, and puts the metrics diff on
the pull request. **It has never been run.** Nothing in this file has been executed on a runner; the
steps are marked below with what to watch on the first run.

It is manual on purpose. Actions minutes can cost money on a personal fork, so there is no `push`
trigger and no schedule: a run only happens when a person asks for one, by hand or with a label.

## What a run does

| Step | What it does |
|---|---|
| System packages | `doc/Build.md`'s list, minus `libwebkit2gtk-4.1-dev` |
| Restore the deps prefix | `actions/cache`, path `deps/build/destdir`, keyed on the deps CMake files |
| Build the deps | `cmake -S deps -B deps/build` and `cmake --build deps/build`, skipped on a cache hit |
| Configure | `cmake --preset default -G Ninja` with the deps prefix, webkit off, no debug symbols |
| Build the two test targets | `slic3r-shared-tests` first, then `sla_print_tests`, `-j 2`, Release |
| Run both | logs in the `sla-test-output` artifact, plus a short job summary |
| Benchmark | `sla_print_tests "[benchmark]"` over the committed `tests/data` models, `sla_benchmark.json` |
| Metrics diff | `doc/sla-fork/tools/bench_diff.py` against `doc/sla-fork/baseline.json` when it exists |
| Comment | a second job posts that table on the pull request and updates it on later pushes |

The benchmark step sets `SLA_BENCH_FILTER=testdata` and deliberately leaves `SLA_BENCH_DIR` unset,
so the harness runs its built in set: the six small models in `tests/data`, which are in the
repository. `local-samples/` is never used, read or uploaded; the M0.12 corpus is not in the
repository and never may be.

## How to trigger it

**By hand.** Repository -> Actions -> *SLA CI (manual)* -> *Run workflow*. The one input is
*Fail the run when a benchmark layer_hash changes* (off by default, see the baseline section).

**With a label.** Add the `run-ci` label to a pull request that targets an `sla/` branch:

```
gh pr edit 123 --add-label run-ci
```

The label is read from the event payload, so there is no gate job and no token is used for the
check. Removing the label cancels a run in flight (the `labeled` event starts a run in the same
concurrency group whose jobs are all skipped, which cancels the old one). Every later push to that
pull request starts a fresh run, as long as the label is still on it.

To create the label, once: repository -> Issues -> Labels -> *New label*, name `run-ci`, any colour.
With the GitHub CLI: `gh label create run-ci --color 0e8a16 --description "Run SLA CI"`.

Pull requests from forks never run, even with the label: the workflow file would come from the fork
and the comment job holds a write token. Push the branch to this repository instead.

## What it costs

A run is Linux minutes from the `ubuntu-24.04` runner pool, and most of it is the build.

- **Deps, no cache hit:** the 50 dependency builds, OCCT and wxWidgets among them. This is the
  long pole, budget an hour and a half on a 4-core runner.
- **Deps, cache hit:** seconds, the prefix is restored instead of built. The cache is written at the
  end of the first run that built it and is keyed on `deps/CMakeLists.txt`, `deps/CMakePresets.json`,
  every `deps/+*/CMakeLists.txt` and `cmake/modules/AddCMakeProject.cmake`, so touching a
  dependency or the module that drives the builds rebuilds it and the old entry expires.
- **The application build:** `libslic3r`, `slic3r-shared` and the two test binaries. On the same
  runner, order an hour, more if it is cold.
- **The tests and the benchmark:** minutes. The benchmark slices six models at 0.05 mm layer height.

So: a first run is a two-hour thing, later runs are about an hour. Ways to spend less, none of them
done: cache the build tree with ccache, or turn STEP support off (the `no-occt` preset pair, and OCCT
is the biggest single dependency).

Two budgets to set before the first run, in repository *Settings -> Billing and licensing -> Budgets
and alerts*: an alert at the number of minutes you are willing to spend, and the per-repository
spending limit. If the repository is public, Linux runners are free and this is all moot; minutes
are only billed to private repositories. `concurrency` is already set to cancel in progress, so a
chain of pushes to a labelled pull request costs one run, not one per push.

## Timeouts and failure behaviour

- The job is capped at `timeout-minutes: 360`, which is GitHub's maximum, so it is a hard limit
  rather than a decision. The deps build step is capped at 150 minutes and the application build at
  200, so a run that is going to overrun says which step it was in.
- A cold run (deps cache miss) is the one that can reach the cap. If it does, the fix is a larger
  runner for that one run (`ubuntu-24.04-large`), not a longer timeout.
- The `sla_print_tests` run and the benchmark run are skipped when a suite failed. The metrics of a
  broken engine are worth nothing, and the run has already spent the minutes.
- The upload and the job summary run whatever happened, so a failed run still has its output in the
  artifact and in the summary.

## The baseline and the `layer_hash` gate

`bench_diff.py` exits 1 when a `layer_hash` changed, which is a real change in the sliced geometry.
The workflow does **not** turn that into a failed run by default. The reason is the first Linux run:
a `baseline.json` produced by the maintainer on Windows is expected to disagree with a Linux run, and
whether the engine is bit identical across the two is exactly what nobody has checked yet.

The order that gets the gate switched on:

1. Run the workflow by hand once. The diff step writes "no baseline" if `doc/sla-fork/baseline.json`
   is missing, which it is: M0.13 is unticked and the baseline has not been produced.
2. Take `sla_benchmark.json` out of the `sla-test-output` artifact of that run and commit it as
   `doc/sla-fork/baseline.json` if the six `tests/data` models are what the baseline should cover.
   (If the baseline is meant to cover the M0.12 corpus instead, it has to be produced from a local
   run over `local-samples/` and committed with model ids only, never file names.)
3. Run it again. Now the diff is real, and the comment shows a table.
4. If the hashes are identical on Linux, tick *Fail the run when a benchmark layer_hash changes* on
   the next by-hand run and from then on run pull requests with that box on, or make the input
   default `true` in the workflow.
5. If they are not identical, that is an engine finding, not a CI problem: `layer_hash` is supposed
   to be reproducible for a commit. Record it and decide then whether the baseline is per platform
   or the hash needs to be platform neutral.

## Steps to check on the first run

These are the ones the author could not verify without running it, all of them read-and-think rather
than tested:

- **`-G Ninja` together with `--preset default`.** CMake 4.4 (the version in BUILD.md) takes the
  command line generator as a cache variable that overrides the preset, and that is how it is
  written. If a CMake version refuses, drop `-G Ninja` from the *Configure* step: the run then
  configures with Unix Makefiles, which is only slower.
- **The apt list.** The system packages come from `doc/Build.md` for Ubuntu 26.04, on the
  `ubuntu-24.04` image. Anything the configure step says it cannot find is one `apt-get install`
  line, and the fix belongs in that step.
- **The `find` for the two binaries.** They are expected at
  `build-default/tests/sla_print/sla_print_tests` and
  `build-default/src/slic3r-shared/slic3r-shared-tests`, which is where `add_executable()` puts them
  with Ninja and no `RUNTIME_OUTPUT_DIRECTORY`. If they are elsewhere, the step prints where it
  looked.
- **`contains(github.event.pull_request.labels.*.name, 'run-ci')`.** The label list is in the
  `pull_request` payload, so this needs no API call. Check it by adding the label to a draft pull
  request: the run should appear, and a run without the label should appear with every job skipped.
- **The comment job.** `createComment` and `updateComment` are called as issues API calls on the
  pull request number, which is what the REST API wants; that is why the job asks for both
  `issues: write` and `pull-requests: write`. It edits its own previous comment (matched on the
  `<!-- sla-ci-metrics -->` marker) so a push does not fill the pull request with tables. It runs
  even when the build job failed, and then the body says the diff did not run.
- **`build-default` against the deps prefix.** `CMAKE_PREFIX_PATH` points at the absolute destdir,
  and wxWidgets hardcodes that path into its `wx-config`, so the cached prefix is only valid in the
  runner's usual workspace path (`/home/runner/work/<repo>/<repo>`). If the prefix is ever restored
  somewhere else, wx is not found; the cache key does not carry the path on purpose, because a
  second prefix copy is far bigger than it is worth.

## Not covered

- Windows and macOS, and the app, the wx shell and the visual regression renders: this job builds
  the two test binaries and nothing else. The visual regression tool (M6.2) and the nightly merge
  rehearsal (M6.3, which needs this todo) still have no job.
- The benchmark on a pull request uses the committed test models, not the M0.12 corpus, because the
  corpus is not in the repository. A metrics diff over the real corpus stays a local run.
- Formatting, static analysis and the upstream platform builds: the workflows inherited from
  upstream are untouched and still call `PrusaSlicer-Actions`.
# CI and the upstream merge rehearsal

Two things live here, both run by hand:

- **the upstream merge rehearsal** ([M6.3](ROADMAP.md), PLAN G4): a script that merges
  `upstream/master` into the fork branch in a throwaway worktree and writes a conflict report,
- **the optional workflow** (`.github/workflows/upstream_rehearsal.yml`) that runs that script and
  uploads the report. It is `workflow_dispatch` only.

The main CI workflow (build, both test binaries, a metrics diff comment) is still
[M0.14](ROADMAP.md) and is not written yet. When it lands, the rehearsal job can move into that
file; until then it is its own small workflow.

## The rehearsal script

`doc/sla-fork/tools/upstream_rehearsal.py`, standard library only, no build and no test run.

```bash
# From the repository root. Prints the report.
python doc/sla-fork/tools/upstream_rehearsal.py

# Write both reports where CI can pick them up.
python doc/sla-fork/tools/upstream_rehearsal.py \
    --report upstream-rehearsal.md --json upstream-rehearsal.json

# Another branch, another remote, without fetching again.
python doc/sla-fork/tools/upstream_rehearsal.py \
    --branch sla/main --remote upstream --upstream-branch master

# Keep the throwaway worktree to look at a conflict by hand.
python doc/sla-fork/tools/upstream_rehearsal.py --keep-worktree
```

What one run does:

1. `git fetch` the read-only `upstream` remote (no push, ever),
2. find the last merge base between the branch and `upstream/master` and count the upstream commits
   since it,
3. `git worktree add --detach` a throwaway worktree under the gitignored `.agent-scratch/`, at the
   branch tip, and run `git merge --no-commit --no-ff` there,
4. collect the unmerged files (`--diff-filter=U`), count the conflict hunks in each (the `<<<<<<<`
   markers git wrote) and write the report,
5. `git merge --abort` and remove the worktree, also when the run failed (`try/finally`).

Because the worktree is a **detached** HEAD, a merge inside it cannot update `sla/main`, `master`
or any other branch, and the merge is never committed. The tip is read again at the end and the
report says whether it moved. `.agent-scratch/` is in `.gitignore` and is the only folder the run
writes to (apart from `--report` / `--json`, which you name).

The `upstream` remote is added once, read only:

```bash
git remote add upstream https://github.com/prusa3d/PrusaSlicer.git
git remote set-url --push upstream DISABLED   # a push is impossible by accident
```

### Exit codes

| Code | Meaning |
| --- | --- |
| 0 | the merge was clean |
| 1 | there were conflicts (a normal result, the report is written) |
| 2 | the rehearsal could not run: no remote, no branch, no merge base, or git failed |

A conflict is not a failure of the tool, so the workflow uploads the report `if: always()` and only
fails the job when it is told to (`fail_on_conflicts`).

## What the report says

Markdown (and the same thing as JSON with `--json`):

- how many upstream commits there are since the last merge base, and the fork's own count,
- how many files the merge touched,
- the conflicted files **grouped by area**, each with its hunk count, its state (both modified,
  added upstream, deleted upstream, ...) and whether it is a PLAN 3.3 hotspot file,
- a **hotspot section** on its own, because a conflict in `SLAResult.hpp`, `Theme.cpp`,
  `ConfigDefsSLA.cpp`, `IGizmo.hpp`, `PlaterRenderModule.cpp` or any `CMakeLists.txt` is the one
  PLAN 3.3 says to merge ahead of other work in a small PR,
- the newest upstream commit subjects since the merge base (capped at 20, the count is exact),
- what the run did, so the report is self explaining when it is read months later in a CI log.

The areas are `engine SLA` (`src/libslic3r/`), `config defs` (`ConfigDefs*`, `ConfigBoxes*`,
`Defines.h`, `resources/presets`, `resources/profiles`), `App/Biz` (`src/slic3r-shared/`,
`src/slic3r-domain/`, `src/slic3r-biz-*`, `src/slic3r-render/`), `build` (`CMakeLists.txt`,
`*.cmake`, `cmake/`, `CMakePresets.json`, `version.inc`, `.github/workflows/`) and `other`.

## Running it in CI

The workflow is manual: **Actions → Upstream merge rehearsal → Run workflow**. Inputs: the branch
(default `sla/main`), the upstream URL and branch, and whether a conflict should fail the job. The
checkout is a full clone (`fetch-depth: 0`), because a shallow one has no merge base, and the job
adds the upstream remote itself, since a CI checkout only knows the fork. The report goes into the
job summary and into the artifact `upstream-rehearsal-<run id>`, as Markdown, JSON and the raw log.

A nightly run is one scheduled line on top of `workflow_dispatch`:

```yaml
on:
  workflow_dispatch:
  schedule:
    - cron: "23 4 * * *"   # a quiet hour, once a day
```

Nothing was run yet: the first rehearsal is a person's job. PLAN G4 also asks for a build and a
test run of the merged tree. That is not in the script (it would need the whole build, which is
what the M0.14 runner is for) and is still open.

## Tests

The report builder is a pure function of a plain dict, so it is tested on a made up conflict list,
with no git command and no merge:

```bash
python doc/sla-fork/tools/test_upstream_rehearsal.py
python -m unittest discover -s doc/sla-fork/tools -p "test_upstream_rehearsal.py"
```

The CI job runs that before the rehearsal, so a broken report fails the run before it fetches
anything.
