#!/usr/bin/env python3
"""Validate .rsdef files against schema 3 (schema 2 files still load: they are
upgraded in memory, with a warning).

Mirrors the rules of ComponentData::fromJson (src/Document/Components/ComponentData.cpp)
and the DeviceTypes table (src/Document/Components/DeviceTypes.cpp): reference
resolution, target-kind rules, device types, unique ids, domains, mass rule and
absence of schema 1 keys.

Usage: tools/validate_rsdef.py [file-or-directory ...]
       (default: models/)
Exit status is 1 if any file is invalid.
"""
import json
import sys
from pathlib import Path

# type -> (allowed target kinds, dim or None for images/displays)
SENSOR_TYPES = {
    "jointpos": (["joint"], 1), "jointvel": (["joint"], 1),
    "tendonpos": (["tendon"], 1), "tendonvel": (["tendon"], 1),
    "accelerometer": (["site"], 3), "gyro": (["site"], 3),
}
ACTUATOR_TYPES = {t: (["joint", "tendon"], 1) for t in ("position", "velocity", "motor")}
CAMERA_TYPES = {t: (["site"], None) for t in ("rgb", "depth")}
DOMAINS = {"ranged": ["min", "max"], "unbounded": []}
FAMILIES = ("actuators", "sensors", "cameras", "displays")
INPUT_KINDS = ("joint", "display", "actuator", "emulator")
OUTPUT_KINDS = ("sensor", "camera", "emulator")
# mirrors src/Document/Components/HintClass.h
HINT_CLASSES = ("actuator", "sensor", "vision", "display", "mcu", "weight", "battery", "part")
OLD_SIGNAL_KEYS = ("pin_required", "data_type", "range", "target_joint", "target_site", "camera_name")


class Errors(list):
    def add(self, msg):
        self.append(msg)


# mirrors PinDef::types() / PinDef::capabilities() in ComponentData.cpp
PIN_TYPES = ("pwm", "digital", "analog", "i2c_sda", "i2c_scl", "spi_sck", "spi_mosi", "spi_miso",
             "uart_tx", "uart_rx", "power", "ground", "gpio", "power_out", "motor", "motor_out")
PIN_CAPS = ("digital", "pwm", "analog", "interrupt", "i2c_sda", "i2c_scl",
            "spi_sck", "spi_mosi", "spi_miso", "uart_tx", "uart_rx")
SCHEMA_VERSION, MIN_SCHEMA_VERSION = 3, 2
SOURCED_JOINT_FIELDS = ("range", "damping", "armature", "frictionloss")

def unique(errs, ids, what):
    seen = set()
    for i in ids:
        if i in seen:
            errs.add(f"duplicate {what} id '{i}'")
        seen.add(i)
    return seen


def check_target(errs, owner, target, allowed, ids):
    if not isinstance(target, dict) or "kind" not in target:
        errs.add(f"{owner}: missing target.kind")
        return
    kind = target["kind"]
    if kind not in allowed:
        errs.add(f"{owner}: target kind '{kind}' not allowed here (allowed: {', '.join(allowed)})")
        return
    if kind == "emulator":
        if "id" in target:
            errs.add(f"{owner}: emulator target takes no id")
    elif target.get("id") not in ids.get(kind, set()):
        errs.add(f"{owner}: target {kind} '{target.get('id')}' does not exist")


def power_source_pins(d):
    pw = d.get("power") or {}
    return ({s.get("pin") for s in pw.get("supplies", [])} |
            {r.get("out") for r in pw.get("regulators", [])})


SUPPLY_KINDS = ("usb", "battery", "supply")
REGULATOR_KINDS = ("linear", "buck")


def check_power(errs, d):
    """Mirrors parsePower in ComponentData.cpp."""
    pw = d.get("power")
    if pw is None:
        return
    if not isinstance(pw, dict):
        errs.add("power: must be an object")
        return
    types = {p.get("id"): p.get("type") for p in d.get("pins", [])}

    def pin(where, pin_id, allowed):
        if not pin_id:
            errs.add(f"{where}: missing pin")
        elif pin_id not in types:
            errs.add(f"{where}: pin '{pin_id}' does not exist")
        elif types[pin_id] not in allowed:
            errs.add(f"{where}: pin '{pin_id}' must be {' or '.join(allowed)}, is '{types[pin_id]}'")

    batteries = 0
    for i, s in enumerate(pw.get("supplies", [])):
        where = f"power.supplies[{i}]"
        pin(where, s.get("pin"), ("power_out",))
        kind = s.get("kind")
        if kind not in SUPPLY_KINDS:
            errs.add(f"{where}: kind '{kind}' is not one of {SUPPLY_KINDS}")
        if kind == "battery":
            batteries += 1
        elif not s.get("voltage", 0) > 0:
            errs.add(f"{where}: 'voltage' must be > 0")
        for key in ("r_internal_ohm", "current_limit_a", "hold_current_a"):
            if s.get(key, 0) < 0:
                errs.add(f"{where}: '{key}' is negative")
    reg_ids = set()
    for i, r in enumerate(pw.get("regulators", [])):
        where = f"power.regulators[{i}]"
        for key in ("id", "in", "out", "v_out", "i_max_a"):
            if key not in r:
                errs.add(f"{where}: missing '{key}'")
        pin(where + ".in", r.get("in"), ("power", "power_out"))
        pin(where + ".out", r.get("out"), ("power_out",))
        if r.get("kind", "linear") not in REGULATOR_KINDS:
            errs.add(f"{where}: kind '{r.get('kind')}' is not one of {REGULATOR_KINDS}")
        if not (r.get("v_out", 0) > 0 and r.get("i_max_a", 0) > 0):
            errs.add(f"{where}: 'v_out' and 'i_max_a' must be > 0")
        if "i_limit_a" in r and r["i_limit_a"] < r.get("i_max_a", 0):
            errs.add(f"{where}: 'i_limit_a' must be at least 'i_max_a'")
        if not 0 < r.get("efficiency", 0.85) <= 1:
            errs.add(f"{where}: 'efficiency' must be in (0, 1]")
        if r.get("id") in reg_ids:
            errs.add(f"{where}: duplicate id '{r.get('id')}'")
        reg_ids.add(r.get("id"))
    load_pins = set()
    for i, l in enumerate(pw.get("loads", [])):
        where = f"power.loads[{i}]"
        pin(where, l.get("pin"), ("power", "power_out"))
        if l.get("current_ma", 0) < 0 or l.get("min_v", 0) < 0:
            errs.add(f"{where}: negative value")
        if l.get("pin") in load_pins:
            errs.add(f"{where}: pin '{l.get('pin')}' already has a load")
        load_pins.add(l.get("pin"))
    if "brownout" in pw:
        b = pw["brownout"]
        for key in ("rail", "threshold_v"):
            if key not in b:
                errs.add(f"power.brownout: missing '{key}'")
        pin("power.brownout.rail", b.get("rail"), ("power", "power_out"))
        if d.get("board") is None:
            errs.add("power.brownout: only boards have a brownout detector")
    if "battery" in pw:
        b = pw["battery"]
        if not b.get("capacity_mah", 0) > 0:
            errs.add("power.battery: 'capacity_mah' must be > 0")
        curve = b.get("curve", [])
        if any(not isinstance(pt, list) or len(pt) != 3 for pt in curve):
            errs.add("power.battery.curve: each point is [charge %, open-circuit V, internal ohm]")
        else:
            pts = sorted(curve)
            if len(pts) < 2:
                errs.add("power.battery.curve: needs at least 2 points")
            elif pts[-1][0] != 100 or not any(pt[0] == 0 for pt in pts):
                errs.add("power.battery.curve: must reach 100 % and have a point at 0 % "
                         "(points below 0 % are the charge past the rated cut-off)")
            if any(pt[1] <= 0 or pt[2] < 0 for pt in pts):
                errs.add("power.battery.curve: voltages must be > 0, resistances >= 0")
            if len({pt[0] for pt in pts}) != len(pts):
                errs.add("power.battery.curve: duplicate charge level")
        cells = b.get("cells", 1)
        if not isinstance(cells, int) or isinstance(cells, bool) or cells < 1:
            errs.add("power.battery: 'cells' must be a whole number >= 1")
        if "rate" in b:
            r = b["rate"]
            for key in ("available_fraction", "rate_constant_per_h"):
                if key not in r:
                    errs.add(f"power.battery.rate: missing '{key}'")
            if not 0 < r.get("available_fraction", 1) <= 1:
                errs.add("power.battery.rate: 'available_fraction' must be in (0, 1]")
            elif r.get("available_fraction", 1) < 1 and not r.get("rate_constant_per_h", 0) > 0:
                errs.add("power.battery.rate: 'rate_constant_per_h' must be > 0")
        if b.get("over_discharge_v", 0) < 0:
            errs.add("power.battery: 'over_discharge_v' must be >= 0")
        if "protection" in b:
            p = b["protection"]
            if not p.get("cutoff_v", 0) > 0 or p.get("delay_ms", 0) < 0:
                errs.add("power.battery.protection: 'cutoff_v' must be > 0, 'delay_ms' >= 0")
        if batteries != 1:
            errs.add("power.battery: needs exactly one supply of kind 'battery'")
    elif batteries:
        errs.add("power: a supply of kind 'battery' needs a 'battery' block")


def power_source_keys(pw):
    """The power block's numbers that need a source, as sources keys."""
    keys = [f"supplies.{s.get('pin')}" for s in pw.get("supplies", [])]
    keys += [f"regulators.{r.get('id')}" for r in pw.get("regulators", [])]
    keys += [f"loads.{l.get('pin')}" for l in pw.get("loads", [])]
    keys += [k for k in ("brownout", "battery") if k in pw]
    keys += [f"battery.{k}" for k in ("rate", "protection") if k in pw.get("battery", {})]
    return keys


def check_pins(errs, d):
    pins = d.get("pins", [])
    board = d.get("board")
    numbers = {}
    for p in pins:
        pid, ptype = p.get("id"), p.get("type")
        if ptype is not None and ptype not in PIN_TYPES:
            errs.add(f"pin '{pid}': type '{ptype}' is not one of {PIN_TYPES}")
        for c in p.get("caps", []):
            if c not in PIN_CAPS:
                errs.add(f"pin '{pid}': capability '{c}' is not one of {PIN_CAPS}")
        if p.get("caps") and ptype != "gpio":
            errs.add(f"pin '{pid}': only gpio pins have caps")
        if ptype == "gpio" and "number" not in p:
            errs.add(f"pin '{pid}': a gpio pin needs a number")
        if p.get("internal") and "number" not in p:
            errs.add(f"pin '{pid}': an internal pin needs a number")
        if board is None and (ptype == "gpio" or p.get("internal")):
            errs.add(f"pin '{pid}': gpio and internal pins are only on boards (add a \"board\" block)")
        if board is None and ptype == "power_out" and pid not in power_source_pins(d):
            errs.add(f"pin '{pid}': a power_out pin needs a supply or regulator in the \"power\" block")
        if "number" in p:
            if p["number"] in numbers:
                errs.add(f"pins '{numbers[p['number']]}' and '{pid}' have the same number {p['number']}")
            numbers[p["number"]] = pid
    if board is None:
        return
    for key in ("fqbn", "logic_voltage"):
        if not board.get(key):
            errs.add(f"board: missing '{key}'")
    by_id = {p.get("id"): p for p in pins}

    def check_ref(field, pin_id, cap):
        if not pin_id:
            return
        p = by_id.get(pin_id)
        if p is None:
            errs.add(f"board.{field}: pin '{pin_id}' does not exist")
        elif p.get("type") != "gpio" or cap not in p.get("caps", []):
            errs.add(f"board.{field}: pin '{pin_id}' is not a gpio pin with '{cap}'")
    i2c, spi = board.get("i2c", {}), board.get("spi", {})
    check_ref("i2c.sda", i2c.get("sda"), "i2c_sda")
    check_ref("i2c.scl", i2c.get("scl"), "i2c_scl")
    check_ref("spi.sck", spi.get("sck"), "spi_sck")
    check_ref("spi.mosi", spi.get("mosi"), "spi_mosi")
    check_ref("spi.miso", spi.get("miso"), "spi_miso")
    for pin_id in board.get("analog_write", {}).get("pins", {}):
        check_ref("analog_write.pins", pin_id, "pwm")
    uart = board.get("uart", {})
    names = set()
    for port in uart.get("ports", []):
        name = port.get("name", "")
        if not str(name).isidentifier():
            errs.add(f"board.uart.ports: '{name}' is not a C++ name")
        if name in names:
            errs.add(f"board.uart.ports: '{name}' is listed twice")
        names.add(name)
        if port.get("matrix"):
            if port.get("rx") or port.get("tx"):
                errs.add(f"board.uart.ports: '{name}' is a matrix port: its pins are chosen in begin(), not listed")
        elif not port.get("rx") or not port.get("tx"):
            errs.add(f"board.uart.ports: '{name}' needs 'rx' and 'tx'")
        else:
            check_ref(f"uart.ports.{name}.rx", port.get("rx"), "uart_rx")
            check_ref(f"uart.ports.{name}.tx", port.get("tx"), "uart_tx")
    for key in ("rx_buffer", "tx_buffer"):
        if key in uart and (not isinstance(uart[key], int) or uart[key] < 1):
            errs.add(f"board.uart: '{key}' must be an integer >= 1")
    variant = board.get("variant", {})
    for name, num in variant.get("pins", {}).items():
        if not isinstance(num, int) or isinstance(num, bool) or num < 0:
            errs.add(f"board.variant.pins: '{name}' must be a pin number >= 0")
        elif not name.isidentifier():
            errs.add(f"board.variant.pins: '{name}' is not a C++ name")
    if any(not isinstance(x, str) or not x.split("=")[0].isidentifier() for x in variant.get("defines", [])):
        errs.add("board.variant.defines: each entry is NAME or NAME=VALUE")


def validate(d):
    errs = Errors()
    if d.get("schema") not in range(MIN_SCHEMA_VERSION, SCHEMA_VERSION + 1):
        errs.add(f"schema must be {MIN_SCHEMA_VERSION}..{SCHEMA_VERSION}, got {d.get('schema')!r}")
        return errs
    if not d.get("id"):
        errs.add("missing top-level 'id'")
    if "io" in d:
        errs.add("'io' is not allowed (renamed to 'interface')")
    if "id" in d.get("meta", {}):
        errs.add("'id' must be top-level, not inside meta")
    if "hint_class" in d.get("meta", {}) and d["meta"]["hint_class"] not in HINT_CLASSES:
        errs.add(f"meta.hint_class '{d['meta']['hint_class']}' is not one of: {', '.join(HINT_CLASSES)}")

    k = d.get("kinematics", {})
    bodies, joints = k.get("bodies", []), k.get("joints", [])
    body_ids = unique(errs, [b.get("id") for b in bodies], "body")
    joint_ids = unique(errs, [j.get("id") for j in joints], "joint")
    tendon_ids = unique(errs, [t.get("id") for t in k.get("tendons", [])], "tendon")
    if k.get("default_body") not in body_ids:
        errs.add(f"default_body '{k.get('default_body')}' is not a body")

    geom_ids, site_ids = [], []
    for b in bodies:
        bid = b.get("id")
        override = b.get("overrideGeom", False)
        if override:
            for key in ("mass", "inertia"):
                if key not in b:
                    errs.add(f"body '{bid}': overrideGeom true requires '{key}'")
        else:
            for key in ("mass", "inertia"):
                if key in b:
                    errs.add(f"body '{bid}': '{key}' only allowed when overrideGeom is true")
        for g in b.get("geoms", []):
            geom_ids.append(g.get("id"))
            if not g.get("id"):
                errs.add(f"body '{bid}': geom without id")
            if not g.get("type"):
                errs.add(f"geom '{g.get('id')}': missing type")
            if override and "mass" in g:
                errs.add(f"geom '{g.get('id')}': body has overrideGeom, geom must carry no mass")
            if not override and "mass" not in g:
                errs.add(f"geom '{g.get('id')}': body has no overrideGeom, geom must carry 'mass'")
            for key in ("material", "mesh"):
                if key in g and not isinstance(g[key], str):
                    errs.add(f"geom '{g.get('id')}': '{key}' must be a string")
        for s in b.get("sites", []):
            site_ids.append(s.get("id"))
            if "sensor" in s:
                errs.add(f"site '{s.get('id')}': 'sensor' is not allowed (use devices.sensors)")
    geom_ids = unique(errs, geom_ids, "geom")
    site_ids = unique(errs, site_ids, "site")

    unique(errs, [p.get("id") for p in d.get("pins", [])], "pin")
    check_pins(errs, d)
    check_power(errs, d)
    for j in joints:
        for key in ("actuator", "sensor"):
            if key in j:
                errs.add(f"joint '{j.get('id')}': '{key}' is not allowed (use devices)")
        for side in ("body_a", "body_b"):
            if j.get(side) not in body_ids:
                errs.add(f"joint '{j.get('id')}': {side} '{j.get(side)}' does not exist")
        src = j.get("sources", {})
        if not isinstance(src, dict):
            errs.add(f"joint '{j.get('id')}': 'sources' must be an object")
        else:
            for key, val in src.items():
                if key not in SOURCED_JOINT_FIELDS:
                    errs.add(f"joint '{j.get('id')}': sources.{key} is not a sourced joint field {SOURCED_JOINT_FIELDS}")
                elif not isinstance(val, str):
                    errs.add(f"joint '{j.get('id')}': sources.{key} must be a string")
    # Joints stacked on one body pair need armature > 0 (else MuJoCo's mass matrix is singular)
    pair = lambda j: frozenset((j.get("body_a"), j.get("body_b")))
    per_pair = {}
    for j in joints:
        per_pair[pair(j)] = per_pair.get(pair(j), 0) + 1
    for j in joints:
        if per_pair[pair(j)] > 1 and not j.get("armature", 0) > 0:
            errs.add(f"joint '{j.get('id')}': shares bodies '{j.get('body_a')}'/'{j.get('body_b')}' "
                     "with another joint, so it needs armature > 0")
    for c in d.get("connectors", []):
        if c.get("body") not in body_ids:
            errs.add(f"connector '{c.get('id')}': body '{c.get('body')}' does not exist")
    for t in k.get("tendons", []):
        if t.get("type") != "fixed":
            errs.add(f"tendon '{t.get('id')}': only type 'fixed' is supported")
        if not t.get("terms"):
            errs.add(f"tendon '{t.get('id')}': terms must not be empty")
        for term in t.get("terms", []):
            if term.get("joint") not in joint_ids:
                errs.add(f"tendon '{t.get('id')}': joint '{term.get('joint')}' does not exist")

    devices = d.get("devices", {})
    for fam in devices:
        if fam not in FAMILIES:
            errs.add(f"devices: unknown family '{fam}'")
    ids = {"joint": joint_ids, "tendon": tendon_ids, "site": site_ids, "geom": geom_ids,
           "actuator": set(), "sensor": set(), "camera": set(), "display": set()}
    all_device_ids = []
    tables = {"actuators": ACTUATOR_TYPES, "sensors": SENSOR_TYPES, "cameras": CAMERA_TYPES}
    singular = {"actuators": "actuator", "sensors": "sensor", "cameras": "camera", "displays": "display"}
    for fam in FAMILIES:
        for dev in devices.get(fam, []):
            all_device_ids.append(dev.get("id"))
            ids[singular[fam]].add(dev.get("id"))
    unique(errs, all_device_ids, "device")
    for fam in FAMILIES:
        for dev in devices.get(fam, []):
            owner = f"{fam[:-1]} '{dev.get('id')}'"
            if not dev.get("id"):
                errs.add(f"{fam}: entry without id")
            if fam == "displays":
                if "type" in dev:
                    errs.add(f"{owner}: displays take no type")
                allowed = ["geom"]
            else:
                row = tables[fam].get(dev.get("type"))
                if row is None:
                    errs.add(f"{owner}: unknown type '{dev.get('type')}' (supported: {', '.join(tables[fam])})")
                    continue
                allowed = row[0]
            check_target(errs, owner, dev.get("target"), allowed, ids)
            if fam in ("actuators",):
                for key in ("ctrlrange", "forcerange"):
                    if key in dev and not (isinstance(dev[key], list) and len(dev[key]) == 2):
                        errs.add(f"{owner}: '{key}' must be [min, max]")
            if fam in ("cameras", "displays"):
                r = dev.get("resolution")
                if not (isinstance(r, list) and len(r) == 2 and all(isinstance(x, int) and x > 0 for x in r)):
                    errs.add(f"{owner}: 'resolution' must be two positive integers")
            if fam == "cameras" and "fovy" not in dev:
                errs.add(f"{owner}: missing 'fovy'")
            if fam == "displays":
                surf = dev.get("surface")
                if not isinstance(surf, dict):
                    errs.add(f"{owner}: missing 'surface' (transform + size)")
                else:
                    if not isinstance(surf.get("transform"), dict):
                        errs.add(f"{owner}: surface: missing 'transform'")
                    size = surf.get("size")
                    if not (isinstance(size, list) and len(size) == 2 and all(isinstance(x, (int, float)) and x > 0 for x in size)):
                        errs.add(f"{owner}: surface: 'size' must be two positive half-extents [hx, hy]")

    iface = d.get("interface", {})
    names = []
    sensor_dims = {s["id"]: SENSOR_TYPES.get(s.get("type"), (None, None))[1] for s in devices.get("sensors", []) if "id" in s}
    for direction, allowed in (("inputs", INPUT_KINDS), ("outputs", OUTPUT_KINDS)):
        for sig in iface.get(direction, []):
            names.append(sig.get("name"))
            owner = f"{direction} signal '{sig.get('name')}'"
            if not sig.get("name"):
                errs.add(f"{direction}: signal without name")
            if not isinstance(sig.get("physical"), bool):
                errs.add(f"{owner}: 'physical' must be a bool")
            for key in OLD_SIGNAL_KEYS:
                if key in sig:
                    errs.add(f"{owner}: schema 1 key '{key}'")
            check_target(errs, owner, sig.get("target"), allowed, ids)
            kind = (sig.get("target") or {}).get("kind")
            if kind == "emulator":
                if sig.get("channel_type") not in ("scalar", "vector"):
                    errs.add(f"{owner}: emulator signal needs channel_type scalar|vector")
                dim = sig.get("dim")
                if not (isinstance(dim, int) and dim >= 1) or (sig.get("channel_type") == "scalar" and dim != 1):
                    errs.add(f"{owner}: bad dim {dim!r}")
            else:
                for key in ("channel_type", "dim"):
                    if key in sig:
                        errs.add(f"{owner}: '{key}' only allowed on emulator signals")
            if "componentLabels" in sig and kind == "sensor":
                dim = sensor_dims.get(sig["target"].get("id"))
                if dim is not None and len(sig["componentLabels"]) != dim:
                    errs.add(f"{owner}: componentLabels has {len(sig['componentLabels'])} entries, sensor dim is {dim}")
            if "domain" in sig:
                dom = sig["domain"]
                if dom.get("type") not in DOMAINS:
                    errs.add(f"{owner}: unknown domain type '{dom.get('type')}'")
                else:
                    params = dom.get("parameters", {})
                    for p in params:
                        if p not in DOMAINS[dom["type"]]:
                            errs.add(f"{owner}: unknown domain parameter '{p}'")
                    for p in DOMAINS[dom["type"]]:
                        if not isinstance(params.get(p), (int, float)) or isinstance(params.get(p), bool):
                            errs.add(f"{owner}: domain parameter '{p}' missing or not a number")
    unique(errs, names, "signal name")

    emu = d.get("emulator") or {}
    sources = emu.get("parameter_sources", {})
    if not isinstance(sources, dict) or not all(isinstance(v, str) for v in sources.values()):
        errs.add("emulator: 'parameter_sources' must map parameter names to strings")
    return errs


def warnings(d):
    """Non-fatal: error-model parameters without a stated source."""
    out = []
    if d.get("schema") == 2:
        out.append("schema 2: loads (upgraded in memory); tools/convert_schema3.py updates the file")
    board = d.get("board")
    if board is not None:
        bsrc = board.get("sources", {}) if isinstance(board.get("sources"), dict) else {}
        for key in ("logic_voltage", "adc", "analog_write", "i2c", "spi", "uart"):
            if key in board and not str(bsrc.get(key, "")).strip():
                out.append(f"board '{key}' has no source (datasheet, measurement or 'estimate: ...')")
    pw = d.get("power")
    if isinstance(pw, dict):
        psrc = pw.get("sources", {}) if isinstance(pw.get("sources"), dict) else {}
        keys = power_source_keys(pw)
        out += [f"power '{k}' has no source (datasheet, measurement or 'estimate: ...')"
                for k in keys if not str(psrc.get(k, "")).strip()]
        out += [f"power.sources: '{k}' names nothing in the power block" for k in psrc if k not in keys]
    # A joint stacked on another joint's bodies is a play gap (backlash): an
    # error-model parameter in all but name, so its range needs a source.
    joints = (d.get("kinematics") or {}).get("joints", [])
    seen = set()
    for j in joints:
        pair = frozenset((j.get("body_a"), j.get("body_b")))
        src = j.get("sources") if isinstance(j.get("sources"), dict) else {}
        if pair in seen and "range" in j and not str(src.get("range", "")).strip():
            out.append(f"joint '{j.get('id')}' (play gap on another joint) has no sources.range")
        seen.add(pair)
    emu = d.get("emulator") or {}
    sources = emu.get("parameter_sources", {})
    if not isinstance(sources, dict):
        return out
    params = emu.get("parameters") or {}
    out += [f"emulator parameter '{k}' has no source (datasheet, measurement or 'estimate: ...')"
           for k in params if not str(sources.get(k, "")).strip()]
    out += [f"parameter_sources: '{k}' is not an emulator parameter" for k in sources if k not in params]
    return out


def collect(args):
    root = Path(__file__).resolve().parent.parent
    targets = [Path(a) for a in args] or [root / "models"]
    files = []
    for t in targets:
        files += sorted(t.rglob("*.rsdef")) if t.is_dir() else [t]
    return files


def main(argv):
    bad = 0
    files = collect(argv)
    for f in files:
        try:
            data = json.loads(f.read_text())
        except (OSError, json.JSONDecodeError) as e:
            print(f"FAIL {f}: {e}")
            bad += 1
            continue
        errs = validate(data)
        if errs:
            bad += 1
            print(f"FAIL {f}")
            for e in errs:
                print(f"     {e}")
        else:
            print(f"ok   {f}")
        for w in warnings(data):
            print(f"     warning: {w}")
    print(f"{len(files) - bad}/{len(files)} valid")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
