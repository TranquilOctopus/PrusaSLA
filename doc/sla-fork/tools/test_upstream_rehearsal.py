#!/usr/bin/env python3
"""Unit tests for the upstream merge rehearsal report (roadmap M6.3).

    python doc/sla-fork/tools/test_upstream_rehearsal.py
    python -m unittest discover -s doc/sla-fork/tools -p "test_upstream_rehearsal.py"

Only the pure half of `upstream_rehearsal.py` is exercised: the area grouping, the hotspot list,
the summary, the Markdown report, the JSON and the exit code, all on a made up conflict list. No
git command is run and no merge is rehearsed, so this is safe to run anywhere and needs no
upstream remote.

The fake conflicts are the awkward cases on purpose: a PLAN 3.3 hotspot that both sides edited, a
build file, a preset, a binary file, a file upstream deleted, and a file neither side knows about.
"""
from __future__ import annotations

import json
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from upstream_rehearsal import (  # noqa: E402  (the path has to be set first)
    AREAS,
    build_json,
    build_report,
    classify,
    describe_conflicts,
    exit_code,
    group_by_area,
    hotspot_label,
    is_hotspot,
    status_label,
    summarise,
)

# A conflict list nothing real would produce, with every awkward shape in it.
FAKE_CONFLICTS = [
    {
        "path": "src/libslic3r/include/libslic3r/SLAResult.hpp",
        "hunks": 3,
        "status": "UU",
        "binary": False,
    },
    {
        "path": "CMakeLists.txt",
        "hunks": 1,
        "status": "UU",
        "binary": False,
    },
    {
        "path": "resources/presets/community-sla.ini",
        "hunks": 1,
        "status": "UU",
        "binary": False,
    },
    {
        "path": "src/slic3r-shared/src/Slic3r/App/Gizmos.cpp",
        "hunks": 2,
        "status": "UU",
        "binary": False,
    },
    {
        "path": "resources/gui/sla/icons.zip",
        "hunks": 1,
        "status": "UU",
        "binary": True,
    },
    {
        "path": "doc/sla-fork/ROADMAP.md",
        "hunks": 1,
        "status": "UD",
        "binary": False,
    },
]

FAKE_RUN = {
    "generated": "2026-09-30 09:15:00 +0200",
    "repo": "/home/someone/PrusaSLA",
    "fork_ref": "sla/main",
    "fork": {"sha": "abcdef0123456789", "tip_unchanged": True},
    "upstream_remote": "upstream",
    "upstream_branch": "master",
    "upstream": {"ref": "FETCH_HEAD", "sha": "9876543210fedcba", "fetched": True},
    "merge_base": "0123456789abcdef",
    "upstream_commit_count": 128,
    "fork_commit_count": 4310,
    "files_changed": 96,
    "upstream_commits": [
        {"sha": "aaaaaaaabbbb", "subject": "Slicer: fix the raft orientation"},
        {"sha": "bbbbbbbbcccc", "subject": "GUI: move the sidebar"},
    ],
    "merge_output": "Automatic merge failed; fix conflicts and then commit the result.",
    "conflicts": FAKE_CONFLICTS,
    "scratch": ".agent-scratch/upstream-rehearsal",
    "kept_worktree": False,
    "error": None,
}

CLEAN_RUN = dict(FAKE_RUN, conflicts=[], merge_output="Automatic merge went well.")


class ClassifyTests(unittest.TestCase):
    def test_every_area_has_a_case(self) -> None:
        cases = {
            "src/libslic3r/src/libslic3r/SLA/Tree.cpp": "engine SLA",
            "src/slic3r-domain/src/Slic3r/Domain/ConfigDefsSLA.cpp": "config defs",
            "resources/presets/community-sla.ini": "config defs",
            "src/slic3r-shared/src/Slic3r/App/Plater/Plater.cpp": "App/Biz",
            "CMakeLists.txt": "build",
            "src/slic3r-app-desktop/Slic3rApp.cpp": "other",
        }
        for path, expected in cases.items():
            self.assertEqual(classify(path), expected, path)

    def test_config_wins_over_engine(self) -> None:
        # Upstream keeps its config definitions in libslic3r; a conflict there is a config one.
        self.assertEqual(classify("src/libslic3r/src/libslic3r/ConfigDefsFDM.cpp"), "config defs")

    def test_every_area_is_one_of_the_reported_ones(self) -> None:
        for conflict in FAKE_CONFLICTS:
            self.assertIn(classify(conflict["path"]), AREAS)


class HotspotTests(unittest.TestCase):
    def test_plan_3_3_files_are_hotspots(self) -> None:
        for path in [
            "src/libslic3r/include/libslic3r/SLAResult.hpp",
            "src/libslic3r/include/libslic3r/SLAPrint.hpp",
            "src/slic3r-shared/include/Slic3r/App/Scene/IGizmo.hpp",
            "src/slic3r-shared/src/Slic3r/App/Plater/PlaterRenderModule.cpp",
            "src/slic3r-shared/src/Slic3r/App/Theme.cpp",
            "src/slic3r-domain/src/Slic3r/Domain/ConfigDefsSLA.cpp",
            "src/slic3r-shared/include/Slic3r/Biz/Slicing/SlicingInteractor.hpp",
        ]:
            self.assertTrue(is_hotspot(path), path)
            self.assertIn("PLAN 3.3", hotspot_label(path))

    def test_any_cmakelists_is_a_hotspot(self) -> None:
        self.assertTrue(is_hotspot("src/libslic3r/CMakeLists.txt"))
        self.assertTrue(is_hotspot("CMakeLists.txt"))

    def test_neighbours_of_a_hotspot_are_not(self) -> None:
        for path in [
            "src/slic3r-shared/src/Slic3r/App/Gizmos.cpp",
            "src/libslic3r/src/libslic3r/SLA/Tree.cpp",
            "src/slic3r-domain/src/Slic3r/Domain/ConfigDefsFDM.cpp",
        ]:
            self.assertFalse(is_hotspot(path), path)
            self.assertEqual(hotspot_label(path), "")


class DescribeTests(unittest.TestCase):
    def test_description_adds_area_hotspot_and_words(self) -> None:
        described = {entry["path"]: entry for entry in describe_conflicts(FAKE_CONFLICTS)}
        self.assertEqual(described["src/libslic3r/include/libslic3r/SLAResult.hpp"]["area"],
                         "engine SLA")
        self.assertTrue(described["src/libslic3r/include/libslic3r/SLAResult.hpp"]["hotspot"])
        self.assertEqual(described["CMakeLists.txt"]["hotspot_label"],
                         "PLAN 3.3 (any CMakeLists.txt)")
        self.assertEqual(described["doc/sla-fork/ROADMAP.md"]["status_label"],
                         "modified here, deleted upstream")
        self.assertTrue(described["resources/gui/sla/icons.zip"]["binary"])
        self.assertFalse(described["resources/gui/sla/icons.zip"]["hotspot"])

    def test_grouping_is_in_area_order_regardless_of_input(self) -> None:
        shuffled = list(reversed(FAKE_CONFLICTS))
        groups = group_by_area(describe_conflicts(shuffled))
        self.assertEqual(list(groups), AREAS)
        self.assertEqual([entry["path"] for entry in groups["engine SLA"]],
                         ["src/libslic3r/include/libslic3r/SLAResult.hpp"])
        self.assertEqual([entry["path"] for entry in groups["build"]], ["CMakeLists.txt"])
        self.assertEqual(len(groups["other"]), 2)

    def test_summary_counts_files_hunks_and_hotspots(self) -> None:
        summary = summarise(describe_conflicts(FAKE_CONFLICTS))
        self.assertEqual(summary["conflict_files"], len(FAKE_CONFLICTS))
        self.assertEqual(summary["conflict_hunks"], 9)
        self.assertEqual(summary["hotspot_files"], 2)
        self.assertEqual(summary["by_area"]["engine SLA"],
                         {"files": 1, "hunks": 3, "hotspots": 1})
        self.assertEqual(summary["by_area"]["config defs"],
                         {"files": 1, "hunks": 1, "hotspots": 0})
        self.assertEqual(summary["by_area"]["App/Biz"],
                         {"files": 1, "hunks": 2, "hotspots": 0})
        self.assertEqual(summary["by_area"]["build"],
                         {"files": 1, "hunks": 1, "hotspots": 1})
        self.assertEqual(summary["by_area"]["other"],
                         {"files": 2, "hunks": 2, "hotspots": 0})

    def test_unknown_status_is_reported_as_itself(self) -> None:
        self.assertEqual(status_label("ZZ"), "ZZ")
        self.assertEqual(status_label(""), "unknown")


class ReportTests(unittest.TestCase):
    def test_report_states_the_numbers_of_the_run(self) -> None:
        report = build_report(FAKE_RUN)
        self.assertIn("`sla/main` at `abcdef012345`", report)
        self.assertIn("tip unchanged by this run: yes", report)
        self.assertIn("Upstream commits since the merge base: 128", report)
        self.assertIn("Fork commits since the merge base: 4310", report)
        self.assertIn("Files the merge touched: 96", report)
        self.assertIn("`aaaaaaaabbbb`", report)
        self.assertIn("Slicer: fix the raft orientation", report)

    def test_report_groups_by_area_in_area_order(self) -> None:
        report = build_report(FAKE_RUN)
        headings = [line for line in report.splitlines() if line.startswith("## Conflicts: ")]
        self.assertEqual(headings, [f"## Conflicts: {area}" for area in AREAS])
        # Every area heading is printed even when nothing conflicted in it, with the count in the
        # summary table, so a reader sees the whole picture and not only the busy parts.
        self.assertIn("| App/Biz | 1 | 2 | 0 |", report)
        self.assertIn("| other | 2 | 2 | 0 |", report)

    def test_report_counts_hunks_per_file_and_marks_hotspots(self) -> None:
        report = build_report(FAKE_RUN)
        self.assertIn("| `src/libslic3r/include/libslic3r/SLAResult.hpp` | 3 | both modified | "
                      "PLAN 3.3 hotspot |", report)
        self.assertIn("| `CMakeLists.txt` | 1 | both modified | PLAN 3.3 (any CMakeLists.txt) |",
                      report)
        self.assertIn("| `resources/gui/sla/icons.zip` | 1 | both modified | binary |", report)
        self.assertIn("`src/libslic3r/include/libslic3r/SLAResult.hpp` - 3 hunk(s), both "
                      "modified, PLAN 3.3 hotspot (engine SLA)", report)

    def test_report_of_a_clean_run(self) -> None:
        report = build_report(CLEAN_RUN)
        self.assertIn("No conflicts: `upstream/master` merges into `sla/main` cleanly", report)
        self.assertNotIn("## Conflicts:", report)
        self.assertNotIn("## Hotspot files involved", report)
        self.assertIn("What the merge said", report)
        self.assertIn("no branch was moved and nothing was pushed", report)

    def test_report_of_a_failed_run_says_nothing_about_a_merge(self) -> None:
        report = build_report(dict(CLEAN_RUN, error="no remote named 'upstream'",
                                   conflicts=[], upstream={"ref": "FETCH_HEAD", "sha": "",
                                                           "fetched": False}))
        self.assertIn("## The run failed", report)
        self.assertIn("no remote named 'upstream'", report)
        self.assertNotIn("No conflicts", report)
        self.assertIn("stopped before the merge", report)

    def test_report_says_where_a_kept_worktree_is(self) -> None:
        report = build_report(dict(FAKE_RUN, kept_worktree=True))
        self.assertIn("kept for inspection (--keep-worktree)", report)
        self.assertIn("git worktree remove --force", report)

    def test_report_truncates_a_long_commit_list_but_not_the_count(self) -> None:
        many = [{"sha": f"{index:012x}", "subject": f"commit {index}"} for index in range(25)]
        report = build_report(dict(FAKE_RUN, upstream_commits=many))
        self.assertIn("Upstream commits since the merge base: 128", report)
        self.assertIn("and 5 more", report)


class JsonTests(unittest.TestCase):
    def test_json_is_serialisable_and_carries_the_exit_code(self) -> None:
        document = json.loads(build_json(FAKE_RUN))
        self.assertEqual(document["schema"], 1)
        self.assertEqual(document["exit_code"], 1)
        self.assertFalse(document["merged"])
        self.assertEqual(document["upstream_commit_count"], 128)
        self.assertIsNone(document["error"])
        self.assertEqual(len(document["conflicts"]), len(FAKE_CONFLICTS))
        self.assertEqual(document["summary"]["conflict_hunks"], 9)

    def test_json_of_a_clean_run(self) -> None:
        document = json.loads(build_json(CLEAN_RUN))
        self.assertEqual(document["exit_code"], 0)
        self.assertTrue(document["merged"])
        self.assertEqual(document["conflicts"], [])
        self.assertEqual(document["summary"]["conflict_files"], 0)

    def test_conflicts_carry_their_area_and_hotspot_in_the_json(self) -> None:
        document = json.loads(build_json(FAKE_RUN))
        paths = {entry["path"]: entry for entry in document["conflicts"]}
        self.assertEqual(paths["resources/presets/community-sla.ini"]["area"], "config defs")
        self.assertEqual(paths["src/slic3r-shared/src/Slic3r/App/Gizmos.cpp"]["area"], "App/Biz")
        self.assertTrue(paths["src/libslic3r/include/libslic3r/SLAResult.hpp"]["hotspot"])


class ExitCodeTests(unittest.TestCase):
    def test_zero_clean_one_conflicts_two_failure(self) -> None:
        self.assertEqual(exit_code(CLEAN_RUN), 0)
        self.assertEqual(exit_code(FAKE_RUN), 1)
        self.assertEqual(exit_code(dict(FAKE_RUN, error="git fetch failed")), 2)

    def test_a_failure_wins_over_conflicts(self) -> None:
        # Belt and braces: a run that reported both must not look like a plain conflict.
        self.assertEqual(exit_code(dict(FAKE_RUN, error="boom")), 2)


if __name__ == "__main__":
    unittest.main()