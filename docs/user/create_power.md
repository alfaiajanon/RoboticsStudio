# The power block (supplies, regulators, loads)

At the Real level every part's electrical side comes from an optional `"power"` block in its `.rsdef`: what it supplies, what it converts, and what it draws. The simulation solves the rails from it ([wiring.md](wiring.md#power)). At the Prototype level it is ignored (ideal power).

```json
"power": {
    "supplies":   [{"pin": "5v", "kind": "usb", "voltage": 5.0, "r_internal_ohm": 0.5, "hold_current_a": 0.5}],
    "regulators": [{"id": "reg_5v", "in": "vin", "out": "5v", "kind": "linear",
                    "v_out": 5.0, "dropout_v": 1.1, "i_max_a": 1.0, "i_quiescent_ma": 5}],
    "loads":      [{"pin": "5v", "current_ma": 45}],
    "brownout":   {"rail": "5v", "threshold_v": 2.7, "hysteresis_v": 0.05, "restart_ms": 65},
    "sources":    {"supplies.5v": "datasheet: ...", "regulators.reg_5v": "datasheet: ...",
                   "loads.5v": "estimate: ...", "brownout": "datasheet: ..."}
}
```

## Supplies

A source on a `power_out` pin.

| Field | Meaning |
|---|---|
| `kind` | `usb`: a board's default feed, dropped as soon as anything else drives its rail. `battery`: voltage and resistance come from the `battery` block. `supply`: a bench supply; its voltage and current limit are set per instance in the Inspector |
| `voltage` | open-circuit volts (not for batteries; for a bench supply, the default) |
| `r_internal_ohm` | the voltage drops by this times the current |
| `current_limit_a` | `supply`: above it the voltage drops (constant current) |
| `hold_current_a` | `usb`: the polyfuse's hold current; the budget check and a run-time warning use it |

## Regulators

From `in` (a `power` pin, or a `power_out` rail feeding the next regulator) to `out` (a `power_out` pin).

| Field | Meaning |
|---|---|
| `kind` | `linear`: `Vout = min(v_out, Vin − dropout_v)`, input current = output current + quiescent. `buck`: `Vout = min(v_out, Vin − dropout_v)` (the headroom), input current = `Vout·Iout / (efficiency·Vin)` + quiescent. A buck module's output is set per instance in the Inspector |
| `i_max_a` | the rated current: the budget check compares against it |
| `i_limit_a` | optional, ≥ `i_max_a`: the current limit, above which the output folds back (`Vout × i_limit / Iout`). Defaults to `i_max_a` |
| `efficiency` | `buck` only, 0..1 |

## Loads

The static draw of a pin (`current_ma`, typical) and the lowest supply the part works at (`min_v`): below it the part is off (not updated, actuators at 0, silent on the buses) and it starts again from its power-on state when the rail recovers (50 mV above `min_v`). Motors add their own current on top, from their device model: the servo from `stall_current_a` (emulator parameter, at its `rated_voltage`), the stepper from `rated_current_a` and `phase_resistance_ohm`. A part draws from its first `power` pin.

## Brownout (boards)

`rail` is the pin the MCU runs on. Below `threshold_v` the board is held in reset: its firmware stops, its pins let go. Once the rail is back above `threshold_v + hysteresis_v` for `restart_ms`, the firmware starts again from `setup()`.

## Batteries

```json
"supplies": [{"pin": "plus", "kind": "battery"}],
"battery": {"cells": 2, "capacity_mah": 2000,
            "curve": [[-2.5, 1.50, 0.50], [-1.5, 2.50, 0.08], [0, 3.05, 0.035], [5, 3.61, 0.020], [50, 3.84, 0.015], [100, 4.20, 0.015]],
            "rate": {"available_fraction": 0.348, "rate_constant_per_h": 17.85},
            "over_discharge_v": 3.0,
            "protection": {"cutoff_v": 2.4, "delay_ms": 40}}
```

| Field | Meaning |
|---|---|
| `cells` | cells in series (default 1). The curve and the thresholds are **per cell**, as in datasheets; the pack has `cells` times the voltage and the resistance |
| `capacity_mah` | the rated capacity: what the datasheet gives at its stated drain down to its cut-off voltage. That cut-off is **0 %** |
| `curve` | `[charge %, open-circuit V, internal Ω]` per cell, interpolated. It must reach 100 % and have a point at 0 %. Points **below 0 %** are the charge a real cell still gives past its rated cut-off, down to its dead point (a few %): the voltage collapses and the resistance climbs there |
| `rate` | optional: the rate-capacity and recovery effects (below). Without it the whole charge is available at any current |
| `over_discharge_v` | optional, per cell, open circuit: below it a real cell is damaged (LiPo: 3.0 V). The run warns once; there is no damage model |
| `protection` | optional: the protection circuit (BMS) this pack can have. Below `cutoff_v` per cell **at the terminals** for `delay_ms`, the pack cuts itself off (0 V) until Stop (a real one needs a charger to release). `fitted` (default false) is the Inspector's default for "Protection circuit (BMS)" |

**How the charge moves (Kinetic Battery Model).** The charge sits in two wells. The load draws from the *available* well (`available_fraction` of the charge); the *bound* charge flows into it at `rate_constant_per_h` × (bound level − available level) per hour. The curve is read at the **available** level:
- at rest both levels are the same, so the voltage is the curve's;
- under a heavy load the available level falls faster than the charge does, so the voltage reaches the cut-off **before** the rated charge is used: an alkaline pack driving servos is "flat" with half its charge still in it;
- when the load stops, the bound charge flows back and the voltage **recovers** (the toy that works again after a rest).

This is the classic two-well model (Manwell & McGowan); it has one time constant, so it gives the slow part of a cell's recovery, not the quick rebound in the first seconds after a load stops.

**Fitting `rate`:** take the datasheet's capacities at two or three constant currents to one cut-off, and let `tools/fit_battery.py` fit the two numbers with the simulation's own model:

```bash
python3 tools/fit_battery.py models/power/battery_4xaa/battery_4xaa.rsdef \
    --point 0.025 2900 0.8 --point 0.1 2200 0.8 --point 0.5 1400 0.8
```

It prints the `rate` block and how far the model is from each point (`--check` adds points not used in the fit; `--trace A` prints a discharge at that current). Put the points you used in `power.sources` under `battery.rate`.

**What the gauge shows.** The Inspector, the plots and Output give the charge as a fuel gauge on the pack would: 0..100 %, 0 % at the rated cut-off. Past it the gauge stays at 0 % while the voltage keeps falling (plot `power.battery_v`). `power.charge_available` shows the available well.

The Inspector sets the starting charge, whether the pack drains during the run, and (with a `protection` block) whether a protection circuit is fitted. A battery's `−` is a `ground` pin.

## Rules

- A `power_out` pin on a part that isn't a board must be one of its supplies or a regulator's output.
- Every number needs a source in `power.sources`, keyed `supplies.<pin>`, `regulators.<id>`, `loads.<pin>`, `brownout`, `battery`, `battery.rate`, `battery.protection`; `tools/validate_rsdef.py` warns otherwise.
- All of a part's `ground` pins count as one (common inside the part), as on a buck module's IN− and OUT−.

Examples: `models/board/arduino_uno`, `models/board/esp32_cam`, `models/power/*`, and the servos' `vcc` loads.
