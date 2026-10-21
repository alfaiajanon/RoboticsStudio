# Creating a motor driver

A motor driver module (L298N, TB6612FNG, ...) sits between a board and a DC motor. Its logic inputs come from the board's pins; its outputs are wired to the motor's terminals and switched to its motor supply. It matters only at the Real level, where it is wired on the Wiring page ([wiring.md](wiring.md)). At the Prototype level a DC motor has a built-in ideal driver. Read [component_basics.md](component_basics.md) and [create_power.md](create_power.md) first. References: `models/driver/l298n_module`, `models/driver/tb6612fng_module`.

## Structure

A driver is a circuit board: one body (`base`, with the board's mass) and a mesh or a few boxes. It has no joints, actuators or sensors, and its `interface` is empty. Give it a `top` connector for parts stacked on it and a `base` connector to mount it.

## Pins

The emulator finds the pins by their ids, so keep them:

| Truth table | Inputs (from the board) | Outputs (to the motor) | Supply |
|---|---|---|---|
| `l298` | `in1`, `in2`, `ena` (channel A); `in3`, `in4`, `enb` (channel B) | `out1`, `out2`; `out3`, `out4` | `vs` (motor supply), `gnd`, and optionally `5v` (the module's regulator) |
| `tb6612` | `ain1`, `ain2`, `pwma`; `bin1`, `bin2`, `pwmb`; `stby` | `ao1`, `ao2`; `bo1`, `bo2` | `vm` (motor supply), `vcc` (logic), `gnd` |

- Inputs: `digital`, or `pwm` for an enable or speed input (`ena`, `pwma`).
- Outputs: `"type": "motor_out"`. A motor's terminals are `"type": "motor"`. The checks know that a `motor_out` belongs on a motor, and flag one wired to another output, to a board pin, to a supply or to ground.
- The motor supply pin comes **first** among the power pins: the emulator takes its supply from the first `power` pin.

## Power block

```json
"power": {
  "regulators": [{ "id": "reg_5v", "in": "vs", "out": "5v", "kind": "linear", "v_out": 5.0,
                   "dropout_v": 2.0, "i_max_a": 0.5, "i_quiescent_ma": 6 }],
  "loads": [{ "pin": "vs", "current_ma": 13, "min_v": 4.5 },
            { "pin": "5v", "current_ma": 24 }]
}
```

The loads are the chip's quiescent draw. The motor current is added on top while the motors run: the driver draws, from its motor supply, the current its high sides pass. A supply rail therefore sags with the motors, and a battery drains with them. The power design rules count each driven motor's stall current against the driver's supply (see [wiring.md](wiring.md#power)).

## Emulator

`"type": "hbridge_emulator"`. Give every parameter a source in `parameter_sources`.

| Parameter | Unit | L298N | TB6612FNG | Meaning |
|---|---|---|---|---|
| `truth_table` | | `l298` | `tb6612` | how the inputs set the outputs (below) |
| `high_v0`, `high_r` | V, Ω | 0.7, 0.65 | 0, 0.25 | high side (output to the supply): drop = `high_v0 + high_r·I` |
| `low_v0`, `low_r` | V, Ω | 0.7, 0.5 | 0, 0.25 | low side (output to ground): drop = `low_v0 + low_r·I` |
| `channel_max_a` | A | 2.0 | 1.2 | continuous current per channel; a motor whose stall current is higher is flagged |
| `ideal_supply_v` | V | 12 | 12 | the supply voltage when there is no power model |

A bipolar bridge (L298) loses a fixed ~1.4 V plus a resistance: at 1 A its two sides drop about 2.5 V, so a 7.4 V pack gives a 12 V motor less than 5 V. A MOSFET bridge (TB6612) has only its on-resistance (0.5 Ω through both sides).

**Truth tables** (each channel):

- `l298`: EN LOW turns both outputs off, and the motor coasts. With EN HIGH, an IN that is HIGH connects its output to the supply and an IN that is LOW connects it to ground. IN1 = IN2 brakes, to ground or to the supply. PWM on EN switches the outputs off for the rest of each period (fast decay). The module's **ENA/ENB jumpers** tie EN to 5 V. They are fitted by default; each is an Inspector checkbox on the part (instance parameters `driver.ena_jumper`, `driver.enb_jumper`). With a jumper fitted, the speed comes from PWM on IN1/IN2, and both LOW is a brake, not a coast.
- `tb6612`: STBY LOW turns everything off. IN1/IN2: H/L or L/H drives while PWM is HIGH and short-brakes while it is LOW (slow decay). H/H short-brakes; L/L stops (outputs off, coast).

A PWM'd input is averaged: an output that is HIGH for a fraction *f* of each period acts as *f*·(supply − high drop) + (1 − *f*)·(low drop), through the matching mix of resistances.

**Not modelled:** PWM ripple, thermal shutdown (above `channel_max_a` there is only a warning), the outputs' clamp diodes beyond the averaged braking, the L298's current-sense pins (wired to GND on modules), and the 5V-EN jumper (the regulator is always on).

## Checks

- **A motor wired straight to a board pin**: Error. A pin gives tens of mA, a motor wants hundreds. In the run the motor barely moves, and the pin is warned about above its absolute maximum (`pin_abs_max_ma` in the board's `board` block).
- **A driver output on another output, a board pin, a supply or ground**: Error (a short).
- **A motor that stalls above `channel_max_a`**: Warning.
- **An L298N's ENA/ENB wired to the board while its jumper is fitted**: Warning (the pin fights the jumper).
- Unused channels, and EN inputs tied by their jumper, are not reported as "not wired".
