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