#!/usr/bin/env python3
"""Rehearse merging upstream/master into a fork branch and report the conflicts.

PLAN G4 / roadmap M6.3.

    python doc/sla-fork/tools/upstream_rehearsal.py
    python doc/sla-fork/tools/upstream_rehearsal.py --report report.md --json report.json
    python doc/sla-fork/tools/upstream_rehearsal.py --branch sla/main --remote upstream
    python doc/sla-fork/tools/upstream_rehearsal.py --no-fetch --keep-worktree

What one run does:

1. fetches the read-only `upstream` remote (`git fetch`, never a push),
2. finds the last merge base between the branch and upstream/master and counts the upstream
   commits since it,
3. creates a throwaway worktree under the repo's gitignored `.agent-scratch/`, detached at the
   branch tip, and runs `git merge --no-commit --no-ff` there,
4. collects the unmerged files, counts the conflict hunks in each and writes the report,
5. aborts the merge and removes the worktree, also when the run failed (try/finally).

Nothing outside `.agent-scratch/` is written apart from the fetch. No branch is moved: the
worktree is a detached HEAD, so a merge inside it cannot update `sla/main`, `master` or anything
else, and the merge is never committed. The branch tip is read again at the end and the report
says whether it moved. `--keep-worktree` leaves the throwaway worktree in place for a person who
wants to look at a conflict, and then the report says where it is.

The report groups the conflicted files by area and marks the PLAN 3.3 hotspot files, because a
conflict in a hotspot is the expensive one: those are merged ahead of other work in small PRs.
The areas are:

| Area | What falls in it |
| --- | --- |
| engine SLA | `src/libslic3r/`, where SLAPrint, PrintSteps, SLAResult and `SLA/` live |
| config defs | `ConfigDefs*` / `ConfigBoxes*` / `Defines.h`, plus `resources/presets` and `resources/profiles` |
| App/Biz | `src/slic3r-shared/`, `src/slic3r-domain/`, `src/slic3r-biz-*`, `src/slic3r-render/` |
| build | `CMakeLists.txt`, `*.cmake`, `cmake/`, `CMakePresets.json`, `version.inc`, `.github/workflows/` |
| other | everything else (desktop shell, platform wx, `src/slic3r/GUI`, `tests/`, `doc/`) |

Exit code 0 when the merge was clean, 1 when there were conflicts and 2 when the run itself failed
(no remote, no branch, a git error), so a CI job can tell "upstream moved a file we also touched"
from "the rehearsal could not run". A conflict is a normal result, not a failure of the tool: the
workflow uploads the report either way and only fails the job when it is asked to.

Only the Python standard library is used, on purpose: this has to run in a bare CI runner. The
report builder (`build_report`, `build_json`, `exit_code` and what they call) is a pure function of
a plain dict, which is what `test_upstream_rehearsal.py` tests on a made up conflict list, so no
real merge is needed to check the report.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from datetime import datetime

# The areas of the report, in reporting order. Every conflicted file lands in exactly one.
AREAS = ["engine SLA", "config defs", "App/Biz", "build", "other"]

# PLAN 3.3 hotspot files. Only these exact paths, plus any CMakeLists.txt (PLAN 3.3 lists those as
# hotspots too), count: a fork file next to a hotspot is a normal file, otherwise every conflict
# in the tree would be reported as one.
HOTSPOT_FILES = [
    "src/slic3r-shared/include/Slic3r/App/Scene/IGizmo.hpp",
    "src/slic3r-shared/src/Slic3r/App/Plater/PlaterRenderModule.cpp",
    "src/slic3r-shared/src/Slic3r/App/Plater/PlaterRenderModule.hpp",
    "src/slic3r-shared/src/Slic3r/App/Plater/ToolGizmosUiInfo.cpp",
    "src/slic3r-shared/src/Slic3r/App/Theme.cpp",
    "src/slic3r-shared/include/Slic3r/App/ThemeTypes.hpp",
    "src/slic3r-domain/src/Slic3r/Domain/ConfigDefsSLA.cpp",
    "src/slic3r-domain/src/Slic3r/Domain/ConfigBoxesSLA.cpp",
    "src/slic3r-domain/include/Slic3r/Domain/ConfigDefsSLA.hpp",
    "src/slic3r-domain/include/Slic3r/Domain/ConfigBoxesSLA.hpp",
    "src/libslic3r/include/libslic3r/PrintSteps.hpp",
    "src/libslic3r/include/libslic3r/SLAPrint.hpp",
    "src/libslic3r/include/libslic3r/SLAResult.hpp",
    "src/slic3r-shared/include/Slic3r/Biz/Slicing/SlicingInteractor.hpp",
]

# The porcelain codes git gives an unmerged path in a merge, as words. X is the branch side and Y
# is the upstream side.
STATUS_LABELS = {
    "UU": "both modified",
    "AA": "both added",
    "DU": "deleted here, modified upstream",
    "UD": "modified here, deleted upstream",
    "AU": "added here",
    "UA": "added upstream",
    "DD": "deleted on both sides",
}

# How many upstream commit subjects the report lists. The count itself is always exact.
MAX_LISTED_COMMITS = 20

# The one folder the rehearsal is allowed to write in: it is in .gitignore, so a worktree left
# behind by a killed run never shows up in a status or a commit.
SCRATCH_FOLDER = ".agent-scratch"

SCHEMA_VERSION = 1


def normalise(path: str) -> str:
    """A repo path with forward slashes, so the rules below work on Windows too."""
    return path.replace("\\", "/")


def is_build_file(path: str) -> bool:
    """Build system files, which are their own area and are hotspots when they are a
    CMakeLists.txt (PLAN 3.3)."""
    name = normalise(path)
    leaf = name.rsplit("/", 1)[-1]
    return (
        leaf == "CMakeLists.txt"
        or leaf.endswith(".cmake")
        or leaf == "CMakePresets.json"
        or leaf == "version.inc"
        or name.startswith("cmake/")
        or name.startswith(".github/workflows/")
    )


def is_config_defs_file(path: str) -> bool:
    """Config definitions, boxes and presets: the stream that adds keys and defaults."""
    name = normalise(path)
    leaf = name.rsplit("/", 1)[-1]
    return (
        leaf.startswith("ConfigDefs")
        or leaf.startswith("ConfigBoxes")
        or leaf == "Defines.h"
        or name.startswith("resources/presets/")
        or name.startswith("resources/profiles/")
    )


def is_hotspot(path: str) -> bool:
    """True for a PLAN 3.3 hotspot file or a CMakeLists.txt."""
    return normalise(path) in HOTSPOT_FILES or is_build_file(path)


def hotspot_label(path: str) -> str:
    """The wording the report uses for a hotspot, empty for an ordinary file."""
    if normalise(path) in HOTSPOT_FILES:
        return "PLAN 3.3 hotspot"
    if is_build_file(path):
        return "PLAN 3.3 (any CMakeLists.txt)"
    return ""


def classify(path: str) -> str:
    """The area a conflicted file belongs to. Build and config are tested before the engine, so a
    `ConfigDefs*` file under `src/libslic3r/` is a config conflict and not an engine one."""
    name = normalise(path)
    if is_build_file(name):
        return "build"
    if is_config_defs_file(name):
        return "config defs"
    if name.startswith("src/libslic3r/"):
        return "engine SLA"
    if (
        name.startswith("src/slic3r-shared/")
        or name.startswith("src/slic3r-domain/")
        or name.startswith("src/slic3r-biz-")
        or name.startswith("src/slic3r-render/")
        or name.startswith("src/slic3r-gcode-reader/")
        or name.startswith("src/slic3r-base/")
    ):
        return "App/Biz"
    return "other"


def status_label(status: str) -> str:
    """A porcelain code as words, falling back to the code itself."""
    return STATUS_LABELS.get((status or "").strip()[:2], (status or "").strip() or "unknown")


def describe_conflicts(conflicts: list[dict]) -> list[dict]:
    """The conflict list with its area, hotspot flag and status words added. Pure: this is what
    the report and the JSON are both built from, and what the unit test drives."""
    described = []
    for conflict in conflicts:
        path = normalise(conflict.get("path", ""))
        status = (conflict.get("status") or "").strip()[:2]
        described.append(
            {
                "path": path,
                "hunks": int(conflict.get("hunks", 1)),
                "status": status,
                "status_label": status_label(status),
                "binary": bool(conflict.get("binary", False)),
                "area": classify(path),
                "hotspot": is_hotspot(path),
                "hotspot_label": hotspot_label(path),
            }
        )
    described.sort(key=lambda entry: (AREAS.index(entry["area"]), entry["path"]))
    return described


def group_by_area(described: list[dict]) -> dict[str, list[dict]]:
    """The described conflicts per area, in AREAS order, empty areas included."""
    groups = {area: [] for area in AREAS}
    for entry in described:
        groups[entry["area"]].append(entry)
    return groups


def summarise(described: list[dict]) -> dict:
    """Files, hunks and hotspot counts, overall and per area. Pure."""
    summary = {
        "conflict_files": len(described),
        "conflict_hunks": sum(entry["hunks"] for entry in described),
        "hotspot_files": sum(1 for entry in described if entry["hotspot"]),
        "by_area": {},
    }
    for area, entries in group_by_area(described).items():
        summary["by_area"][area] = {
            "files": len(entries),
            "hunks": sum(entry["hunks"] for entry in entries),
            "hotspots": sum(1 for entry in entries if entry["hotspot"]),
        }
    return summary


def _short(sha: str) -> str:
    return (sha or "")[:12]


def build_report(data: dict) -> str:
    """The Markdown report of one run. Pure function of `data`, which is what the unit test
    passes a made up conflict list to."""
    described = describe_conflicts(data.get("conflicts", []))
    summary = summarise(described)
    groups = group_by_area(described)
    upstream = data.get("upstream") or {}
    fork = data.get("fork") or {}

    lines = [
        "# Upstream merge rehearsal",
        "",
        f"- Generated: {data.get('generated', 'unknown')}",
        f"- Repository: `{data.get('repo', 'unknown')}`",
        f"- Fork branch: `{data.get('fork_ref', 'unknown')}` at `{_short(fork.get('sha', ''))}`"
        f" (tip unchanged by this run: {'yes' if fork.get('tip_unchanged') else 'NO'})",
        f"- Upstream: `{data.get('upstream_remote', 'unknown')}/"
        f"{data.get('upstream_branch', 'unknown')}` at `{_short(upstream.get('sha', ''))}`"
        f" (fetched as `{upstream.get('ref', 'unknown')}`)",
        f"- Last merge base: `{_short(data.get('merge_base', ''))}`",
        f"- Upstream commits since the merge base: {data.get('upstream_commit_count', '?')}",
        f"- Fork commits since the merge base: {data.get('fork_commit_count', '?')}",
        f"- Files the merge touched: {data.get('files_changed', '?')}",
        "",
    ]

    if data.get("error"):
        lines += [
            "## The run failed",
            "",
            f"The rehearsal did not get as far as a merge: {data['error']}",
            "",
        ]

    if data.get("error"):
        pass
    elif not described:
        lines += [
            "## Result",
            "",
            "No conflicts: "
            f"`{data.get('upstream_remote', 'upstream')}/{data.get('upstream_branch', 'master')}` "
            f"merges into `{data.get('fork_ref', 'sla/main')}` cleanly at this point, and the "
            f"merge was aborted again without being committed.",
            "",
        ]
    else:
        lines += [
            f"## Result: {summary['conflict_files']} conflicted files, "
            f"{summary['conflict_hunks']} hunks, {summary['hotspot_files']} of them hotspot files",
            "",
            "| Area | Files | Hunks | Hotspot files |",
            "| --- | --- | --- | --- |",
        ]
        for area in AREAS:
            entry = summary["by_area"][area]
            lines.append(f"| {area} | {entry['files']} | {entry['hunks']} | {entry['hotspots']} |")
        lines.append("")

        hotspots = [entry for entry in described if entry["hotspot"]]
        lines += [
            "## Hotspot files involved",
            "",
        ]
        if hotspots:
            lines.append(
                "PLAN 3.3 says these are merged ahead of other work in small PRs, so a conflict "
                "here is the one to settle first:"
            )
            lines.append("")
            for entry in hotspots:
                lines.append(
                    f"- `{entry['path']}` - {entry['hunks']} hunk(s), {entry['status_label']}, "
                    f"{entry['hotspot_label']} ({entry['area']})"
                )
        else:
            lines.append("None: every conflict is in an ordinary file.")
        lines.append("")

        for area in AREAS:
            entries = groups[area]
            lines.append(f"## Conflicts: {area}")
            lines.append("")
            if not entries:
                lines += ["None.", ""]
                continue
            lines += ["| File | Hunks | State | Hotspot |", "| --- | --- | --- | --- |"]
            for entry in entries:
                marks = []
                if entry["hotspot"]:
                    marks.append(entry["hotspot_label"])
                if entry["binary"]:
                    marks.append("binary")
                lines.append(
                    f"| `{entry['path']}` | {entry['hunks']} | {entry['status_label']} | "
                    f"{', '.join(marks) or '-'} |"
                )
            lines.append("")

    commits = data.get("upstream_commits") or []
    lines += ["## Upstream commits since the merge base", ""]
    if not commits:
        lines += ["None: upstream/master is already merged into the branch.", ""]
    else:
        for commit in commits[:MAX_LISTED_COMMITS]:
            lines.append(f"- `{_short(commit.get('sha', ''))}` {commit.get('subject', '')}")
        if len(commits) > MAX_LISTED_COMMITS:
            lines.append(f"- ... and {len(commits) - MAX_LISTED_COMMITS} more")
        lines.append("")

    merge_output = data.get("merge_output") or ""
    if merge_output:
        lines += ["## What the merge said", "", "```"]
        lines += merge_output.splitlines()[:40]
        lines += ["```", ""]

    if data.get("error"):
        lines += [
            "## What this run did",
            "",
            "- stopped before the merge, so no worktree was left behind and no branch was touched",
            "",
        ]
        return "\n".join(lines)

    lines += [
        "## What this run did",
        "",
        f"- fetched `{data.get('upstream_remote', 'upstream')}` read only, and wrote no ref "
        "outside the remote-tracking one",
        f"- created a detached worktree under `.agent-scratch/` "
        f"(`{data.get('scratch', 'unknown')}`), merged with `--no-commit --no-ff` and never "
        "committed the result",
        "- aborted the merge and removed the worktree again: "
        f"{'kept for inspection (--keep-worktree)' if data.get('kept_worktree') else 'yes'}",
        "- no branch was moved and nothing was pushed",
        "",
    ]
    if data.get("kept_worktree"):
        lines += [
            f"The worktree was left at `{data['scratch']}` (--keep-worktree). Remove it with "
            "`git worktree remove --force <path>` from the repository root once you are done.",
            "",
        ]
    return "\n".join(lines)


def build_json(data: dict) -> str:
    """The same run as JSON, for CI. Pure, and stable in key order."""
    described = describe_conflicts(data.get("conflicts", []))
    summary = summarise(described)
    document = {
        "schema": SCHEMA_VERSION,
        "generated": data.get("generated"),
        "repo": data.get("repo"),
        "fork_ref": data.get("fork_ref"),
        "fork": {
            "sha": (data.get("fork") or {}).get("sha"),
            "tip_unchanged": bool((data.get("fork") or {}).get("tip_unchanged")),
        },
        "upstream_remote": data.get("upstream_remote"),
        "upstream_branch": data.get("upstream_branch"),
        "upstream": {
            "ref": (data.get("upstream") or {}).get("ref"),
            "sha": (data.get("upstream") or {}).get("sha"),
            "fetched": bool((data.get("upstream") or {}).get("fetched")),
        },
        "merge_base": data.get("merge_base"),
        "upstream_commit_count": data.get("upstream_commit_count"),
        "fork_commit_count": data.get("fork_commit_count"),
        "files_changed": data.get("files_changed"),
        "merged": not described and not data.get("error"),
        "conflicts": described,
        "summary": summary,
        "upstream_commits": data.get("upstream_commits") or [],
        "merge_output": data.get("merge_output") or "",
        "scratch": data.get("scratch"),
        "kept_worktree": bool(data.get("kept_worktree")),
        "error": data.get("error"),
        "exit_code": exit_code(data),
    }
    return json.dumps(document, indent=2, sort_keys=False) + "\n"


def exit_code(data: dict) -> int:
    """0 clean, 1 conflicts, 2 the run failed."""
    if data.get("error"):
        return 2
    return 1 if data.get("conflicts") else 0


class GitError(RuntimeError):
    """A git command that failed, with the message git printed."""


def git(arguments: list[str], cwd: str | None = None, check: bool = True) -> str:
    """Run git and return its output, raising GitError with git's own message on failure."""
    process = subprocess.run(
        ["git", *arguments],
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if check and process.returncode != 0:
        message = (process.stderr or process.stdout or "").strip()
        raise GitError(f"git {' '.join(arguments)} failed: {message}")
    return process.stdout


def repo_root(path: str) -> str:
    """The absolute top level of the repository holding `path`."""
    root = git(["rev-parse", "--show-toplevel"], cwd=path).strip()
    if not root:
        raise GitError(f"{path} is not inside a git repository")
    return os.path.abspath(root)


def inside(root: str, path: str) -> bool:
    """True when `path` is `root` itself or below it. Used to keep the scratch worktree in the
    repository's own gitignored folder and to keep `--report` there if it is a relative path."""
    root = os.path.abspath(root)
    path = os.path.abspath(path)
    return path == root or path.startswith(root + os.sep)


def registered_worktrees(root: str) -> list[str]:
    """The worktrees git knows about, so a stale one from an earlier run can be cleaned up."""
    paths = []
    for line in git(["worktree", "list", "--porcelain"], cwd=root).splitlines():
        if line.startswith("worktree "):
            paths.append(os.path.abspath(line[len("worktree "):].strip()))
    return paths


def remove_scratch(root: str, scratch: str) -> None:
    """Take the throwaway worktree down: remove it from git, prune the record and delete the
    folder. Called from the finally block, so it also runs when the merge itself failed."""
    if scratch in registered_worktrees(root):
        git(["worktree", "remove", "--force", scratch], cwd=root, check=False)
    git(["worktree", "prune"], cwd=root, check=False)
    if os.path.isdir(scratch):
        shutil.rmtree(scratch, ignore_errors=True)
    parent = os.path.dirname(scratch)
    while inside(root, parent) and os.path.abspath(parent) != os.path.abspath(root):
        if os.path.isdir(parent) and os.listdir(parent):
            break
        try:
            os.rmdir(parent)
        except OSError:
            break
        parent = os.path.dirname(parent)


def count_hunks(worktree: str, path: str) -> tuple[int, bool]:
    """Conflict hunks in a conflicted file, and whether it is binary. Git writes one conflict
    block per hunk, so the count is the number of `<<<<<<<` markers in the working tree file. A
    binary or a file one side deleted has no markers, and counts as one hunk."""
    full = os.path.join(worktree, path.replace("/", os.sep))
    if not os.path.isfile(full):
        return 1, False
    try:
        with open(full, "rb") as handle:
            data = handle.read()
    except OSError:
        return 1, False
    if b"\x00" in data[:8192]:
        return 1, True
    hunks = sum(1 for line in data.split(b"\n") if line.startswith(b"<<<<<<<"))
    return max(1, hunks), False


def collect_conflicts(worktree: str) -> list[dict]:
    """The unmerged paths of the merge in progress, with their hunks and their state. Uses
    `--diff-filter=U`, so a file that merged itself is not reported."""
    conflicts = []
    unmerged = git(["diff", "--name-only", "--diff-filter=U", "-z"], cwd=worktree).split("\x00")
    states = {}
    for line in git(["status", "--porcelain", "-z"], cwd=worktree).split("\x00"):
        if len(line) > 3:
            states[line[3:].strip()] = line[:2]
    for path in [entry.strip() for entry in unmerged if entry.strip()]:
        hunks, binary = count_hunks(worktree, path)
        conflicts.append(
            {
                "path": normalise(path),
                "hunks": hunks,
                "status": states.get(normalise(path), "UU"),
                "binary": binary,
            }
        )
    return conflicts


def rehearse(args: argparse.Namespace) -> dict:
    """Fetch, count, merge in a throwaway worktree and collect what the merge says. Everything it
    touches is removed again before it returns, except with --keep-worktree."""
    root = repo_root(args.repo)
    scratch = os.path.abspath(args.scratch) if os.path.isabs(args.scratch) else os.path.join(
        root, args.scratch
    )
    relative = os.path.relpath(scratch, root).replace("\\", "/")
    if not inside(root, scratch) or relative == os.curdir or \
            relative.split("/")[0] != SCRATCH_FOLDER:
        raise GitError(
            f"the throwaway worktree has to be a folder under {SCRATCH_FOLDER}/ inside {root}, "
            f"not '{relative}': the rehearsal only ever writes in the repository's gitignored "
            "scratch folder"
        )
    if scratch in registered_worktrees(root) or os.path.exists(scratch):
        remove_scratch(root, scratch)

    fork_ref = args.branch
    fork_sha = git(["rev-parse", "--verify", f"{fork_ref}^{{commit}}"], cwd=root).strip()
    remote = args.remote
    remote_names = git(["remote"], cwd=root).split()
    if remote not in remote_names:
        raise GitError(
            f"no remote named '{remote}' (remotes here: {', '.join(remote_names) or 'none'}); "
            "add it with `git remote add upstream https://github.com/prusa3d/PrusaSlicer.git`"
        )

    data = {
        "generated": datetime.now().astimezone().strftime("%Y-%m-%d %H:%M:%S %z"),
        "repo": root,
        "fork_ref": fork_ref,
        "fork": {"sha": fork_sha, "tip_unchanged": True},
        "upstream_remote": remote,
        "upstream_branch": args.upstream_branch,
        "upstream": {"ref": "FETCH_HEAD", "sha": "", "fetched": False},
        "merge_base": "",
        "upstream_commit_count": 0,
        "fork_commit_count": 0,
        "files_changed": 0,
        "upstream_commits": [],
        "conflicts": [],
        "scratch": relative,
        "kept_worktree": bool(args.keep_worktree),
        "error": None,
    }

    if not args.no_fetch:
        git(["fetch", "--no-tags", remote, args.upstream_branch], cwd=root)
        data["upstream"]["fetched"] = True
    upstream_sha = git(["rev-parse", "--verify", "FETCH_HEAD^{commit}"], cwd=root).strip()

    data["upstream"]["sha"] = upstream_sha
    merge_base = git(["merge-base", fork_sha, upstream_sha], cwd=root).strip()
    data["merge_base"] = merge_base
    if not merge_base:
        raise GitError(
            f"no merge base between {fork_ref} and {remote}/{args.upstream_branch}: the histories "
            "are unrelated, so a merge rehearsal says nothing"
        )
    data["upstream_commit_count"] = int(
        git(["rev-list", "--count", f"{merge_base}..{upstream_sha}"], cwd=root).strip() or 0
    )
    data["fork_commit_count"] = int(
        git(["rev-list", "--count", f"{merge_base}..{fork_sha}"], cwd=root).strip() or 0
    )
    log = git(
        ["log", "--max-count", str(MAX_LISTED_COMMITS + 1), "--format=%H%x09%s",
         f"{merge_base}..{upstream_sha}"],
        cwd=root,
    )
    commits = []
    for line in log.splitlines():
        sha, _, subject = line.partition("\t")
        if sha:
            commits.append({"sha": sha, "subject": subject})
    data["upstream_commits"] = commits

    os.makedirs(os.path.dirname(scratch) or root, exist_ok=True)
    git(["worktree", "add", "--detach", "--force", scratch, fork_sha], cwd=root)
    try:
        # --no-commit --no-ff: the result is inspected, never committed, and a merge that is
        # already up to date still stops here. A non zero exit is the conflict case, not an error.
        merge = subprocess.run(
            ["git", "merge", "--no-commit", "--no-ff", upstream_sha],
            cwd=scratch,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        data["merge_output"] = merge.stdout.strip()
        data["files_changed"] = len(
            [line for line in git(["diff", "--cached", "--name-only"], cwd=scratch).splitlines()
             if line.strip()]
        )
        data["conflicts"] = collect_conflicts(scratch)
    finally:
        if args.keep_worktree:
            pass
        else:
            git(["merge", "--abort"], cwd=scratch, check=False)
            remove_scratch(root, scratch)

    after = git(["rev-parse", "--verify", f"{fork_ref}^{{commit}}"], cwd=root).strip()
    data["fork"]["tip_unchanged"] = after == fork_sha
    if not data["fork"]["tip_unchanged"]:
        raise GitError(f"the tip of {fork_ref} moved during the rehearsal ({fork_sha} -> {after})")
    return data


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo", default=".", help="any path inside the repository (default .)")
    parser.add_argument("--branch", default="sla/main",
                        help="fork branch to merge upstream into (default sla/main)")
    parser.add_argument("--remote", default="upstream",
                        help="read-only upstream remote to fetch (default upstream)")
    parser.add_argument("--upstream-branch", default="master",
                        help="branch of the upstream remote (default master)")
    parser.add_argument("--scratch", default=os.path.join(SCRATCH_FOLDER, "upstream-rehearsal"),
                        help=f"throwaway worktree, a folder under {SCRATCH_FOLDER}/ inside the "
                             f"repository (default {SCRATCH_FOLDER}/upstream-rehearsal)")
    parser.add_argument("--no-fetch", action="store_true",
                        help="use FETCH_HEAD as it is, without fetching again")
    parser.add_argument("--keep-worktree", action="store_true",
                        help="leave the throwaway worktree in place to look at a conflict")
    parser.add_argument("--report", help="write the Markdown report here (default: stdout)")
    parser.add_argument("--json", dest="json_path", metavar="PATH",
                        help="write the JSON report here, for CI")
    return parser.parse_args(argv)


def write_text(path: str, text: str) -> None:
    folder = os.path.dirname(os.path.abspath(path))
    if folder:
        os.makedirs(folder, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        data = rehearse(args)
    except (GitError, OSError) as error:
        print(f"upstream_rehearsal: {error}", file=sys.stderr)
        if args.json_path:
            failed = {
                "schema": SCHEMA_VERSION,
                "generated": datetime.now().astimezone().strftime("%Y-%m-%d %H:%M:%S %z"),
                "repo": args.repo,
                "fork_ref": args.branch,
                "upstream_remote": args.remote,
                "upstream_branch": args.upstream_branch,
                "error": str(error),
                "exit_code": 2,
            }
            write_text(args.json_path, json.dumps(failed, indent=2) + "\n")
            print(f"wrote {args.json_path}")
        return 2

    report = build_report(data)
    if args.report:
        write_text(args.report, report)
        print(f"wrote {args.report}")
    else:
        print(report)
    if args.json_path:
        write_text(args.json_path, build_json(data))
        print(f"wrote {args.json_path}")

    described = describe_conflicts(data["conflicts"])
    summary = summarise(described)
    upstream = data["upstream"]
    print(f"{data['fork_ref']} {_short(data['fork']['sha'])}, "
          f"{data['upstream_remote']}/{data['upstream_branch']} {_short(upstream['sha'])}, "
          f"merge base {_short(data['merge_base'])}")
    print(f"upstream commits since the merge base: {data['upstream_commit_count']}")
    if described:
        for area in AREAS:
            entry = summary["by_area"][area]
            if entry["files"]:
                print(f"  {area:12s} {entry['files']} file(s), {entry['hunks']} hunk(s), "
                      f"{entry['hotspots']} hotspot")
        print(f"conflicts: {summary['conflict_files']} files, {summary['conflict_hunks']} hunks, "
              f"{summary['hotspot_files']} hotspot file(s)")
    else:
        print("no conflicts")
    return exit_code(data)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))