#!/usr/bin/env python3
"""Run the SLA visual regression renders and compare them with the committed references.

PLAN G3, the CI half of roadmap M6.2d. The renders themselves are the app's (M6.2, `--render-to`)
and the comparison is `visual_diff.py`; this is the glue around them, so a CI job is one command
with one exit code.

    # What a run would do, without rendering anything. Prints the plan, or the reason there is
    # nothing to do. --github-output adds have_references=<true|false> to a GitHub output file.
    python doc/sla-fork/tools/visual_ci.py plan
    python doc/sla-fork/tools/visual_ci.py plan --github-output "$GITHUB_OUTPUT"

    # The run itself, from the repository root. The app needs a GL context even though the render
    # is offscreen, so on a headless runner the command goes through Xvfb.
    xvfb-run -a python doc/sla-fork/tools/visual_ci.py run --app build-default/src/slic3r-app-launcher/slic3r-app-launcher --out visual-out

    # One render of one fixture by hand, to see what the job would do for it.
    python doc/sla-fork/tools/visual_ci.py run --only bracket-prepare --app <path> --out visual-out

What one run does, per entry of the manifest:

1. renders the fixture view offscreen with `--sla-fixture`, `--render-to`, `--render-view` and
   `--render-size`, which writes the PNG and its sidecar JSON and quits with an exit code,
2. runs `visual_diff.py check` on it: the per pixel diff against the committed reference, the
   grayscale diff image, the PLAN 2.1 lightness check on the tokens the render was drawn with and,
   with a probe file, on the rendered pixels,
3. keeps the render, the diff image and the greyscale variant next to each other, and the report
   in Markdown.

The manifest is `doc/sla-fork/visual-regression/manifest.json`, and it is written by whoever makes
the first references (roadmap M6.2a, a `[human]` todo). While it is missing or empty there is
nothing to compare against, so the run says so, writes that in the report and exits 0: a fork
without references yet is not a failing build, and a CI job that burned an hour of app build to
compare against nothing is a waste of minutes that can cost money.

    {
      "size": "1280x960",
      "fixtures": [
        {
          "id": "bracket-prepare",
          "fixture": "tests/data/sla_fixtures/bracket.3mf",
          "view": "prepare",
          "size": "1280x960",
          "reference": "doc/sla-fork/visual-regression/bracket-prepare.png",
          "probes": "doc/sla-fork/visual-regression/bracket-prepare.probes.json"
        }
      ]
    }

`id` names the render and its output files, `fixture` and `view` are what the app is asked for,
`size` overrides the top level one, `reference` is the committed PNG to compare with and `probes`
is the optional probe file that puts the lightness check on the rendered pixels too. Paths are
relative to the repository root.

Exit codes:

| Code | Meaning |
| --- | --- |
| 0 | every render matched its reference, or there are no references yet and nothing was compared |
| 1 | a render changed more than the tolerances allow, or a lightness check failed (PLAN 2.1) |
| 2 | the run itself failed: no app, a manifest that is not valid, a reference the manifest names but that is not in the commit, a render that did not produce its PNG, or a missing sidecar |

Only the Python standard library is used, on purpose: this has to run in a bare CI runner next to
the render. The parts that decide something - the manifest, the two command lines, the report and
the exit code - are pure functions of plain data, which is what `test_visual_ci.py` tests with a
fake app and a fake `visual_diff.py`, so none of this needs a build to check.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

# Where the manifest and the references live. The default of --manifest, and the folder the
# suggested layout of doc/sla-fork/visual-regression.md points at.
MANIFEST_RELPATH = "doc/sla-fork/visual-regression/manifest.json"

# The two views --render-view takes. Checked here rather than discovered from the app, so a typo in
# a manifest entry is a message before an app run rather than an exit code 1 from it.
VIEWS = ["prepare", "preview"]

# The default render size, the app's own default, so an entry that says nothing gets what a person
# running the command by hand gets.
DEFAULT_SIZE = "1280x960"

# How long one app run may take before it is called a failure. The app has a five minute backstop
# of its own for the slice of the fixture (DesktopApp.cpp), and this is the whole run around it:
# start up, the slice, the render and writing the PNG.
DEFAULT_RENDER_TIMEOUT = 900

# How a render's verdict is written in the report's table. `passed` and `changed` are the two
# verdicts the comparison gives; the rest say the run could not answer the question, which is a
# different thing from "the picture changed" and has its own exit code.
STATUS_LABELS = {
    "passed": "passed",
    "changed": "CHANGED",
    "render_failed": "render failed",
    "error": "error",
}


class ManifestError(Exception):
    """The manifest is missing, is not JSON, or says something that cannot be rendered."""


@dataclass
class Render:
    """One (fixture, view) pair to render, compare, and name the output files after."""

    id: str
    fixture: str
    view: str
    size: str
    reference: str
    probes: str | None = None

    def render_path(self, out_dir: Path) -> Path:
        return out_dir / f"{self.id}.png"

    def sidecar_path(self, out_dir: Path) -> Path:
        return out_dir / f"{self.id}.png.json"

    def diff_path(self, out_dir: Path) -> Path:
        return out_dir / f"{self.id}-diff.png"

    def gray_path(self, out_dir: Path) -> Path:
        return out_dir / f"{self.id}-gray.png"


def parse_manifest(text: str, default_size: str = DEFAULT_SIZE) -> list[Render]:
    """The renders a manifest describes, validated, or ManifestError saying what is wrong with it.

    A manifest with no `fixtures` (or an empty list) is a valid manifest with nothing in it: that is
    the state before roadmap M6.2a, and it is a skip rather than an error.
    """
    try:
        document = json.loads(text)
    except json.JSONDecodeError as error:
        raise ManifestError(f"not valid JSON: {error}") from error
    if not isinstance(document, dict):
        raise ManifestError("the manifest is not a JSON object")

    size = document.get("size", default_size)
    if not isinstance(size, str) or not is_render_size(size):
        raise ManifestError(f"'size' is not a WIDTHxHEIGHT in 1..8192: {size!r}")

    entries = document.get("fixtures", [])
    if not isinstance(entries, list):
        raise ManifestError("'fixtures' is not a list of renders")

    renders = []
    for position, entry in enumerate(entries):
        where = f"fixtures[{position}]"
        if not isinstance(entry, dict):
            raise ManifestError(f"{where} is not a JSON object")
        identifier = entry.get("id")
        if not isinstance(identifier, str) or not identifier.strip():
            raise ManifestError(f"{where} has no 'id'")
        if "/" in identifier or "\\" in identifier:
            # The id names the output files, so a path in it would write outside the output folder.
            raise ManifestError(f"{where} 'id' is a path, not a name: {identifier!r}")
        view = entry.get("view")
        if view not in VIEWS:
            raise ManifestError(f"{where} 'view' is {view!r}, expected one of {', '.join(VIEWS)}")
        entry_size = entry.get("size", size)
        if not isinstance(entry_size, str) or not is_render_size(entry_size):
            raise ManifestError(f"{where} 'size' is not a WIDTHxHEIGHT in 1..8192: {entry_size!r}")
        for key in ("fixture", "reference"):
            if not isinstance(entry.get(key), str) or not entry[key].strip():
                raise ManifestError(f"{where} has no '{key}'")
        probes = entry.get("probes")
        if probes is not None and (not isinstance(probes, str) or not probes.strip()):
            raise ManifestError(f"{where} 'probes' is not a path")
        renders.append(
            Render(
                id=identifier,
                fixture=entry["fixture"],
                view=view,
                size=entry_size,
                reference=entry["reference"],
                probes=probes,
            )
        )
    return renders


def is_render_size(size: str) -> bool:
    """Whether a string is the WIDTHxHEIGHT the app's --render-size accepts."""
    parts = size.split("x")
    if len(parts) != 2:
        return False
    for part in parts:
        if not part.isdigit():
            return False
        if not 1 <= int(part) <= 8192:
            return False
    return True


def read_manifest(path: Path, default_size: str = DEFAULT_SIZE) -> list[Render]:
    """The manifest at `path`, or ManifestError saying that there is none."""
    if not path.is_file():
        raise ManifestError(f"{path} is not in this commit")
    return parse_manifest(path.read_text(encoding="utf-8"), default_size)


def render_command(app: str, render: Render, out_dir: Path) -> list[str]:
    """The app command line that renders one entry, as argv. The flags are the app's own."""
    return [
        app,
        "--sla-fixture", render.fixture,
        "--render-to", str(render.render_path(out_dir)),
        "--render-view", render.view,
        "--render-size", render.size,
    ]


def compare_command(python: str, tool: str, render: Render, out_dir: Path) -> list[str]:
    """The visual_diff.py command line for one entry: the compare, the lightness check on the
    tokens and (with a probe file) on the pixels, the diff image and the greyscale variant."""
    command = [
        python, tool, "check",
        "--render", str(render.render_path(out_dir)),
        "--reference", render.reference,
        "--sidecar", str(render.sidecar_path(out_dir)),
        "--diff-out", str(render.diff_path(out_dir)),
        "--grayscale", str(render.gray_path(out_dir)),
    ]
    if render.probes:
        command += ["--probes", render.probes]
    return command


def check_inputs(repo: Path, renders: list[Render]) -> list[str]:
    """What is missing for a planned run, as one message per line. Empty means the plan is runnable.

    A missing fixture or probe file is a manifest that does not describe this commit. A missing
    reference is the one that matters most: the manifest says a picture is the reference and the
    commit does not have it, which is a regression rather than a manifest to fix.
    """
    problems = []
    for render in renders:
        for label, relative in (("fixture", render.fixture), ("reference", render.reference)):
            if not (repo / relative).is_file():
                problems.append(f"{render.id}: the {label} {relative} is not in this commit")
        if render.probes and not (repo / render.probes).is_file():
            problems.append(f"{render.id}: the probe file {render.probes} is not in this commit")
    return problems


def plan_lines(renders: list[Render], manifest: str) -> list[str]:
    """The plan of a run, one render per line, for the CI log and for a person reading it."""
    lines = [f"manifest: {manifest}", f"renders: {len(renders)}"]
    for render in renders:
        probes = f", probes {render.probes}" if render.probes else ""
        lines.append(
            f"  {render.id}: {render.view} of {render.fixture} at {render.size}, "
            f"against {render.reference}{probes}"
        )
    return lines


NO_REFERENCES_MESSAGE = (
    "No committed visual regression references, so nothing was rendered and nothing was "
    "compared. The first references are a person's job: render the fixtures with --render-to and "
    "commit the PNGs, their sidecars and the probe files, then list them in "
    f"{MANIFEST_RELPATH} (roadmap M6.2a, doc/sla-fork/visual-regression.md). This run is not a "
    "failure, it is a fork that has nothing to compare against yet."
)


def build_report(results: list[dict], meta: dict) -> str:
    """The Markdown report: a table of the verdicts, then what visual_diff.py said per render.

    The skipped case gets its own report, because a run that compared nothing is otherwise
    indistinguishable from a run that never happened in a job summary.
    """
    lines = ["# Visual regression (roadmap M6.2d)", ""]
    for key, label in (("commit", "commit"), ("app", "app"), ("manifest", "manifest"),
                       ("out", "renders in")):
        if meta.get(key):
            lines.append(f"- {label}: {meta[key]}")
    lines.append("")

    if meta.get("skipped"):
        lines += [f"**{NO_REFERENCES_MESSAGE}**", ""]
    elif meta.get("error"):
        lines += [f"**The run could not be made: {meta['error']}**", ""]

    if results:
        lines += ["| Render | View | Verdict |", "| --- | --- | --- |"]
        for result in results:
            lines.append(
                f"| `{result['id']}` | {result['view']} | {STATUS_LABELS[result['status']]}"
                + (f" ({result['detail']})" if result.get("detail") else "")
                + " |"
            )
        lines.append("")
        for result in results:
            if not result.get("output"):
                continue
            lines += [f"## {result['id']}", "", "```", result["output"].rstrip(), "```", ""]

    if meta.get("generated"):
        lines += [f"_{meta['generated']}_", ""]
    return "\n".join(lines)


def exit_code(results: list[dict], error: str | None = None) -> int:
    """0 when every render passed (or there was nothing to compare), 1 for a failed comparison,
    2 for a run that could not be made. The order matters: a run that broke is 2 even when a
    comparison failed too, because a broken run's verdicts are not evidence."""
    if error:
        return 2
    if any(result["status"] in ("render_failed", "error") for result in results):
        return 2
    if any(result["status"] == "changed" for result in results):
        return 1
    return 0


def subprocess_runner(argv: list[str], timeout: int) -> tuple[int, str]:
    """Run one command line and return (exit code, everything it printed).

    The output is captured rather than streamed, so the report can quote what a render said and
    what the comparison said without the CI log having to be read for it. A command that outlives
    the timeout is killed and reported as a failure, so one render that hangs costs its own minutes
    and not the whole job.
    """
    try:
        completed = subprocess.run(
            argv,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            text=True,
            errors="replace",
            check=False,
        )
    except subprocess.TimeoutExpired as expired:
        return 124, (expired.output or "") + f"\n{argv[0]} did not finish within {timeout} s."
    except OSError as failure:
        return 127, f"{argv[0]} could not be run: {failure}"
    return completed.returncode, completed.stdout or ""


def run(
    renders: list[Render],
    app: str,
    out_dir: Path,
    tool: str,
    python: str = sys.executable,
    render_timeout: int = DEFAULT_RENDER_TIMEOUT,
    runner=subprocess_runner,
    manifest: str = MANIFEST_RELPATH,
    commit: str | None = None,
) -> tuple[int, list[dict], str]:
    """Render every entry, compare every render, and return (exit code, results, report).

    `runner` is how a command line is run, so the whole loop can be driven by a fake app and a
    fake visual_diff.py in the tests. One failing render does not stop the others: the whole point
    of a run is to say which of them changed.
    """
    out_dir.mkdir(parents=True, exist_ok=True)
    results: list[dict] = []
    for render in renders:
        render.render_path(out_dir).unlink(missing_ok=True)
        render.sidecar_path(out_dir).unlink(missing_ok=True)

        code, output = runner(render_command(app, render, out_dir), render_timeout)
        log = [f"$ {' '.join(render_command(app, render, out_dir))}", output.rstrip()]
        if code != 0:
            results.append({
                "id": render.id, "view": render.view, "status": "render_failed",
                "detail": f"the app exited {code}", "output": "\n".join(log),
            })
            continue
        if not render.render_path(out_dir).is_file():
            results.append({
                "id": render.id, "view": render.view, "status": "render_failed",
                "detail": f"the app exited 0 but wrote no {render.render_path(out_dir).name}",
                "output": "\n".join(log),
            })
            continue
        if not render.sidecar_path(out_dir).is_file():
            # The lightness check reads the tokens out of the sidecar, so without it the run would
            # be the per pixel diff alone and would pass a palette that collapses two roles.
            results.append({
                "id": render.id, "view": render.view, "status": "error",
                "detail": f"the app wrote no sidecar {render.sidecar_path(out_dir).name}",
                "output": "\n".join(log),
            })
            continue

        command = compare_command(python, tool, render, out_dir)
        code, output = runner(command, render_timeout)
        log.append(f"$ {' '.join(command)}")
        log.append(output.rstrip())
        if code == 0:
            results.append({
                "id": render.id, "view": render.view, "status": "passed", "detail": "",
                "output": "\n".join(log),
            })
        elif code == 1:
            detail = first_failing_line(output)
            results.append({
                "id": render.id, "view": render.view, "status": "changed",
                "detail": detail, "output": "\n".join(log),
            })
        else:
            results.append({
                "id": render.id, "view": render.view, "status": "error",
                "detail": f"visual_diff.py exited {code}", "output": "\n".join(log),
            })

    report = build_report(results, {
        "commit": commit,
        "app": app,
        "manifest": manifest,
        "out": str(out_dir),
        "generated": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC"),
    })
    return exit_code(results), results, report


def first_failing_line(output: str) -> str:
    """The line of a visual_diff.py run that says why it failed, for the report's table.

    A `TOO CLOSE` lightness pair first, because PLAN 2.1 rejecting a palette is the more specific
    thing to say when both happened; then the `changed:` line of a picture that moved more than the
    tolerances allow, and the FAILED/PASSED verdict as the fallback.
    """
    for line in output.splitlines():
        if "TOO CLOSE" in line:
            return line.strip()
    for line in output.splitlines():
        stripped = line.strip()
        if stripped.startswith("changed:"):
            return stripped
    for line in reversed(output.splitlines()):
        if line.strip() in ("FAILED", "PASSED"):
            return line.strip()
    return "the comparison failed"


def write_github_output(path: str, values: dict[str, str]) -> None:
    """Add key=value lines to a GitHub output file, for the steps that follow a plan."""
    with open(path, "a", encoding="utf-8") as handle:
        for key, value in values.items():
            handle.write(f"{key}={value}\n")


def skipped(manifest_path: Path, detail: str, args: argparse.Namespace) -> int:
    """Say that there is nothing to compare, record it for the steps that follow, and pass.

    This is what a fork without references does: the plan step writes have_references=false into the
    GitHub output file, the build and render steps that read it are skipped, and the run is green
    with the reason in its log. Exit code 0, because a missing reference is not a broken picture.
    """
    print(NO_REFERENCES_MESSAGE)
    print(detail)
    output = getattr(args, "github_output", None)
    if output:
        write_github_output(output, {"have_references": "false", "render_count": "0"})
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    subparsers = parser.add_subparsers(dest="command", required=True)

    def add_shared(sub: argparse.ArgumentParser) -> None:
        sub.add_argument("--repo", default=".", help="repository root (default: .)")
        sub.add_argument("--manifest", help=f"manifest path (default: {MANIFEST_RELPATH})")

    plan = subparsers.add_parser("plan", help="print what a run would do, rendering nothing")
    add_shared(plan)
    plan.add_argument("--size", default=DEFAULT_SIZE, help="default render size for entries "
                                                         f"that name none (default {DEFAULT_SIZE})")
    plan.add_argument("--github-output", help="append key=value lines to this GitHub output file "
                                              "(have_references, render_count)")

    run_parser = subparsers.add_parser("run", help="render the fixtures and compare them")
    add_shared(run_parser)
    run_parser.add_argument("--app", required=True, help="the app binary that renders the fixtures")
    run_parser.add_argument("--out", default="visual-out", help="folder for the renders, the diff "
                                                               "images and the greyscale variants "
                                                               "(default: visual-out)")
    run_parser.add_argument("--report", default="visual-regression.md",
                            help="Markdown report to write (default: visual-regression.md)")
    run_parser.add_argument("--only", action="append", default=[],
                            help="only this render id, repeatable")
    run_parser.add_argument("--render-timeout", type=int, default=DEFAULT_RENDER_TIMEOUT,
                            help=f"seconds one render may take (default {DEFAULT_RENDER_TIMEOUT})")
    run_parser.add_argument("--commit", help="the commit the run is for, for the report")

    args = parser.parse_args(argv)
    repo = Path(args.repo).resolve()
    manifest_path = Path(args.manifest) if args.manifest else repo / MANIFEST_RELPATH
    tool = str(Path(__file__).resolve().parent / "visual_diff.py")

    renders: list[Render] | None = None
    problem = None
    try:
        renders = read_manifest(manifest_path)
    except ManifestError as failure:
        problem = str(failure)
    if renders is None:
        # A manifest that is not in the commit is the state before the first references (M6.2a), not
        # a broken plan and not a failed run: say so and stop before an hour of app build. A
        # manifest that is there and cannot be read is a mistake, and `run` says so.
        if problem and manifest_path.is_file() and args.command != "plan":
            print(f"Error: {problem}", file=sys.stderr)
            return 2
        return skipped(manifest_path, f"no manifest: {manifest_path}", args)
    if not renders:
        return skipped(manifest_path, f"no renders: {manifest_path} has no entries", args)

    if args.command == "plan":
        for line in plan_lines(renders, str(manifest_path.relative_to(repo)
                                            if manifest_path.is_relative_to(repo) else manifest_path)):
            print(line)
        problems = check_inputs(repo, renders)
        for problem in problems:
            print(f"missing: {problem}")
        if args.github_output:
            write_github_output(args.github_output, {
                "have_references": "true" if not problems else "false",
                "render_count": str(len(renders)),
            })
        return 2 if problems else 0

    renders = [render for render in renders if not args.only or render.id in args.only]
    if args.only and not renders:
        print(f"Error: no render in the manifest has the id {' or '.join(args.only)}.",
              file=sys.stderr)
        return 2
    problems = check_inputs(repo, renders)
    if problems:
        for problem in problems:
            print(f"Error: {problem}", file=sys.stderr)
        print("Nothing was rendered: the manifest does not describe this commit.", file=sys.stderr)
        return 2

    app = Path(args.app)
    if not app.is_file():
        print(f"Error: {app} is not a file. The renders are made by the app (M6.2), so the run "
              f"needs a built slic3r-app-launcher.", file=sys.stderr)
        return 2
    if not Path(tool).is_file():
        print(f"Error: {tool} is not there, the comparison cannot run.", file=sys.stderr)
        return 2

    code, results, report = run(
        renders,
        str(app),
        Path(args.out).resolve(),
        tool,
        render_timeout=args.render_timeout,
        manifest=str(manifest_path),
        commit=args.commit,
    )
    Path(args.report).write_text(report, encoding="utf-8")
    print(report)
    print(f"exit code: {code} (0 passed or nothing to compare, 1 changed, 2 the run could not be "
          f"made), report in {args.report}")
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
