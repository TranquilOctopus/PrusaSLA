# Orientation Test Piece

A printable calibration model to verify SLA printer image orientation (mirroring and rotation).

## Generate the STL

```bash
python doc/sla-fork/tools/orientation_test_piece.py [output_path]
```

Default output: `orientation_test_piece.stl` in the current directory.

## How to Use

1. **Load the STL** into the slicer.
2. **Place it in the middle of the build plate**, unrotated (0°).
3. **Slice and print it flat without supports** — the model is designed to print directly on the plate.
4. **Inspect the printed part** as it sits on the build plate (the side that was on the plate stays facing down).

## Reading the Result

When looking down at the printed part on the build plate:

- The **letter "F"** must read correctly (stem on the left, arms pointing right).
- The **square boss** must be at the **back right** corner.
- The **bar** must be at the **front right**, covering the right half of the front edge.

### Diagnosing Issues

| Observation | Cause | Fix |
|-------------|-------|-----|
| "F" is mirrored left–right | `display_mirror_x` wrong | Toggle `display_mirror_x` |
| "F" is mirrored front–back | `display_mirror_y` wrong | Toggle `display_mirror_y` |
| "F" is rotated 90° | Display orientation wrong | Rotate display 90° in firmware or swap X/Y in slicer |
| "F" is rotated 180° | Both mirrors wrong | Toggle both `display_mirror_x` and `display_mirror_y` |

The model is asymmetric in all four quadrants, so any mirror or 90° rotation is immediately obvious.