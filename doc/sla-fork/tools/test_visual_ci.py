#!/usr/bin/env python3
"""Unit tests for the visual regression CI glue (roadmap M6.2d).

    python doc/sla-fork/tools/test_visual_ci.py
    python -m unittest discover -s doc/sla-fork/tools -p "test_visual_ci.py"

Nothing here builds or renders anything: the app and `visual_diff.py` are fakes that record the
command line they were given and answer with an exit code, and the renders they "write" are empty
files. What is under test is everything around them - the manifest, the two command lines, the
loop, the report and the exit code - plus the case that matters most for the job: a fork with no
references yet skips the comparison and passes, instead of failing a build that has nothing to
compare against.

The awkward manifests are the point: a view that does not exist, a path as an id, a size over
8192, an entry with no reference, a reference the commit does not have, a render that exits 1, a
render that exits 0 and writes no PNG, and a render whose sidecar is missing.
"""
from __future__ import annotations

import contextlib
import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from visual_ci import (  # noqa: E402  (the path has to be set first)
    DEFAULT_SIZE,
    NO_REFERENCES_MESSAGE,
    ManifestError,
    Render,
    build_report,
    check_inputs,
    compare_command,
    exit_code,
    first_failing_line,
    is_render_size,
    main,
    parse_manifest,
    plan_lines,
    read_manifest,
    render_command,
    run,
    write_github_output,
)

MANIFEST = {
    "size": "1024x768",
    "fixtures": [
        {
            "id": "bracket-prepare",
            "fixture": "tests/data/sla_fixtures/bracket.3mf",
            "view": "prepare",
            "reference": "doc/sla-fork/visual-regression/bracket-prepare.png",
            "probes": "doc/sla-fork/visual-regression/bracket-prepare.probes.json",
        },
        {
            "id": "bracket-preview",
            "fixture": "tests/data/sla_fixtures/bracket.3mf",
            "view": "preview",
            "size": "640x480",
            "reference": "doc/sla-fork/visual-regression/bracket-preview.png",
        },
    ],
}

FAKE_COMPARE_OK = "== compare ==\nchanged: 12 (0.0098 %, tolerance 0.5 %)\nPASSED"
FAKE_COMPARE_CHANGED = ("== compare ==\nchanged: 19040 (1.5486 %, tolerance 0.5 %)\n"
                        "FAILED")
FAKE_COMPARE_TOO_CLOSE = ("== compare ==\nchanged: 0 (0.0000 %, tolerance 0.5 %)\n"
                          "== lightness ==\n  dL* pad             supports        4.21 "
                          "(threshold 10.0) TOO CLOSE\nFAILED")


def manifest_text(**overrides) -> str:
    document = dict(MANIFEST)
    document.update(overrides)
    return json.dumps(document)


class ManifestTests(unittest.TestCase):
    def test_a_manifest_is_read_into_renders(self) -> None:
        renders = parse_manifest(manifest_text())
        self.assertEqual([render.id for render in renders],
                         ["bracket-prepare", "bracket-preview"])
        self.assertEqual(renders[0].view, "prepare")
        self.assertEqual(renders[0].size, "1024x768")
        self.assertEqual(renders[0].probes,
                         "doc/sla-fork/visual-regression/bracket-prepare.probes.json")
        self.assertIsNone(renders[1].probes)

    def test_an_entry_size_wins_over_the_top_level_one(self) -> None:
        self.assertEqual(parse_manifest(manifest_text())[1].size, "640x480")

    def test_a_manifest_without_fixtures_is_empty_and_not_an_error(self) -> None:
        # The state before roadmap M6.2a: valid, and nothing to do.
        self.assertEqual(parse_manifest("{}"), [])
        self.assertEqual(parse_manifest('{"fixtures": []}'), [])

    def test_a_manifest_that_is_not_json_is_refused(self) -> None:
        with self.assertRaises(ManifestError):
            parse_manifest("fixtures: [")

    def test_a_manifest_that_is_not_an_object_is_refused(self) -> None:
        with self.assertRaises(ManifestError):
            parse_manifest('["bracket-prepare"]')

    def test_fixtures_that_is_not_a_list_is_refused(self) -> None:
        with self.assertRaises(ManifestError):
            parse_manifest('{"fixtures": {"id": "bracket"}}')

    def test_an_entry_that_is_not_an_object_is_refused(self) -> None:
        with self.assertRaises(ManifestError):
            parse_manifest('{"fixtures": ["bracket-prepare"]}')

    def test_a_missing_id_is_refused(self) -> None:
        with self.assertRaises(ManifestError) as caught:
            parse_manifest('{"fixtures": [{"fixture": "a.3mf", "view": "prepare",'
                           ' "reference": "a.png"}]}')
        self.assertIn("id", str(caught.exception))

    def test_a_path_as_an_id_is_refused(self) -> None:
        # The id names the output files, so a path in it would write outside the output folder.
        for identifier in ["../escape", "sub/dir", "windows\\path"]:
            with self.assertRaises(ManifestError):
                parse_manifest(json.dumps({"fixtures": [
                    {"id": identifier, "fixture": "a.3mf", "view": "prepare",
                     "reference": "a.png"}]}))

    def test_a_view_that_does_not_exist_is_refused(self) -> None:
        for view in ["", "Prepare", "gcode", "window", None]:
            with self.assertRaises(ManifestError) as caught:
                parse_manifest(json.dumps({"fixtures": [
                    {"id": "a", "fixture": "a.3mf", "view": view, "reference": "a.png"}]}))
            self.assertIn("view", str(caught.exception))

    def test_a_missing_fixture_or_reference_is_refused(self) -> None:
        for key in ("fixture", "reference"):
            entry = {"id": "a", "fixture": "a.3mf", "view": "prepare", "reference": "a.png"}
            del entry[key]
            with self.assertRaises(ManifestError) as caught:
                parse_manifest(json.dumps({"fixtures": [entry]}))
            self.assertIn(key, str(caught.exception))

    def test_a_size_outside_1_to_8192_is_refused(self) -> None:
        for size in ["0x960", "1280x0", "16384x960", "1280X960", "1280", "12a0x960", 7]:
            with self.assertRaises(ManifestError) as caught:
                parse_manifest(json.dumps({"size": size, "fixtures": []}))
            self.assertIn("size", str(caught.exception))

    def test_an_entry_size_is_checked_too(self) -> None:
        with self.assertRaises(ManifestError) as caught:
            parse_manifest(json.dumps({"fixtures": [
                {"id": "a", "fixture": "a.3mf", "view": "prepare", "size": "0x0",
                 "reference": "a.png"}]}))
        self.assertIn("size", str(caught.exception))

    def test_a_render_size_is_what_the_app_accepts(self) -> None:
        for size in ["1x1", "1280x960", "8192x8192"]:
            self.assertTrue(is_render_size(size), size)
        for size in ["", "1280", "1280x", "x960", "0x0", "8193x10", "1280x960x2", "-1x1"]:
            self.assertFalse(is_render_size(size), size)

    def test_the_default_size_is_the_apps_own(self) -> None:
        self.assertEqual(DEFAULT_SIZE, "1280x960")
        self.assertEqual(parse_manifest('{"fixtures": [{"id": "a", "fixture": "a.3mf",'
                                        ' "view": "prepare", "reference": "a.png"}]}')[0].size,
                         "1280x960")


class CommandTests(unittest.TestCase):
    def setUp(self) -> None:
        self.renders = parse_manifest(manifest_text())
        self.out = Path("visual-out")

    def test_the_render_command_is_the_apps_own_flags(self) -> None:
        command = render_command("/opt/app/slic3r-app-launcher", self.renders[0], self.out)
        self.assertEqual(command[0], "/opt/app/slic3r-app-launcher")
        self.assertEqual(command[1:3], ["--sla-fixture", "tests/data/sla_fixtures/bracket.3mf"])
        self.assertEqual(command[3], "--render-to")
        self.assertEqual(Path(command[4]), self.out / "bracket-prepare.png")
        self.assertEqual(command[5:], ["--render-view", "prepare", "--render-size", "1024x768"])

    def test_the_render_command_carries_the_entry_size(self) -> None:
        command = render_command("app", self.renders[1], self.out)
        self.assertEqual(command[-1], "640x480")
        self.assertEqual(Path(command[4]), self.out / "bracket-preview.png")
        self.assertIn("preview", command)

    def test_the_compare_command_is_a_check_with_one_exit_code(self) -> None:
        command = compare_command("python3", "visual_diff.py", self.renders[0], self.out)
        self.assertEqual(command[:3], ["python3", "visual_diff.py", "check"])
        self.assertEqual(command[command.index("--reference") + 1],
                         "doc/sla-fork/visual-regression/bracket-prepare.png")
        self.assertEqual(Path(command[command.index("--render") + 1]),
                         self.out / "bracket-prepare.png")
        self.assertEqual(Path(command[command.index("--sidecar") + 1]),
                         self.out / "bracket-prepare.png.json")
        self.assertEqual(Path(command[command.index("--diff-out") + 1]),
                         self.out / "bracket-prepare-diff.png")
        self.assertEqual(Path(command[command.index("--grayscale") + 1]),
                         self.out / "bracket-prepare-gray.png")
        self.assertEqual(command[command.index("--probes") + 1],
                         "doc/sla-fork/visual-regression/bracket-prepare.probes.json")

    def test_an_entry_without_probes_asks_for_no_probe_check(self) -> None:
        self.assertNotIn("--probes", compare_command("python3", "v.py", self.renders[1], self.out))

    def test_output_files_are_named_after_the_id(self) -> None:
        render = Render("a", "a.3mf", "prepare", "1280x960", "a.png")
        self.assertEqual(render.render_path(self.out).name, "a.png")
        self.assertEqual(render.sidecar_path(self.out).name, "a.png.json")
        self.assertEqual(render.diff_path(self.out).name, "a-diff.png")
        self.assertEqual(render.gray_path(self.out).name, "a-gray.png")


class FakeRunner:
    """A fake app and a fake visual_diff.py: it records the command lines and answers from two
    tables of render id to (exit code, output), one for the app run and one for the comparison. A
    render that answers 0 writes the files the app writes, unless `writes` says otherwise."""

    def __init__(self, renders: dict | None = None, compares: dict | None = None,
                 writes: dict | None = None):
        self.renders = renders or {}
        self.compares = compares or {}
        self.writes = writes or {}
        self.commands: list[list[str]] = []

    def __call__(self, argv: list[str], timeout: int) -> tuple[int, str]:
        self.commands.append(list(argv))
        is_render = "--render-to" in argv
        flag = "--render-to" if is_render else "--render"
        path = Path(argv[argv.index(flag) + 1])
        identifier = path.stem
        code, output = (self.renders if is_render else self.compares).get(identifier, (0, ""))
        if is_render and code == 0:
            for name in self.writes.get(identifier, ["<id>.png", "<id>.png.json"]):
                (path.parent / name.replace("<id>", identifier)).write_bytes(b"")
        return code, output


class RunTests(unittest.TestCase):
    def setUp(self) -> None:
        self.renders = parse_manifest(manifest_text())
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.out = Path(self.tmp.name) / "visual-out"

    def run_with(self, runner) -> tuple[int, list[dict], str]:
        return run(self.renders, "app", self.out, "visual_diff.py", runner=runner)

    def test_everything_passing_is_exit_code_zero(self) -> None:
        runner = FakeRunner(compares={"bracket-prepare": (0, FAKE_COMPARE_OK),
                                      "bracket-preview": (0, FAKE_COMPARE_OK)})
        code, results, report = self.run_with(runner)
        self.assertEqual(code, 0)
        self.assertEqual([result["status"] for result in results], ["passed", "passed"])
        self.assertIn("| `bracket-prepare` | prepare | passed |", report)
        self.assertIn("| `bracket-preview` | preview | passed |", report)

    def test_a_changed_picture_is_exit_code_one_and_says_how_much(self) -> None:
        runner = FakeRunner(compares={"bracket-preview": (1, FAKE_COMPARE_CHANGED)})
        code, results, report = self.run_with(runner)
        self.assertEqual(code, 1)
        self.assertEqual(results[1]["status"], "changed")
        self.assertIn("1.5486", results[1]["detail"])
        self.assertIn("CHANGED", report)

    def test_a_lightness_pair_the_palette_rule_rejects_is_exit_code_one(self) -> None:
        runner = FakeRunner(compares={"bracket-prepare": (1, FAKE_COMPARE_TOO_CLOSE)})
        code, results, _report = self.run_with(runner)
        self.assertEqual(code, 1)
        self.assertEqual(results[0]["status"], "changed")
        self.assertIn("TOO CLOSE", results[0]["detail"])

    def test_one_failing_render_does_not_stop_the_others(self) -> None:
        runner = FakeRunner(compares={"bracket-prepare": (1, FAKE_COMPARE_CHANGED)})
        _code, results, _report = self.run_with(runner)
        self.assertEqual([result["status"] for result in results], ["changed", "passed"])

    def test_an_app_that_exits_nonzero_is_exit_code_two(self) -> None:
        # A render that refused is not a changed picture: the exit code 1 the app uses says the
        # render could not be made, and the run itself is what failed.
        runner = FakeRunner(renders={"bracket-prepare": (1, "Error: the fixture rendered nothing.")})
        code, results, _report = self.run_with(runner)
        self.assertEqual(code, 2)
        self.assertEqual(results[0]["status"], "render_failed")
        self.assertIn("exited 1", results[0]["detail"])

    def test_an_app_that_writes_no_png_is_exit_code_two(self) -> None:
        runner = FakeRunner(writes={"bracket-prepare": []})
        code, results, _report = self.run_with(runner)
        self.assertEqual(code, 2)
        self.assertEqual(results[0]["status"], "render_failed")
        self.assertIn("wrote no", results[0]["detail"])

    def test_a_missing_sidecar_is_an_error_not_a_pass(self) -> None:
        # Without the sidecar the lightness check cannot run, and a run that only did the per pixel
        # diff would pass a palette that collapses two roles.
        runner = FakeRunner(writes={"bracket-prepare": ["<id>.png"]})
        code, results, _report = self.run_with(runner)
        self.assertEqual(code, 2)
        self.assertEqual(results[0]["status"], "error")
        self.assertIn("sidecar", results[0]["detail"])

    def test_a_comparison_tool_that_broke_is_exit_code_two(self) -> None:
        runner = FakeRunner(compares={"bracket-prepare": (2, "a.png: not a PNG file")})
        code, results, _report = self.run_with(runner)
        self.assertEqual(code, 2)
        self.assertEqual(results[0]["status"], "error")
        self.assertIn("visual_diff.py exited 2", results[0]["detail"])

    def test_a_broken_run_outranks_a_changed_picture(self) -> None:
        results = [{"id": "a", "view": "prepare", "status": "changed"},
                   {"id": "b", "view": "preview", "status": "render_failed"}]
        self.assertEqual(exit_code(results), 2)

    def test_the_command_lines_are_the_ones_the_run_was_asked_for(self) -> None:
        runner = FakeRunner()
        self.run_with(runner)
        renders = [command for command in runner.commands if "--render-to" in command]
        compares = [command for command in runner.commands if "check" in command]
        self.assertEqual(len(renders), 2)
        self.assertEqual(len(compares), 2)
        self.assertEqual(renders[0], render_command("app", self.renders[0], self.out))
        self.assertEqual(compares[0], compare_command(sys.executable, "visual_diff.py",
                                                     self.renders[0], self.out))
        self.assertEqual(renders[1], render_command("app", self.renders[1], self.out))

    def test_the_report_quotes_both_command_lines(self) -> None:
        runner = FakeRunner(compares={"bracket-preview": (1, FAKE_COMPARE_CHANGED)})
        _code, _results, report = self.run_with(runner)
        self.assertIn("--render-view preview", report)
        self.assertIn("visual_diff.py check", report)
        self.assertIn("1.5486", report)

    def test_a_stale_render_of_an_earlier_run_is_removed_first(self) -> None:
        # A render that fails after a previous run left a PNG there must not be compared against
        # the old one, so the old files go before the app is asked.
        self.out.mkdir(parents=True, exist_ok=True)
        (self.out / "bracket-prepare.png").write_bytes(b"stale")
        (self.out / "bracket-prepare.png.json").write_bytes(b"stale")
        runner = FakeRunner(renders={"bracket-prepare": (1, "boom")})
        run(self.renders[:1], "app", self.out, "visual_diff.py", runner=runner)
        self.assertFalse((self.out / "bracket-prepare.png").exists())


class ReportTests(unittest.TestCase):
    def test_the_skip_report_says_why_and_names_the_todo(self) -> None:
        report = build_report([], {"skipped": True, "manifest": "doc/x/manifest.json"})
        self.assertIn("# Visual regression (roadmap M6.2d)", report)
        self.assertIn(NO_REFERENCES_MESSAGE, report)
        self.assertIn("M6.2a", report)
        self.assertIn("not a failure", report)

    def test_the_report_lists_the_verdicts_in_a_table(self) -> None:
        results = [
            {"id": "a", "view": "prepare", "status": "passed", "detail": "", "output": "ok"},
            {"id": "b", "view": "preview", "status": "changed", "detail": "changed: 1.5 %",
             "output": "FAILED"},
        ]
        report = build_report(results, {"commit": "abc1234", "app": "app"})
        self.assertIn("| `a` | prepare | passed |", report)
        self.assertIn("| `b` | preview | CHANGED (changed: 1.5 %) |", report)
        self.assertIn("commit: abc1234", report)

    def test_an_error_report_says_what_the_run_could_not_do(self) -> None:
        report = build_report([], {"error": "the app is not a file"})
        self.assertIn("The run could not be made", report)
        self.assertIn("the app is not a file", report)

    def test_the_failing_line_is_the_one_that_says_why(self) -> None:
        self.assertIn("1.5486", first_failing_line(FAKE_COMPARE_CHANGED))
        self.assertIn("TOO CLOSE", first_failing_line(FAKE_COMPARE_TOO_CLOSE))
        self.assertEqual(first_failing_line("something else\nFAILED"), "FAILED")
        self.assertEqual(first_failing_line("nothing useful"), "the comparison failed")

    def test_exit_code_rules(self) -> None:
        self.assertEqual(exit_code([]), 0)
        self.assertEqual(exit_code([{"status": "passed"}]), 0)
        self.assertEqual(exit_code([{"status": "changed"}]), 1)
        self.assertEqual(exit_code([{"status": "render_failed"}]), 2)
        self.assertEqual(exit_code([{"status": "error"}]), 2)
        self.assertEqual(exit_code([{"status": "changed"}], error="no app"), 2)


class PlanAndInputTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.repo = Path(self.tmp.name)

    def cli(self, argv: list[str]) -> tuple[int, str]:
        """main() with its printing kept out of the test output, so a failing test is the only
        thing on the screen. Returns the exit code and what the command printed."""
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer), contextlib.redirect_stderr(buffer):
            code = main(argv)
        return code, buffer.getvalue()

    def write(self, relative: str, text: str = "x") -> Path:
        path = self.repo / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def test_a_missing_manifest_is_a_skip_with_a_message_and_not_a_failure(self) -> None:
        output = self.tmp.name + "/gh_output"
        code, printed = self.cli(["plan", "--repo", str(self.repo), "--github-output", output])
        self.assertEqual(code, 0)
        self.assertIn("have_references=false", Path(output).read_text())
        self.assertIn("render_count=0", Path(output).read_text())
        self.assertIn("No committed visual regression references", printed)

    def test_a_manifest_with_no_entries_is_a_skip_too(self) -> None:
        self.write("doc/sla-fork/visual-regression/manifest.json", '{"fixtures": []}')
        output = self.tmp.name + "/gh_output"
        code, _printed = self.cli(["plan", "--repo", str(self.repo), "--github-output", output])
        self.assertEqual(code, 0)
        self.assertIn("have_references=false", Path(output).read_text())

    def test_a_run_without_a_manifest_says_there_is_nothing_to_do_and_passes(self) -> None:
        # The case the CI job depends on: a fork with no references yet renders nothing, compares
        # nothing, says why, and does not fail.
        code, printed = self.cli(["run", "--repo", str(self.repo), "--app", "app",
                                  "--out", self.tmp.name + "/out"])
        self.assertEqual(code, 0)
        self.assertIn("nothing was rendered and nothing was compared", printed)
        self.assertIn("M6.2a", printed)
        self.assertFalse(Path(self.tmp.name + "/out").exists())

    def test_a_manifest_that_is_not_valid_json_is_a_skip_in_plan_and_an_error_in_a_run(self) -> None:
        self.write("doc/sla-fork/visual-regression/manifest.json", "not json at all")
        self.assertEqual(self.cli(["plan", "--repo", str(self.repo)])[0], 0)
        self.assertEqual(self.cli(["run", "--repo", str(self.repo), "--app", "app",
                                   "--out", self.tmp.name + "/out"])[0], 2)

    def test_a_full_plan_names_every_render_and_its_probes(self) -> None:
        self.write("doc/sla-fork/visual-regression/manifest.json", manifest_text())
        self.write("tests/data/sla_fixtures/bracket.3mf")
        self.write("doc/sla-fork/visual-regression/bracket-prepare.png")
        self.write("doc/sla-fork/visual-regression/bracket-prepare.probes.json")
        self.write("doc/sla-fork/visual-regression/bracket-preview.png")
        output = self.tmp.name + "/gh_output"
        code, _printed = self.cli(["plan", "--repo", str(self.repo), "--github-output", output])
        self.assertEqual(code, 0)
        written = Path(output).read_text()
        self.assertIn("have_references=true", written)
        self.assertIn("render_count=2", written)
        lines = plan_lines(parse_manifest(manifest_text()), "manifest.json")
        self.assertIn("renders: 2", lines[1])
        self.assertIn("bracket-prepare.probes.json", lines[2])
        self.assertNotIn(".probes.json", lines[3])

    def test_a_reference_the_commit_does_not_have_is_a_missing_input(self) -> None:
        # The manifest says a picture is the reference and the commit has none: that is a
        # regression, and the plan says so instead of quietly comparing against nothing.
        renders = parse_manifest(manifest_text())
        problems = check_inputs(self.repo, renders)
        # Two renders, a fixture, a reference and one probe file, none of them in the empty repo.
        self.assertEqual(len(problems), 5)
        self.assertIn("the fixture", problems[0])
        self.assertIn("the reference", problems[1])
        self.assertIn("the probe file", problems[2])

    def test_a_run_with_a_missing_reference_refuses_before_the_app(self) -> None:
        self.write("doc/sla-fork/visual-regression/manifest.json", manifest_text())
        self.write("tests/data/sla_fixtures/bracket.3mf")
        code, _printed = self.cli(["run", "--repo", str(self.repo), "--app", "app",
                                   "--out", self.tmp.name + "/out",
                                   "--report", self.tmp.name + "/report.md"])
        self.assertEqual(code, 2)

    def test_a_run_without_an_app_is_an_error_and_writes_a_report(self) -> None:
        self.write("doc/sla-fork/visual-regression/manifest.json", manifest_text())
        self.write("tests/data/sla_fixtures/bracket.3mf")
        for name in ("bracket-prepare", "bracket-preview"):
            self.write(f"doc/sla-fork/visual-regression/{name}.png")
        self.write("doc/sla-fork/visual-regression/bracket-prepare.probes.json")
        report = self.tmp.name + "/report.md"
        # No --app path at all: argparse refuses it, and no render is attempted.
        with self.assertRaises(SystemExit):
            self.cli(["run", "--repo", str(self.repo), "--out", self.tmp.name + "/out"])
        # An --app that is not a file: the run says so and stops before the first render.
        self.assertEqual(self.cli(["run", "--repo", str(self.repo), "--app", self.tmp.name + "/nope",
                                   "--out", self.tmp.name + "/out", "--report", report])[0], 2)
        # An app that is there but cannot be run: the renders are reported as failed, not passed.
        app = self.write("app", "#!/bin/sh\n")
        self.assertEqual(self.cli(["run", "--repo", str(self.repo), "--app", str(app),
                                   "--out", self.tmp.name + "/out", "--report", report])[0], 2)
        self.assertIn("render failed", Path(report).read_text())

    def test_only_selects_one_render_and_says_when_the_id_is_unknown(self) -> None:
        self.write("doc/sla-fork/visual-regression/manifest.json", manifest_text())
        self.write("tests/data/sla_fixtures/bracket.3mf")
        self.write("doc/sla-fork/visual-regression/bracket-prepare.png")
        app = self.write("app", "#!/bin/sh\n")
        report = self.tmp.name + "/report.md"
        self.assertEqual(self.cli(["run", "--repo", str(self.repo), "--app", str(app),
                                   "--only", "bracket-prepare", "--out", self.tmp.name + "/out",
                                   "--report", report])[0], 2)
        self.assertEqual(self.cli(["run", "--repo", str(self.repo), "--app", str(app),
                                   "--only", "no-such-render", "--out", self.tmp.name + "/out",
                                   "--report", report])[0], 2)

    def test_a_manifest_at_another_path_is_read_from_there(self) -> None:
        manifest = self.write("elsewhere/references.json", manifest_text())
        renders = read_manifest(manifest)
        self.assertEqual(len(renders), 2)
        with self.assertRaises(ManifestError):
            read_manifest(self.repo / "not-there.json")


class GithubOutputTests(unittest.TestCase):
    def test_the_output_file_gains_a_line_per_value(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "output"
            write_github_output(str(path), {"have_references": "true", "render_count": "3"})
            write_github_output(str(path), {"have_references": "false"})
            self.assertEqual(path.read_text().splitlines(),
                             ["have_references=true", "render_count=3", "have_references=false"])


if __name__ == "__main__":
    unittest.main()
