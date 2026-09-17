# Support Research Tools

Offline analysis scripts for M7.3 (contact extraction). Not part of the application build.

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
- `test_m73a.py` — end-to-end registration tests on synthetic data
- `test_geometry.py` — unit tests for the exact closest-point query

## Constraints

- Synthetic tests only; no access to `local-samples/supports/manifest.yaml` or real dataset files
- Research-only rules apply: never read Trench Crusade STL files, print only aggregate numbers, never commit dataset-derived data
- Memory-conscious: runs on 8 GB machine with dependency build possibly running