# Peel force per layer (M4.9b)

The slicer estimates, for every layer, how hard the vat film has to be peeled off that layer.
The number is shown in the layer image window (the "Peel force (N, <film>)" chart and the
`Peel ... N` line of text) and is stored per layer in `SLAResultData::layer_peel_force`.

**The number is an estimate from a model, not a measurement of your printer.** It is there to
rank the layers of a print against each other and to point at the ones worth looking at. Do not
quote it as a force, and do not use it to predict whether a print will delaminate. The
coefficients are order-of-magnitude defaults taken from the published range of SLA peel force
measurements; nothing in this repository has ever measured a peel force on a printer, so the
defaults are not calibrated to any machine in particular.

## The model

```
F_peel(layer) = k_area      * cured_area_mm2(layer)
              + k_perimeter * perimeter_mm(layer)
              + k_suction   * cup_opening_area_mm2(layer)
```

- **k_area** (N/mm²) is the force the film needs to let go of an area of cured resin.
- **k_perimeter** (N/mm) is the force per millimetre of boundary. The film is lifted off the part
  along its whole outline, so a thin comb of the same area as a solid block costs more to peel:
  the same 100 mm² is 40 mm of boundary as a 10x10 mm square and about 220 mm as ten 1 mm wide
  teeth. This is the term that makes sparse layers more expensive than solid ones, which is what
  the literature reports for peeling generally (the force grows with the length of the peel line).
- **k_suction** (N/mm²) is the extra force a suction cup (PLAN B5b, `SLA/CavityDetection.*`,
  M4.8e) adds: a region that is a hole in its layer and stays a hole going up pins a vacuum
  against the film on every peel it is open for. The area counted is the opening of the cup, and
  it is added to every layer from the one holding the opening up to the one below the roof. A
  trapped cavity (closed at both ends) is not a cup and adds nothing: nothing holds the film down
  there.

The area and the perimeter come from the same merged layer polygons (`layer_areas_mm2()` and
`layer_perimeters_mm()`, M4.9), so a model, its supports and its raft are all in the number.

The model is linear, so it says nothing that a per-layer table does not, and it ignores things
that matter in reality: the tilt speed and the lift height of the printer, the exposure time,
the resin, the temperature, the condition of the film and the fact that a real FEP film stretches
and only then gives way. It is a shape with a unit, not a simulation.

## The coefficients

They come from the vat film of the printer, set with `vat_film_type`:

| film | k_area (N/mm²) | k_perimeter (N/mm) | k_suction (N/mm²) | peel_force_warning (N) |
|------|----------------|--------------------|--------------------|------------------------|
| FEP  | 0.10           | 0.15               | 0.30               | 20                     |
| nFEP | 0.08           | 0.12               | 0.25               | 18                     |
| PFA  | 0.06           | 0.10               | 0.20               | 15                     |
| ACF  | 0.02           | 0.04               | 0.12               | 8                      |

FEP is the default because it is what most printers ship with. The order of the table is the
order the films are in for peeling: FEP stretches the most before it lets go, nFEP is a little
softer, PFA is stiff but thin, and ACF (air cushion film) gives way early because it inflates
rather than stretches, so it wants a fraction of the force. A factor of five between FEP and ACF
is the ratio that keeps coming up in practice and in the literature; the individual numbers
inside each film are much less certain.

Where the ranges come from, roughly: peel forces measured on FEP parts in the tens of newtons
for cross sections of order 10 cm², which puts k_area in the range 0.1 to 0.4 N/mm² and, with a
peel line of order 100 mm, k_perimeter in the range 0.1 to 1 N/mm; and reports that ACF needs a
few times less force than FEP. The defaults above sit at the low end of those ranges on purpose:
a model that overestimates a peel force warns about layers that are fine, and a user who has
been trained by that stops reading the warnings.

## Tuning it

Two keys override the area and the perimeter term without touching the film:

- `peel_area_coefficient` (N/mm², 0 = from the film)
- `peel_perimeter_coefficient` (N/mm, 0 = from the film)

To calibrate on your own printer: slice a shape whose widest layer is a known area and whose
outline you know (a 100 x 100 mm raft does it), peel it, and read the peak force of the printer
or of your force gauge. Take the two coefficients from the layers you can account for:

```
k_area      = (F_measured - k_perimeter * perimeter_mm) / area_mm2
k_perimeter = (F_measured - k_area      * area_mm2)     / perimeter_mm
```

Two different layer shapes (one wide and solid, one thin and long) are enough to separate the
two terms; a single measurement only gives their sum.

`peel_force_warning` (N) is the threshold above which a layer is called high. A negative value,
which is its default, takes the number of the film in the table above; zero reports no layer at
all. A layer over the threshold becomes a `SlaIssue` of the kind `HighPeelForce`, and the layer
image window writes the limit next to the force of the layer it shows, so a high layer is visible
in the chart and in the text below it.

## Where the code is

- `SLA/LayerStats.*` in the engine: the coefficients, the settings, the per-layer areas and
  perimeters and the estimate itself. Pure functions, no config reading, so the tests can call
  them with any film and any override.
- `SLAPrintSteps.cpp`, `merge_slices_and_eval_stats()`: reads the four keys, feeds the merged
  layers and the cups of M4.8e to the model, fills `layer_areas`, `layer_peel_force` and the
  `HighPeelForce` issues.
- `SLA/CavityDetection.*`: `cup_suction_area_mm2()`, which spreads the opening of every cup over
  the layers it is open for.
- `App/Preview/SlaLayerImageWindow.*`: the chart with the film in its title and the limit in the
  text line.
- `tests/sla_print/sla_layer_stats_tests.cpp`: a comb of the same area as a square costs more, FEP
  costs more than ACF, the defaults are finite and positive, and a threshold of zero flags
  nothing.
