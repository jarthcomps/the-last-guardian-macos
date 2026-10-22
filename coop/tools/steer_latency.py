"""How Trico answers the stick in a probe's script log.

For each stretch of "TR ... on" reports (the stick held), prints how long Trico took to move 1 m
and 3 m from where he stood when the stick went out, how far he got, the distance walked along
the way, and how often he stopped (moved less than 0.2 m over a second after having moved) or
reversed (a second's motion against the previous one), using the "COOP t=" clock lines. Usage:
python3 tools/steer_latency.py logs/coop-20261021/<tag>-coop.txt ...
"""
import math
import re
import sys

CLOCK = re.compile(r"COOP t=([0-9.]+)")
STEER = re.compile(r"TR (\w+) (on|off) p=(-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+)")


def stretches(lines):
    now, cur = None, None
    for line in lines:
        m = CLOCK.search(line)
        if m:
            now = float(m.group(1))
            continue
        m = STEER.search(line)
        if not m or now is None:
            continue
        on = m.group(2) == "on"
        x, z = float(m.group(3)), float(m.group(5))
        if on and cur is None:
            cur = {"start": now, "origin": (x, z), "marks": {}, "far": 0.0, "samples": [(now, x, z)]}
        elif on:
            d = math.hypot(x - cur["origin"][0], z - cur["origin"][1])
            cur["far"] = max(cur["far"], d)
            for metres in (1, 3):
                if d >= metres and metres not in cur["marks"]:
                    cur["marks"][metres] = now - cur["start"]
            cur["samples"].append((now, x, z))
        elif cur is not None:
            cur["held"] = now - cur["start"]
            yield cur
            cur = None
    if cur is not None:
        cur["held"] = (now or cur["start"]) - cur["start"]
        yield cur


def motion(samples):
    """Path length, stops and reversals from per-second steps."""
    per_second = {}
    for t, x, z in samples:
        per_second.setdefault(int(t), (x, z))
    points = [per_second[k] for k in sorted(per_second)]
    path, stops, reversals, moved, last = 0.0, 0, 0, False, None
    for (x0, z0), (x1, z1) in zip(points, points[1:]):
        dx, dz = x1 - x0, z1 - z0
        step = math.hypot(dx, dz)
        if step > 20:   # a bogus position, not a step
            continue
        path += step
        if step < 0.2:
            if moved:
                stops += 1
            moved = False
            continue
        if last and (dx * last[0] + dz * last[1]) / (step * math.hypot(*last)) < -0.5:
            reversals += 1
        moved, last = True, (dx, dz)
    return path, stops, reversals


def main(paths):
    for path in paths:
        with open(path, encoding="utf-8", errors="replace") as f:
            lines = f.read().splitlines()
        print(path)
        for s in stretches(lines):
            def fmt(m):
                return f"{s['marks'][m]:.1f} s" if m in s["marks"] else "never"
            walked, stops, reversals = motion(s["samples"])
            print(f"  stick at t={s['start']:.1f} s for {s['held']:.1f} s: 1 m after {fmt(1)}, "
                  f"3 m after {fmt(3)}, furthest {s['far']:.1f} m, walked {walked:.1f} m, "
                  f"{stops} stops, {reversals} reversals")


if __name__ == "__main__":
    main(sys.argv[1:])
