# Expert supporting rules (M7.1 interview)

Rules from the maintainer, an experienced resin printer of 28–32 mm miniatures, collected in chat
for ROADMAP M7.1. One numbered rule per practice. They feed the miniature orient goal (M2.36), the
auto-support presets (M2.37), and later the rulebook (M7.5) and the scorecard (M7.6).

Status: in progress (first answers 2026-10-05). Sections without a rule are still to be asked.

## Process order

1. **Orient for the front first.** Rotate the part until the most detailed side, the "front" of the
   miniature, gets no supports and no support marks at all. Orientation is chosen for where the marks
   land, before anything else.
2. **Auto supports, then a review by hand.** Start from auto supports and review them; do not place
   every support by hand.
3. **Review every support that touches a thin feature.** Any support that touches a tip, a point or
   another thin, fragile feature gets its tip reduced to the minimum size, so removing it does not
   break the feature off.
4. **Review supports on small surface detail.** Supports that land on a rivet or similar small detail
   are removed, or their tip size is changed, so the detail is not scarred.

## Tip sizes

5. **Smallest tip on fragile and detailed contacts** (see rules 3 and 4). The exact diameters per
   use are still to be asked.
6. **A heavy support where the part is glued.** On heads, helmets and busts: one fat support at the
   neck, where the mark is hidden by gluing the part onto the body (given earlier in chat, 2026-10-02).

## Density

7. **Methodology does not depend on resin, printer or layer height.** The same supporting is used
   regardless of resin, printer or layer height. Spacing values are still to be asked.

## Orientation

8. **Heads, helmets and busts face up.** The face of a helmet or bust points upwards to keep as much
   detail as possible, with the glue joint (neck) towards the plate (2026-10-02). See rules 1 and 6.

## Surfaces never to support

(to be asked)

## Structure and base style

(to be asked)

## Common auto-support failures

(to be asked)

## Rules from the references (paraphrased)

From VogMan's beginner guide (first reference below), read from its transcript 2026-10-05. These
are the reference's practices, kept apart from the maintainer's own rules above; where they differ,
the maintainer's rule wins.

- **V1. Every island's lowest point is supported.** A region that starts in mid-air prints onto
  nothing and fails; islands are the main cause of failed prints and are worth hunting from several
  camera angles.
- **V2. No large flat face parallel to the plate.** Unless a flat face is meant to sit on the plate,
  lift the model (about 5 mm) and tilt it so a flat face does not sag between supports.
- **V3. About 45 degrees self-supports.** Faces up to roughly 45 degrees from vertical print on the
  layer below without support; steeper overhangs are weak and sag. (The guide says the limit depends
  on printer and resin; the maintainer does not change methodology for either, rule 7.)
- **V4. Orient on two axes to remove islands and reduce supports.** There are many good poses; pick
  one that also keeps supports off important detail such as the head (agrees with rule 1).
- **V5. Light supports by default; heavier over distance and for weight.** Light tips mark the
  surface least, but long light supports bend while printing, so medium or heavy ones carry long
  spans; a heavier model needs more or heavier supports.
- **V6. The lowest point gets at least one heavy support, on an unimportant spot.** Early in the
  print most of the load hangs on the first support (agrees with rule 6).
- **V7. A few heavy anchors on flat, low-detail areas.** They hold the print firm on the plate; the
  number scales with the model's size (a couple on a small model).
- **V8. Order of work: lowest point, then islands, then the slicer's overhang warnings.**
- **V9. Keep a gap between the model and the support bodies,** so they do not fuse.
- **V10. Join several light tips into one thick trunk:** strong over distance, small marks on the
  model (the branching tree's idea).
- **V11. Over-supporting beats a failed print;** brace or support tall supports on large prints.
- **V12. Always use a raft, enlarged to about 150 %.** Differs from the maintainer: a part printed
  flat on the plate generally needs no raft, and the raft stays a user setting (M4.6b).

## References the maintainer follows

- VogMan, "3D resin print supports [EASY GUIDE]", https://www.youtube.com/watch?v=MU0Cq_bjhy4
  (beginner guide: orient to avoid islands and overhangs, then raft, anchor, support and print).
  Reviewed from its transcript 2026-10-05, rules V1-V12 above.
- "Masterclass Resin Support (MRP) 1", https://www.youtube.com/watch?v=Qgv_hGNzGOI. Content review
  pending.
