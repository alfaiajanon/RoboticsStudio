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
"battery": {"capacity_mah": 2000,
            "curve": [[0, 6.6, 0.08], [50, 7.7, 0.03], [100, 8.4, 0.03]]}
```

`curve` points are `[charge %, open-circuit V, internal Ω]`, from 0 to 100 %, interpolated. Read them off the datasheet's discharge curves (or measure your pack). The Inspector sets the starting charge and whether it drains during the run. A battery's `−` is a `ground` pin.

## Rules

- A `power_out` pin on a part that isn't a board must be one of its supplies or a regulator's output.
- Every number needs a source in `power.sources`, keyed `supplies.<pin>`, `regulators.<id>`, `loads.<pin>`, `brownout`, `battery`; `tools/validate_rsdef.py` warns otherwise.
- All of a part's `ground` pins count as one (common inside the part), as on a buck module's IN− and OUT−.

Examples: `models/board/arduino_uno`, `models/board/esp32_cam`, `models/power/*`, and the servos' `vcc` loads.
