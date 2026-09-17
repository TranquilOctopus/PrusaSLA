# PrusaSLA build and synchronization

## Build bootstrap (M0.1 pending)

This file currently documents M0.3 only. A verified dependency prefix, configure/build commands, clean-checkout verification, and measured build times still need to be recorded by the person completing M0.1. Follow [../Build.md](../Build.md) for prerequisites. No dependency or application build was run during M0.3; CMake and MSVC were not on PATH, and `build-default` did not exist.

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
