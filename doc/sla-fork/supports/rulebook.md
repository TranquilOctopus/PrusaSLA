# Support rulebook

The one set of rules the auto supports follow (ROADMAP M7.1 + M7.5). It merges three sources into
general rules with concrete numbers:

- **M**: the maintainer, an experienced resin printer of 28–32 mm miniatures (interview in chat,
  2026-10-02 and 2026-10-05). The maintainer also confirmed using the same sizes as W.
- **V**: VogMan, "3D resin print supports [EASY GUIDE]", https://www.youtube.com/watch?v=MU0Cq_bjhy4
- **W**: Dennis Wang, "Masterclass Resin Support (MRP) 1", https://www.youtube.com/watch?v=Qgv_hGNzGOI

Where the sources disagree, the maintainer's practice wins. Both videos were read from transcripts
the maintainer provided and are paraphrased here. Each rule names the code or todo that implements it.

Accepted by the maintainer 2026-10-05 ("collapse all rules into one general rule document and build
auto-support algorithms based off of that").

## 1. Workflow

- **R1.1 Orient first, then auto support, then review by hand.** Auto supports are a starting point
  that a person reviews; the tool must make that review fast. (M, V, W)
- **R1.2 The same rules for every resin, printer and layer height.** Nothing below changes with the
  material or the machine. (M)
- **R1.3 Supports are one link in the chain.** Calibration (levelling, exposure), orientation,
  hollowing and drain holes decide the print together with the supports. (W)

## 2. Orientation

- **R2.1 The front gets no supports and no support marks.** Choose the pose so the most detailed
  side, the "front" of the miniature, faces away from the plate. (M, V)
  Implemented by: M2.36 (Miniature / bust orient goal).
- **R2.2 Heads, helmets and busts: face up, glue joint down.** The neck cut or other glue face points
  at the plate, the face points up. (M)
  Implemented by: M2.36.
- **R2.3 Tilt instead of printing a large flat face parallel to the plate,** unless that face is
  meant to rest on the plate. About 35 degrees by default. (V)
  Implemented by: M2.36.
- **R2.4 Overhangs up to about 45 degrees from vertical print without support.** Steeper faces need
  support or a better pose. (V)
- **R2.5 Fewer islands is better, but never at the cost of R2.1.** Orient on two axes to remove
  islands; among the many good poses, pick one that keeps supports off important detail. (V, M)

## 3. Support sizes

All sizes are diameters in mm. Supports are named by their tip size, not by weight (W).

| Class | Tip (contact) | Use |
|---|---|---|
| T0.1 | 0.1 | fragile thin features and tiny detail (R4.4, R4.5) |
| T0.2 | 0.2 | stabilising thin parts, detail, small islands |
| T0.3 | 0.3 | small islands, visible surfaces |
| T0.4 | 0.4 | anchors: the lowest point and the heavy anchors of most models |
| T0.6 | 0.6 | anchors of very large objects (filling a mid-size printer's plate) |

- **R3.1 Ball contact, sunk half its diameter.** Contact shape: ball; contact depth = 0.5 × tip. (W)
- **R3.2 A narrow neck just under the contact is the breaking point.** The cone from the contact to
  the stem starts slightly narrower than the contact (about 0.8–0.95 × tip: 0.48 under 0.6, 0.38
  under 0.4) and widens to 0.8 mm, so a support breaks there and leaves the least mark. (W)
- **R3.3 Stem 1.0 mm.** Same diameter top and bottom; 1.2–1.5 mm only on big objects; never above
  1.5 mm. Polygon (prism) cross-section. (W)
- **R3.4 One base for everything:** 6 mm across, 0.3 mm thick, prism shaped. Not changed per object. (W)
- **R3.5 Short supports between two parts of the model are a ball at each end,** each the tip
  diameter and sunk half of it. (W)

Implemented by: M7.8.1 (rulebook sizes as the presets and defaults).

## 4. Where supports go and which size they get

- **R4.1 The lowest point always gets at least one T0.4 anchor,** and the lowest point should be an
  unimportant spot (glue joint, under a foot). Early in the print it carries the whole part. (M, V, W)
  Implemented by: M2.37 (lowest island heavy), M7.8.2.
- **R4.2 A few T0.4 anchors on flat, low-detail areas** facing the plate; about two on a small
  miniature, more with size and weight; T0.6 on very large objects. (V, W)
  Implemented by: M7.8.3.
- **R4.3 Every island's lowest point is supported;** small islands get T0.3, very small ones T0.2. (V, W)
  Implemented by: the generator's island sampling + M7.8.2 (size by role).
- **R4.4 Supports that touch a tip, a point or another thin, fragile feature get the minimum tip,
  T0.1,** so removing them does not snap the feature. (M)
  Implemented by: M7.8.2.
- **R4.5 No support on small surface detail such as a rivet:** move it to plain surface next to the
  detail, or shrink it to T0.1 when it cannot move. (M)
  Implemented by: M7.8.2.
- **R4.6 Light supports by default, heavier for distance and weight.** Overhangs and the rest get
  T0.2–T0.3; long supports and heavy parts get thicker stems or more supports. (V)
  Implemented by: M2.37 (detail preset), M7.8.2.
- **R4.7 Mixed sizes on one model are normal:** anchors, edges, visible surfaces and small islands
  each get their own size (W). The review in R1.1 adjusts them; the tool selects and changes one or
  many at once (M2.33, M2.35, M2.38).
- **R4.8 Order of work: lowest point, then islands, then the overhang warnings.** (V)

## 5. Structure

- **R5.1 Keep a gap between the model and the support bodies** so they do not fuse. (V)
- **R5.2 Join light tips into a thick trunk over distance:** strong trunks, small marks (the branching
  tree). (V)
- **R5.3 Brace tall supports; over-supporting beats a failed print.** (V)
  Per-support bracing: M2.38.

## 6. Raft

W's raft rule, chosen by the maintainer as the more efficient one (2026-10-05). It replaces the
maintainer's earlier "the raft stays a user setting" (M4.6b) and V's "always an enlarged raft".

- **R6.1 No raft by default:** a raft wastes resin. (W)
- **R6.2 A raft where the underside would form a suction cup:** when the bottom of the part would
  close a pocket against the plate or the raft-less first layers (a hollow sole, a cup, a ring), the
  raft goes in, so no suction cup forms. (W)
  Implemented by: M7.8.4 (a raft type "Auto" that is the default: no raft, unless the part's underside
  forms a suction cup). The other raft types stay available for a user who wants one.

## 7. Density

- **R7.1 Spacing does not depend on resin, printer or layer height** (R1.2). The generator's support
  radius curve sets it today; a calibrated spacing waits for the M7.4c measurements of the reference
  library.

## Open questions for the maintainer

- Typical spacing under an overhang (mm).
- Surfaces never to support, beyond the front and small surface detail (eyes, armour edges, the top
  of a base?).
- Lift height and when to brace.
- What auto supports most often get wrong.

## Shortcuts in W's Chitubox workflow (for comparison with M2.28)

Tab cycles the support profile, A adds a support, E edits (moves) one, D then click then D deletes,
and a box selection then D deletes several.
