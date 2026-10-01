#!/usr/bin/env python3
"""Validate .rsdef files against schema 2.

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
OLD_SIGNAL_KEYS = ("pin_required", "data_type", "range", "target_joint", "target_site", "camera_name")


class Errors(list):
    def add(self, msg):
        self.append(msg)


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


def validate(d):
    errs = Errors()
    if d.get("schema") != 2:
        errs.add(f"schema must be 2, got {d.get('schema')!r}")
        return errs
    if not d.get("id"):
        errs.add("missing top-level 'id'")
    if "io" in d:
        errs.add("'io' is not allowed (renamed to 'interface')")
    if "id" in d.get("meta", {}):
        errs.add("'id' must be top-level, not inside meta")

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

    for j in joints:
        for key in ("actuator", "sensor"):
            if key in j:
                errs.add(f"joint '{j.get('id')}': '{key}' is not allowed (use devices)")
        for side in ("body_a", "body_b"):
            if j.get(side) not in body_ids:
                errs.add(f"joint '{j.get('id')}': {side} '{j.get(side)}' does not exist")
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
    return errs


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
    print(f"{len(files) - bad}/{len(files)} valid")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
