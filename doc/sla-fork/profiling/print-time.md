# Estimated print time (M1.11c)

The sidebar summary shows an estimated print time, and the file formats that have a field for one
write it. Both come from one function, `Domain::sla_estimate_print_time()` in
`src/slic3r-domain/`, so the number the user reads and the number in the file cannot drift apart.

## What the two models are

There are two, and they are not the same machine.

**The SL1 model** is upstream PrusaSlicer's. `SLAPrint::Steps::merge_slices_and_eval_stats()`
accumulates it into `Domain::SLA::PrintStatistics::estimated_print_time`: for a Prusa machine
through `layer_peel_move_time()` and an `ExposureProfile` (the tilt times, the tower microsteps,
the screen refresh delay, the delays around the exposure), and for anything else through the
`fast_tilt_time` / `slow_tilt_time` / `high_viscosity_tilt_time` settings with a handful of
"magical constants" out of the SL1 firmware. It is what the `.sl1` export writes as `printTime`
and what the layer view reads for its per-layer times. It is left exactly as it was.

**The MSLA model** is the one this document is about. A masked printer that peels by lifting the
plate has no tilt and no tower, so none of those terms exist: its layer time is the exposure, the
waits with the light off, and the lift and the retract. That is the function.

| | SL1 | MSLA |
|---|---|---|
| where | `PrintStatistics::estimated_print_time` | `SLAResultData::print_time_s` / `layer_print_times_s` (M1.11c) |
| who reads it | the `.sl1` export, the layer view, the filename | the SLA sidebar summary |
| tilt, tower microsteps, screen refresh | yes | no |
| separation | tilt cycles | lift and retract at their speeds |

Neither is a measurement. Both are estimates, and the MSLA one is only as good as the numbers the
resin profile carries. The `.sl1` writer still writes the SL1 model, so for a Prusa machine the
sidebar figure and the `printTime` in the exported `config.ini` are two different estimates of the
same print. That is left as it is: the SL1 model is the one the firmware was fitted against, and
replacing it is a separate decision.

## The formula

For every layer, counted from the build plate (0 is the first layer on the build plate):

```
layer_time = exposure + light_off + motion
```

and the estimate is the sum over the layers. `SlaLayerTime` keeps the three parts apart, so which
of them a setting moves is visible in the result.

### exposure

| the layer is | exposure in s |
|---|---|
| a burn-in layer, `i < bottom_layer_count` | `initial_exposure_time` |
| inside the raft interface band (M2.14b3) | `raft_interface_exposure`, or `exposure_time` when that is 0 |
| otherwise | `max(exposure_time, initial_exposure_time - i * fade_step)` |

`bottom_layer_count` is `Domain::sla_bottom_layer_count()`: the setting where the resin states one,
and otherwise the transition layers plus the first one, which is the number of layers the engine
actually holds at the bottom exposure. A print shorter than its burn-in is cut to the layers it
has, so the count never runs past the last layer.

`fade_step` is the difference between the two exposures spread over the transition layers plus the
first one:

```
fade_step = (initial_exposure_time - exposure_time) / (sla_effective_faded_layers() + 1)
```

This is the same ramp the engine uses in `merge_slices_and_eval_stats()`
(`std::max(exp_time, init_exp_time - sliced_layer_cnt * delta_fade_time)`), so a layer of the
sidebar estimate is exposed for as long as the engine thinks it is. An `initial_exposure_time` at
or below `exposure_time` makes `fade_step` 0, which is the same thing the engine's `std::max`
does. The `max` also means the ramp never dips below the normal exposure: a print whose ramp would
finish early simply sits at the normal exposure.

The raft interface band is `Domain::sla_raft_interface()`, which never reaches down into the
burn-in, so a layer is not both and the order of the first two rows does not matter.

### light_off

The three `wait_*` delays, or their `bottom_wait_*` counterparts on a burn-in layer:

```
light_off = wait_before_lift + wait_after_lift + wait_after_retract        (normal layer)
light_off = bottom_wait_before_lift + bottom_wait_after_lift
                            + bottom_wait_after_retract                     (burn-in layer)
```

`light_pwm` and `bottom_light_pwm` are not times and are not in the estimate; what the light is
set to does not change how long the layer takes in this model.

### motion

The lift up and the retract back down, at the speed each has its own setting for. There is **no
retract distance setting** in the resin settings, so the plate returns over the distance it was
lifted, which is what the `.goo` and `.ctb` headers do as well.

```
motion = lift_height / lift_speed + lift_height / retract_speed
       + lift_height_2 / lift_speed_2 + lift_height_2 / retract_speed_2    (normal layer)
motion = bottom_lift_height / bottom_lift_speed
       + bottom_lift_height / bottom_retract_speed
       + bottom_lift_height_2 / bottom_lift_speed_2
       + bottom_lift_height_2 / bottom_retract_speed_2                      (burn-in layer)
```

The `*_2` settings are the second stage of the separation, which the `.goo` writer already wrote
into its own second-stage fields. Their default is 0, so a print that does not use a two-stage lift
pays for one move up and one move down, and the terms for the second stage cost nothing.

A term with a distance of 0 or a speed of 0 is 0. That is what makes the function safe on a
partial view: an option that is not there counts as 0, so a printer-only view (no resin settings
at all) estimates the exposures and nothing else, instead of dividing by a zero speed.

## The estimate in the result and the sidebar

`SLAPrint::Steps::merge_slices_and_eval_stats()` runs the estimate next to the other per-layer work
and puts it in `SLAResultData` (see `SLAResult.hpp`): the whole print in `print_time_s` and each
layer's own time in `layer_print_times_s`, both in seconds. `print_time_s` is empty rather than 0
when there is no print to estimate, which is a different state from a print that takes no time.

`SidebarSlaSummary` reads it and prints it as `hh:mm` next to the layer count it is summed from,
the way Chitubox and Lychee show it: whole hours without a leading zero (a print of a day and a half
reads `36:00`), the minutes always two digits, seconds dropped, and the en dash where there is no
value. A non-finite or negative value is shown as the dash too, not as `0:00`.

## What the writers write

Every format with a print time field writes this estimate:

| format | field | written as |
|---|---|---|
| `.pwmo` / `.pwmx` / `.pwms` | `anycubicsla_format_header::print_time_s` | the estimate in whole seconds, truncated |
| `.pm5` / `.pm5s` / `.pm7` | HEADER body +68, u32 | the estimate in whole seconds, truncated |
| `.goo` | `goo_header_info::printing_time` | the estimate in whole seconds, truncated |
| `.ctb` | print parameters word 23, u32 | the estimate rounded to the nearest second |
| `.sl1` / `.sl1s` | `printTime` in `config.ini` | the SL1 model, unchanged |

Before M1.11c each of the four non-SL1 writers summed its own terms and they did not agree: the
`.pwmx`/`.pm5` writers charged every layer the normal lift distance and one delay, the `.goo` and
`.ctb` writers charged the exposures and the waits but **no** lift or retract at all, and none of
them counted the second stage of the separation. The estimate counts all of it.

Two containers have fewer fields than the estimate has terms, and that is fine: the estimate is a
number to write, not a transcript of the settings. The `.pwmx` and `.pm5` headers have one delay
field and no light PWM, and one lift distance and one lift speed with no second stage, so the
`.pm5`/`.pwmx` file tells the printer less than the estimate knows. The `.ctb` writer used to
accumulate in whole milliseconds before rounding, which was there so the value did not depend on
the order the floats were added in; the shared function adds the terms in a fixed order per layer
and rounds once at the end, which is the same thing.

## Tests

- `src/slic3r-domain/test/Slic3r/Domain/SlaPrintTimeTests.cpp` - the formula itself: a hand-built
  three layer print (one burn-in layer, one transition layer, one normal layer) with the three
  parts of the layer time spelled out, a longer print that shows the exposure ramp and the floor at
  `exposure_time`, the raft interface band, the two-stage lift on both the normal and the burn-in
  layers, and the states that are not an estimate at all (no layers, a printer-only view, a
  distance with no speed).
- `src/slic3r-shared/test/Slic3r/Biz/ResultExport/SLA/CtbExportTests.cpp` - the `.ctb` header field
  read back from the written file, with and without a raft interface.
- `src/slic3r-shared/test/Slic3r/Biz/ResultExport/SLA/PM5ExportTests.cpp` - the `.pm5` HEADER +68
  field, which nothing read back before.
- `src/slic3r-shared/test/Slic3r/App/SidebarSlaSummaryTests.cpp` - the `hh:mm` formatting and the
  dash.
