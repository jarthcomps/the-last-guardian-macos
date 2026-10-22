"""How quickly Trico answers the stick in a probe's script log.

For each stretch of "TR ... on" reports (the stick held), prints how long Trico took to move 1 m
and 3 m from where he stood when the stick went out, using the "COOP t=" clock lines. Usage:
python3 tools/steer_latency.py logs/coop-20261021/<tag>-coop.txt ...
"""
import math
import re
import sys

CLOCK = re.compile(r"COOP t=([0-9.]+)")
STEER = re.compile(r"TR (\w+) (on|off) p=(-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+)")


def stretches(lines):
    now, start, origin, marks = None, None, None, {}
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
        if on and start is None:
            start, origin, marks = now, (x, z), {}
        elif on:
            d = math.hypot(x - origin[0], z - origin[1])
            for metres in (1, 3):
                if d >= metres and metres not in marks:
                    marks[metres] = now - start
        elif start is not None:
            yield start, now - start, marks
            start = None
    if start is not None:
        yield start, now - start, marks


def main(paths):
    for path in paths:
        with open(path, encoding="utf-8", errors="replace") as f:
            lines = f.read().splitlines()
        print(path)
        for start, held, marks in stretches(lines):
            def fmt(m):
                return f"{marks[m]:.1f} s" if m in marks else "never"
            print(f"  stick at t={start:.1f} s for {held:.1f} s: 1 m after {fmt(1)}, 3 m after {fmt(3)}")


if __name__ == "__main__":
    main(sys.argv[1:])
