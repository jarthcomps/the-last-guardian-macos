"""Whether Trico stayed on walkable ground while the stick was held, from a probe's script log.

For each stretch of "TR ... on" reports: how many "TG off" reports (not over his navmesh) came
while it was held, the furthest he went below the ground last seen under him ("TG on ... dy="
is his height over the nearest ground), the edge distances the reach reported ("TE ... edge="),
and whether he ended standing over his navmesh. Pass: no TG off, never more than DROP metres
below, ended on ground. Usage:
python3 tools/edge_check.py logs/coop-20261021/<tag>-coop.txt ...
"""
import re
import sys

CLOCK = re.compile(r"COOP t=([0-9.]+)")
STEER = re.compile(r"TR \w+ (on|off) p=(-?[0-9.]+),(-?[0-9.]+),(-?[0-9.]+)")
GROUND = re.compile(r"TG (on xz=(-?[0-9.]+) dy=(-?[0-9.]+)|off)")
EDGE = re.compile(r"TE .*edge=(\S+) why=(\S+)")
DROP = 1.0


def holds(lines):
    now, cur, out = None, None, []
    for line in lines:
        m = CLOCK.search(line)
        if m:
            now = float(m.group(1))
            continue
        m = GROUND.search(line)
        if m and cur is not None:
            if m.group(1) == "off":
                cur["off"] += 1
                cur["last_on"] = False
            else:
                dy = float(m.group(3))
                cur["lowest"] = min(cur["lowest"], dy)
                cur["last_on"] = True
            continue
        m = EDGE.search(line)
        if m and cur is not None:
            if m.group(1) != "-":
                cur["edges"].append(float(m.group(1)))
            continue
        m = STEER.search(line)
        if not m or now is None:
            continue
        if m.group(1) == "on" and cur is None:
            cur = {"start": now, "off": 0, "lowest": 0.0, "last_on": None, "edges": [],
                   "from": (float(m.group(2)), float(m.group(4)))}
        elif m.group(1) == "on":
            cur["to"] = (float(m.group(2)), float(m.group(4)))
        elif cur is not None:
            cur["held"] = now - cur["start"]
            out.append(cur)
            cur = None
    if cur is not None:
        cur["held"] = (now or cur["start"]) - cur["start"]
        out.append(cur)
    return out


def verdict(h):
    return h["off"] == 0 and h["lowest"] >= -DROP and h["last_on"] is not False


def main(paths):
    for path in paths:
        with open(path, encoding="utf-8", errors="replace") as f:
            hs = holds(f.read().splitlines())
        print(path)
        for h in hs:
            fx, fz = h["from"]
            tx, tz = h.get("to", h["from"])
            moved = ((tx - fx) ** 2 + (tz - fz) ** 2) ** 0.5
            edges = h["edges"]
            near = min(edges) if edges else None
            print(f"  stick at t={h['start']:.1f} s for {h['held']:.1f} s: moved {moved:.1f} m, "
                  f"{h['off']} off-mesh, lowest {h['lowest']:+.2f} m, "
                  f"nearest edge {near if near is not None else '-'} m, "
                  f"open in {sum(1 for e in edges if e >= 12) * 100 // max(1, len(edges))}% "
                  f"-> {'pass' if verdict(h) else 'FAIL'}")


if __name__ == "__main__":
    main(sys.argv[1:])
