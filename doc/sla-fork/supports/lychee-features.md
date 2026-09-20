# Lychee support options: parity target

**Status: the parity target is every Lychee support option, not a personal subset.** Compiled from general knowledge of Lychee Slicer's settings, not from its files or code. This list was written from general knowledge of Lychee Slicer's support settings, not from its files or code (see the M7 research rules in [ROADMAP.md](../ROADMAP.md)). Option names and ranges may be wrong or incomplete. Rows are unverified: names and ranges may be wrong, and options may be missing. Treat a wrong name as a bug to fix when someone checks against the Lychee UI, not as a reason to delay M2.11. Add any option found later.

Goal: every option below has an equivalent in this fork, per support point where Lychee allows it and globally otherwise.

| Group | Lychee option (approximate name) | What it controls | Per point? | Checked |
|---|---|---|---|---|
| Presets | Light / Medium / Heavy (plus custom) | A named bundle of tip, stem and base dimensions | yes | ☐ |
| Presets | Mini / tiny supports | Very small presets for fine detail | yes | ☐ |
| Tip | Contact (tip) diameter | Diameter where the tip touches the model | yes | ☐ |
| Tip | Tip length | Length of the tapered tip section | yes | ☐ |
| Tip | Tip depth / penetration | How far the tip sinks into the model surface | yes | ☐ |
| Tip | Tip shape | Cone, or sphere/ball contact | yes | ☐ |
| Tip | Knot / joint | Optional ball between tip and stem, and its size | yes | ☐ |
| Stem | Stem (body) diameter | Main column diameter | yes | ☐ |
| Stem | **Stem geometry** | Cross-section (round, square, polygon side count) | yes | ☐ |
| Stem | Stem taper | Diameter changes along the stem | yes | ☐ |
| Stem | Support on model | Stems that start on the model instead of the raft | yes | ☐ |
| Stem | Support on support / branching | Stems that join into other stems | global | ☐ |
| Base | Base (foot) diameter | Diameter where the stem meets the raft or plate | yes | ☐ |
| Base | Base height and shape | Cone or cylinder foot, and its height | yes | ☐ |
| Bracing | Braces / cross-braces | Automatic links between stems | global | ☐ |
| Bracing | Brace diameter, spacing, angle, pattern | Brace size and layout (for example zig-zag) | global | ☐ |
| Raft | Raft type | For example standard, skate, grid/honeycomb, none | global | ☐ |
| Raft | Raft thickness, margin/offset, chamfer/slope | Raft shape | global | ☐ |
| Placement | Model elevation (Z lift) | Height of the model above the raft | global | ☐ |
| Placement | Auto-support density and minimum distance | Point spacing for auto-generation | global | ☐ |
| Placement | Overhang angle threshold and island detection | Where auto-generation places points | global | ☐ |

Add any Lychee option missing from this table.
