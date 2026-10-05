# Support Research Tools

Offline analysis scripts for M7.3 (contact extraction) and M7.4c (calibration against
the expert supports). Not part of the application build.

## Setup

```bash
cd tools/support-research
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
```

## Running Tests

```bash
# From repo root
.venv\Scripts\python -m pytest tools/support-research -q

# Or from tools/support-research
.venv\Scripts\python -m pytest -q
```

## Modules

- `synth.py` — synthetic asymmetric test shape with procedurally placed supports
- `register.py` — rigid registration (coarse PCA alignment + trimmed point-to-plane ICP)
- `separate.py` — support vs model faces, with a scaled variant for big meshes
- `shells.py` — M7.3e: the shell split of a supported file, and the contacts on the support shells
- `contacts.py` — one record per support contact
- `calibrate.py` — M7.4c: expert support count, orientation and model size per pair
- `calibration_report.py` — M7.4c: aggregate tables by category from the two sources
- `test_m73a.py`, `test_m73b.py`, `test_m73c.py`, `test_geometry.py` — synthetic tests
- `test_m74c.py` — synthetic tests of the calibration measurements (numpy only)
- `test_m73e.py` — synthetic tests of the shell split and the contacts on it

## The M7.3e shell split

A supported file is a triangle soup of disjoint bodies: the model, and every support,
tree or raft of its own. `shells.py` welds the vertices and labels the faces by
adjacency, so the scene comes apart into shells with no registration and no epsilon;
the model is the shell with the most faces. With `tools/support-research` on the path:

```python
from calibrate import load_mesh
from shells import measure_shells, split_supported_scene

scene = load_mesh(path)                      # a supported file
model_shell, support_shells = split_supported_scene(scene)
print(measure_shells(model_shell, support_shells, np.random.default_rng(7)))
```

`calibrate.py` does this itself: the plain model is registered against the model shell
alone (registering it against the whole scene fails, the supports dominate the scene),
and the contacts come off the support shells in place, so no registration error is left
in a tip diameter. Its `--epsilon` option is gone with the distance method.

## The M7.4c calibration

Three steps, all local, all writing into the gitignored
`local-samples/supports/out/`:

```bash
# 1. What the expert did: tip count, structures, orientation, model size.
#    Also writes out/oriented/<id>.stl, the plain model in the expert orientation.
python tools/support-research/calibrate.py [--ids cal001,cal002] [--category head]

# 2. What we do with the same models, as loaded and in the expert orientation.
set SLA_CALIB_MANIFEST=local-samples\supports\manifest_calib.yaml
build-default\tests\sla_print\Release\sla_print_tests.exe "[.local]" > calibration.log

# 3. The tables that get committed, by category only.
python tools/support-research/calibration_report.py --log calibration.log > calibration.md
```

`calibrate.py --clean` removes the oriented STLs. Exit codes: 0 everything measured,
1 at least one pair failed, 2 the manifest is unusable.

The research-only rules of ROADMAP M7 apply to all three: the meshes stay where they
are, no mesh is ever opened by an agent, the console gets ids and counts only, and
only the aggregate tables of step 3 are ever committed.

## Constraints

- Synthetic tests only; no access to `local-samples/supports/manifest.yaml` or real dataset files
- Research-only rules apply: never read Trench Crusade STL files, print only aggregate numbers, never commit dataset-derived data
- Memory-conscious: runs on 8 GB machine with dependency build possibly running
