# Derived Support Rules from M7 Research Dataset

**Source:** 13 model pairs from Factory Fortress Trench Crusade miniatures, supported in Lychee Slicer by the studio. Aggregate statistics only; no per-model data committed.

**Registration quality note:** 12/13 pairs have inlier fraction < 0.9 (mean 0.776). The 24-candidate registration search helps but real pairs with remeshing rarely reach 0.9 inlier. All reported statistics use the best recovered transform.

---

## Rule 1: Contact Tip Diameter Clusters Around 8.9 mm

**Evidence:** 122 contacts across 13 models. Mean 8.44 mm, median 8.91 mm, p95 8.99 mm, std 1.47 mm. 75% of contacts fall in a tight 8.7–9.0 mm band.

**Confidence:** High (tight distribution, n=122)

**Interpretation:** Lychee's default support tip preset produces ~9 mm diameter contacts regardless of model scale or feature size. This is significantly larger than the ~1.2 mm tips in our synthetic test (0.6 mm radius cone). For PrusaSLA, we should consider offering smaller tip options for detailed models.

---

## Rule 2: Near-Zero Penetration

**Evidence:** Mean penetration 0.036 mm, median 0.000 mm, p95 0.000 mm. Only a few contacts show any measurable penetration.

**Confidence:** High (n=122, consistent near-zero)

**Interpretation:** Expert supports are placed to just touch the surface without piercing. PrusaSLA generator should target zero penetration with a small positive contact_gap tolerance.

---

## Rule 3: Contacts Dominantly on Near-Vertical Surfaces (Overhang 75–105°)

**Evidence:** Mean overhang 94.1°, median 93.8°, std 36.0°. 70% of contacts fall in 60–105° bins. Only 3 contacts below 30°.

**Confidence:** High (n=122)

**Interpretation:** Studio supports primarily address vertical sidewalls of figures, not horizontal overhangs. This matches the "figure" category dominance (tall thin parts). PrusaSLA should prioritize sidewall support density for figurines.

---

## Rule 4: Local Minimum Coverage Extremely Low (1.6%)

**Evidence:** Only 2 of 122 contacts serve a local minimum (bottom of a pocket/cavity).

**Confidence:** High (n=122)

**Interpretation:** These models have few deep concavities. Supports are placed on vertical walls to prevent sway, not to prop up isolated islands. Island detection remains important but local-minimum detection is secondary for this dataset.

---

## Rule 5: No Strong Diameter–Overhang Correlation

**Evidence:** Binned mean diameters: 0–15°: 8.94, 15–30°: 8.89, 30–45°: 8.38, 45–60°: 8.04, 60–75°: 8.79, 75–90°: 8.52 mm. All within 1 mm of each other; no monotonic trend.

**Confidence:** Medium (n=122 but only 1–19 per bin)

**Interpretation:** Tip size is preset-driven, not adapted to local geometry. PrusaSLA could improve by varying tip diameter with overhang angle (larger tips for steeper overhangs that carry more load).

---

## Rule 6: No Strong Diameter–Height Correlation

**Evidence:** Binned mean diameters: 0–5 mm: 8.69, 5–10 mm: 8.17, 10–15 mm: 8.62, 15–20 mm: 8.51, 20–25 mm: 8.54 mm. Flat profile.

**Confidence:** Medium (n=122)

**Interpretation:** Tip size does not scale with leverage (height above plate). Taller supports carry more moment but use same tip size. PrusaSLA should consider larger tips higher up.

---

## Rule 7: Category Differences in Contact Height and Count

| Category | Contacts | Mean Diameter | Mean Overhang | Mean Height |
|----------|----------|---------------|---------------|-------------|
| weapon | 61 | 8.28 mm | 96.0° | 10.1 mm |
| figure body | 29 | 8.44 mm | 92.4° | 12.1 mm |
| arm | 14 | 8.71 mm | 88.6° | 3.6 mm |
| small detail (head) | 13 | 8.75 mm | 92.0° | 2.8 mm |
| mechanical part | 5 | 8.80 mm | 102.6° | 5.0 mm |

**Confidence:** Medium (uneven sample sizes: weapons dominate with 61 contacts)

**Interpretation:** Weapons and bodies have tall supports (10–12 mm); heads and arms are short (2.8–3.6 mm). Contact count scales with model size. Tip diameter similar across categories.

---

## Rule 8: Support Structures Usually Touch the Build Plate

**Evidence:** 85% of support components touch the plate. Mean 2.1 components per pair.

**Confidence:** Medium (n=13 pairs)

**Interpretation:** Lychee defaults to plate-anchored supports. PrusaSLA should maintain this default but allow floating supports for internal cavities.

---

## Rule 9: Contacts Per Pair Scales with Model Size

**Evidence:** Contacts/pair: mean 9.4, median 7.0, range 2–20. Small heads: 2–4 contacts. Weapons: 12–20 contacts. Large bodies: 13–16 contacts.

**Confidence:** Medium (n=13)

**Interpretation:** Support density is not constant; it adapts to model complexity. PrusaSLA should use adaptive density based on surface area and feature count.

---

## Summary of Recommended Generator Changes

| Priority | Rule | Suggested Action |
|----------|------|------------------|
| High | 1, 2 | Add configurable tip diameter preset (default ~1.2 mm for detail, ~9 mm for heavy); enforce zero penetration |
| High | 3 | Increase sidewall support density for figurines; add vertical-wall detection |
| Medium | 4 | Keep island detection primary; local-minimum detection secondary |
| Medium | 5 | Make tip diameter scale with overhang angle (larger for steeper) |
| Medium | 6 | Make tip diameter scale with height above plate |
| Low | 7 | Category-aware presets (figure/weapon/head) |
| Low | 8 | Default to plate-anchored; option for floating |
| Low | 9 | Adaptive density by surface area and curvature |

---

## Limitations

- **Dataset bias:** 61/122 contacts from weapons (tall, thin), only 13 from heads. Figure bodies (29 contacts) are medium height.
- **Registration uncertainty:** 12/13 pairs below 0.9 inlier; contact positions have ~0.5–0.9 mm RMS error.
- **Single source:** All supports from one studio (Factory Fortress) in one slicer (Lychee). Not generalizable to all support styles.
- **No print validation:** Print outcomes unknown for most models.
- **Curvature not computed:** Skipped for speed; curvature-based rules unavailable.

---

## Next Steps (M7.5+)

1. **M7.5 Rulebook review:** Merge with expert interview rules (M7.1).
2. **M7.6 Scorecard:** Score current PrusaSLA generator against these aggregates.
3. **M7.8+ Implementation:** Address high-priority rules first.