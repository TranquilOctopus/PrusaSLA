# Support calibration (M7.4c)

Aggregate measurements of expert-supported models from the maintainer's local reference library,
under the M7 research-only rules: files stay in place, only per-category numbers are committed.
Method: `tools/support-research/calibrate.py` (M7.3e shell split: the model is the largest shell of
the supported file, every other shell is a support; the plain model is registered against the model
shell; contact tips are support-shell vertices within 0.5 mm of the model shell).

## Heads (5 pairs, studio supports authored in Lychee; 2026-10-05)

| Measure | Range | Median |
|---|---|---|
| Model height | 8.4–9.7 mm | 8.7 mm |
| Registration (plain model to model shell) | inlier fraction 1.00, RMS 0.053–0.062 mm | |
| Contact tips per head | 17–24 | 20 |
| Tip diameter (median per head) | 0.25–0.33 mm | 0.32 mm |
| Tips by class (all 5 heads) | 0.1: 2 · 0.2: 45 · 0.3: 38 · 0.4: 15 · 0.6: 0 | |
| Support structures standing on the plate | 9–16 per head | 13 |
| Tilt of the modelling up axis in print orientation | 40.5–60.0° | 49.4° |
| Largest flat area facing the plate | 0 of 5 | |

Our generator, same category, for comparison (default config, model as loaded, M4.3c local run on
the benchmark heads bm01/bm13 of similar size): 4 and 9 points. The per-pair comparison in the expert
orientation comes from `sla_calibration_support_tests` and is still to be run. Those two
numbers are the ones from before M7.8.7, which makes the density of a small part follow
the size of the part, so they are to be measured again with it in.

### What it says

- **Density:** on miniature heads the experts place about 20 supports; the generator places about a
  fifth of that. The generator's density for small parts needs raising (rulebook R7.1 stays: spacing
  does not depend on resin, printer or layer height).
- **Tip sizes:** 0.2 and 0.3 mm dominate, with a few 0.4 mm anchors and almost no 0.1 mm, which
  matches the rulebook's classes (R3, R4.1–R4.6).
- **Orientation:** heads are leaned 40–60° (median about 49°), never flat on their largest flat
  area. The Miniature orient goal (M2.36) leans 48° by default, which is this median rounded down
  (M2.36b).

Other categories (bodies, arms, weapons, bases, figures) are measured when their pairs are local; the
library has more pairs online-only, fetched only for a run and released afterwards.
