#!/usr/bin/env python3
"""Fit and check a battery's rate block (Kinetic Battery Model) against datasheet capacities.

The model is the one PowerSystem runs (src/Simulation/Power/PowerSystem.cpp,
docs/user/create_power.md): the charge sits in two wells, in % of the rated
capacity above the curve's dead point. The load draws from the available well
(fraction c); the bound charge flows into it at k * (h_bound - h_available)
per hour. The curve (per cell) is read at the available well's level; the
terminal voltage is cells * (OCV - I * R).

A datasheet point is (current A, capacity mAh, cut-off V per cell): the charge
a constant-current discharge gives until the terminals fall to the cut-off.

    tools/fit_battery.py PART.rsdef --point 0.025 2900 0.8 --point 0.5 1400 0.8 [--check 0.1 2200 0.8]
        fits available_fraction and rate_constant_per_h to the --point values,
        prints the residuals (and the --check points, not used in the fit)
    tools/fit_battery.py PART.rsdef --check 0.5 1400 0.8 ...
        only checks the part's current rate block
    --trace I    prints the voltage over a constant-current discharge at I amps
"""
import argparse
import json
import math
import sys


def at(curve, percent):
    """Per cell (OCV, R) at a charge level, interpolated, clamped (BatteryDef::at)."""
    if percent <= curve[0][0]:
        return curve[0][1], curve[0][2]
    if percent >= curve[-1][0]:
        return curve[-1][1], curve[-1][2]
    for (p0, v0, r0), (p1, v1, r1) in zip(curve, curve[1:]):
        if percent <= p1:
            f = (percent - p0) / (p1 - p0)
            return v0 + f * (v1 - v0), r0 + f * (r1 - r0)
    return curve[-1][1], curve[-1][2]


def discharge(bat, c, k, amps, cutoff, start=100.0, trace=None):
    """Constant-current discharge from `start` %; returns mAh delivered to the cut-off (per cell)."""
    curve = sorted(bat["curve"])
    cells = bat.get("cells", 1)
    cap = bat["capacity_mah"] / 1000.0
    dead = curve[0][0]
    above = start - dead
    avail, bound = above * c, above * (1.0 - c)
    # Step: at most 0.02 % of rated drawn, and well inside the flow's time constant.
    dt = 0.0002 * cap / amps
    if c < 1.0:
        dt = min(dt, 0.2 * c * (1.0 - c) / k)
    t = 0.0
    delivered = 0.0
    next_trace = 0.0
    while t < 1e4:
        level = dead + avail / c
        ocv, r = at(curve, level)
        i = min(amps, ocv / r) if r > 0 else amps
        v = ocv - amps * r if amps <= i else 0.0
        if trace is not None and t >= next_trace:
            trace.append((t, delivered * 1000.0, v * cells, dead + avail + bound))
            next_trace = t + 0.05 * bat["capacity_mah"] / 1000.0 / amps
        if v < cutoff:
            return delivered * 1000.0
        drawn = i * dt / cap * 100.0
        flow = k * (bound / (1.0 - c) - avail / c) * dt if c < 1.0 else 0.0
        avail = max(0.0, avail - drawn + flow)
        bound = max(0.0, bound - flow)
        delivered += i * dt
        t += dt
        if avail <= 0.0 and bound <= 1e-9:
            return delivered * 1000.0
    return delivered * 1000.0


def error(bat, c, k, points):
    return math.sqrt(sum(((discharge(bat, c, k, i, v) - mah) / mah) ** 2 for i, mah, v in points) / len(points))


def fit(bat, points):
    best = (1e9, 1.0, 0.0)
    cs = [0.05 * n for n in range(2, 20)] + [0.97, 0.99]
    ks = [10 ** (e / 4.0) for e in range(-12, 9)]   # 0.001 .. 100 per hour
    for c in cs:
        for k in ks:
            e = error(bat, c, k, points)
            if e < best[0]:
                best = (e, c, k)
    e, c, k = best
    sc, sk = 0.05, 0.5   # refine: coordinate search (c linear, k in log)
    for _ in range(40):
        improved = False
        for dc, dk in ((sc, 0), (-sc, 0), (0, sk), (0, -sk)):
            c2, k2 = min(0.995, max(0.01, c + dc)), k * math.exp(dk)
            e2 = error(bat, c2, k2, points)
            if e2 < e:
                e, c, k, improved = e2, c2, k2, True
        if not improved:
            sc, sk = sc / 2, sk / 2
    return c, k, e


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rsdef")
    ap.add_argument("--point", nargs=3, type=float, action="append", default=[], metavar=("A", "MAH", "V_CELL"))
    ap.add_argument("--check", nargs=3, type=float, action="append", default=[], metavar=("A", "MAH", "V_CELL"))
    ap.add_argument("--trace", type=float, metavar="A")
    args = ap.parse_args()

    with open(args.rsdef) as f:
        bat = json.load(f)["power"]["battery"]
    rate = bat.get("rate", {})
    c, k = rate.get("available_fraction", 1.0), rate.get("rate_constant_per_h", 0.0)
    if args.point:
        c, k, e = fit(bat, args.point)
        print(f'fit: "rate": {{"available_fraction": {c:.3f}, "rate_constant_per_h": {k:.4g}}}  (rms error {e * 100:.1f} %)')
    print(f"{'current':>9} {'datasheet':>10} {'model':>8} {'error':>7}  cut-off")
    for tag, pts in (("fit", args.point), ("check", args.check)):
        for i, mah, v in pts:
            m = discharge(bat, c, k, i, v)
            print(f"{i * 1000:7.0f}mA {mah:8.0f}mAh {m:6.0f}mAh {100 * (m - mah) / mah:+6.1f}%  {v} V/cell ({tag})")
    if args.trace:
        tr = []
        cut = sorted(bat["curve"])[0][1] * 0.5
        discharge(bat, c, k, args.trace, cut, trace=tr)
        print(f"discharge at {args.trace} A: hours, mAh, pack V, charge %")
        for t, mah, v, pct in tr:
            print(f"  {t:8.3f} h {mah:7.0f} mAh {v:6.3f} V {pct:6.1f} %")
    return 0


if __name__ == "__main__":
    sys.exit(main())
