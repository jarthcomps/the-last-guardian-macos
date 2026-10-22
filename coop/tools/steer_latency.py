"""How Trico answers the stick in a probe's script log.

For each stretch of "TR ... on" reports (the stick held), prints how long Trico took to move 1 m
and 3 m from where he stood when the stick went out, how far he got, the distance walked along
the way, and how often he stopped (moved less than 0.2 m over a second after having moved) or
reversed (a second's motion against the previous one), using the "COOP t=" clock lines.

Then his pace: the speed for each whole second of the hold, the mean after the first second, the
share of those seconds within 20% of the mean, his animations (share of reports), and how long
after the stick was let go he stood still (a second with less than 0.2 m). Usage:
python3 tools/steer_latency.py logs/coop-20261021/<tag>-coop.txt ...
"""
import math
import re
import sys
from collections import Counter

CLOCK = re.compile(r"COOP t=([0-9.]+)")
STEER = re.compile(r"TR (\w+) (on|off) p=(-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+)")
ANIM = re.compile(r" an=(\S+)")
STRIDE = re.compile(r"c_walk(_slow|_fast)?[A-D]$")   # a walk cycle (not setting off or stopping)
SETTLE_WINDOW = 6.0   # seconds after release looked at for his stop
STILL = 0.2           # metres in a second that count as standing


def reports(lines):
    """(time, on, x, z, anim) for every TR report, on the COOP clock."""
    now = None
    for line in lines:
        m = CLOCK.search(line)
        if m:
            now = float(m.group(1))
            continue
        m = STEER.search(line)
        if not m or now is None:
            continue
        a = ANIM.search(line)
        yield now, m.group(2) == "on", float(m.group(3)), float(m.group(5)), a.group(1) if a else "?"


def stretches(lines):
    every = list(reports(lines))
    cur = None
    for i, (now, on, x, z, anim) in enumerate(every):
        if on and cur is None:
            cur = {"start": now, "origin": (x, z), "marks": {}, "far": 0.0,
                   "samples": [(now, x, z)], "anims": Counter([anim])}
        elif on:
            d = math.hypot(x - cur["origin"][0], z - cur["origin"][1])
            cur["far"] = max(cur["far"], d)
            for metres in (1, 3):
                if d >= metres and metres not in cur["marks"]:
                    cur["marks"][metres] = now - cur["start"]
            cur["samples"].append((now, x, z))
            cur["anims"][anim] += 1
            if cur.get("striding") is None and STRIDE.match(anim):
                cur["striding"] = now
        elif cur is not None:
            cur["held"] = now - cur["start"]
            cur["after"] = [(t, ax, az) for t, aon, ax, az, _ in every[i:]
                            if t - now <= SETTLE_WINDOW and not aon]
            yield cur
            cur = None
    if cur is not None:
        cur["held"] = (every[-1][0] if every else cur["start"]) - cur["start"]
        cur["after"] = []
        yield cur


def per_second(samples):
    """The first position in each whole second of the clock, in order."""
    seconds = {}
    for t, x, z in samples:
        seconds.setdefault(int(t), (t, x, z))
    return [seconds[k] for k in sorted(seconds)]


def motion(samples):
    """Path length, stops and reversals from per-second steps."""
    points = [(x, z) for _, x, z in per_second(samples)]
    path, stops, reversals, moved, last = 0.0, 0, 0, False, None
    for (x0, z0), (x1, z1) in zip(points, points[1:]):
        dx, dz = x1 - x0, z1 - z0
        step = math.hypot(dx, dz)
        if step > 20:   # a bogus position, not a step
            continue
        path += step
        if step < STILL:
            if moved:
                stops += 1
            moved = False
            continue
        if last and (dx * last[0] + dz * last[1]) / (step * math.hypot(*last)) < -0.5:
            reversals += 1
        moved, last = True, (dx, dz)
    return path, stops, reversals


def pace(samples, start, skip=1.0):
    """Speeds (m/s) between whole-second samples from `skip` s into the hold, their mean, and the
    share within 20% of the mean."""
    points = [p for p in per_second(samples) if p[0] - start >= skip]
    speeds = []
    for (t0, x0, z0), (t1, x1, z1) in zip(points, points[1:]):
        step = math.hypot(x1 - x0, z1 - z0)
        if t1 > t0 and step <= 20:
            speeds.append(step / (t1 - t0))
    if not speeds:
        return speeds, None, None
    mean = sum(speeds) / len(speeds)
    steady = sum(1 for v in speeds if abs(v - mean) <= 0.2 * mean) / len(speeds) if mean > 0 else 0.0
    return speeds, mean, steady


def pace2(samples, start, skip=1.0):
    """The mean and the share of 2 s windows within 20% of it (the position reports come in
    bursts, so single seconds swing about a steady gait)."""
    points = [p for p in per_second(samples) if p[0] - start >= skip]
    speeds = []
    for (t0, x0, z0), (t1, x1, z1) in zip(points, points[2:]):
        step = math.hypot(x1 - x0, z1 - z0)
        if t1 > t0 and step <= 40:
            speeds.append(step / (t1 - t0))
    if not speeds:
        return None
    mean = sum(speeds) / len(speeds)
    if mean <= 0:
        return mean, 0.0
    return mean, sum(1 for v in speeds if abs(v - mean) <= 0.2 * mean) / len(speeds)


def settle(after):
    """Seconds from release until a second in which he moved less than STILL, or None."""
    points = per_second(after)
    if not points:
        return None
    released = after[0][0]
    for (t0, x0, z0), (t1, x1, z1) in zip(points, points[1:]):
        if math.hypot(x1 - x0, z1 - z0) < STILL:
            return t0 - released
    return None


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
            speeds, mean, steady = pace(s["samples"], s["start"])
            if mean is not None:
                print(f"    pace after 1 s: mean {mean:.2f} m/s, {steady * 100:.0f}% of seconds within 20%: "
                      + " ".join(f"{v:.2f}" for v in speeds))
            w2 = pace2(s["samples"], s["start"])
            if w2:
                print(f"    over 2 s windows: {w2[1] * 100:.0f}% within 20% of {w2[0]:.2f} m/s")
            if s.get("striding") is not None:
                speeds, mean, steady = pace(s["samples"], s["striding"], skip=0.0)
                if mean is not None:
                    print(f"    pace once into his stride (t={s['striding']:.1f} s): mean {mean:.2f} m/s, "
                          f"{steady * 100:.0f}% within 20%")
            total = sum(s["anims"].values())
            print("    anims: " + ", ".join(f"{a} {n * 100 / total:.0f}%" for a, n in s["anims"].most_common(6)))
            stood = settle(s["after"])
            print(f"    stood still {stood:.1f} s after release" if stood is not None
                  else "    no stop seen after release")


if __name__ == "__main__":
    main(sys.argv[1:])
