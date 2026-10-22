"""How the Trico camera follows his heading, from a probe's script log (CoopCamera.trace on).

Reads the "CF" follow lines on the "COOP t=" clock. The error is where the camera really is from
Trico (its look, "live") against straight behind him ("hd"). Each turn of his (the heading moving
TURN degrees or more from where it had settled) starts a settle; for each it prints how long the
error took to come under PASS degrees, whether it then stayed under for HOLD seconds (unless the
next turn came first), how often the error changed sign on the way, and the rod length and pitch
seen over the whole log. Usage:
python3 tools/camera_follow.py logs/coop-20261021/<tag>-coop.txt ...
"""
import re
import sys

CLOCK = re.compile(r"COOP t=([0-9.]+)")
FOLLOW = re.compile(r"CF hd=(-?[0-9.]+) h=(-?[0-9.]+) e=(-?[0-9.]+) live=(-?[0-9.]+) off=(-?\d+) "
                    r"len=(\S+) pitch=(\S+) basis=(\S+)")
TURN = 45.0
PASS = 15.0
HOLD = 5.0
WITHIN = 2.0    # seconds the pass criterion allows for a settle


def wrap(a):
    return (a + 180.0) % 360.0 - 180.0


def samples(lines):
    """(time, heading, live, offset, length, pitch) for every follow line with a live camera."""
    now, seen = None, {}
    for line in lines:
        m = CLOCK.search(line)
        if m:
            now = float(m.group(1))
            seen = {}
            continue
        m = FOLLOW.search(line)
        if not m or now is None:
            continue
        # several lines within one clock second: spread them evenly-ish by order
        n = seen.get(now, 0)
        seen[now] = n + 1
        yield (now + 0.1 * n, float(m.group(1)), float(m.group(4)), float(m.group(5)),
               m.group(6), m.group(7))


def settles(points):
    """One dict per turn of his: start, heading, time to PASS, held, sign changes."""
    out, cur, base = [], None, None
    for t, hd, live, off, _, _ in points:
        if off != 0:   # (the orbit's offset: not a follow measurement)
            continue
        err = wrap(live - hd)
        if base is None:
            base = hd
        if abs(wrap(hd - base)) >= TURN:
            if cur:
                out.append(cur)
            cur = {"start": t, "turn": wrap(hd - base), "reached": None, "held": None,
                   "signs": 0, "last": err, "broke": None}
            base = hd
        if cur is None:   # (before his first turn; small drift keeps the base)
            continue
        if cur["last"] is not None and err * cur["last"] < 0 and abs(err) > 1 and abs(cur["last"]) > 1:
            cur["signs"] += 1
        cur["last"] = err
        if cur["reached"] is None:
            if abs(err) < PASS:
                cur["reached"] = t - cur["start"]
        elif cur["held"] is None:
            if abs(err) >= PASS:
                cur["held"], cur["broke"] = False, t
            elif t - cur["start"] - cur["reached"] >= HOLD:
                cur["held"] = True
    if cur:
        out.append(cur)
    return out


def main(paths):
    for path in paths:
        with open(path, encoding="utf-8", errors="replace") as f:
            points = list(samples(f.read().splitlines()))
        print(path)
        if not points:
            print("  no follow lines (CoopCamera.trace off?)")
            continue
        errs = [abs(wrap(live - hd)) for _, hd, live, off, _, _ in points if off == 0]
        lengths = sorted({p[4] for p in points})
        pitches = sorted({p[5] for p in points})
        print(f"  {len(points)} samples, median |error| {sorted(errs)[len(errs) // 2]:.1f} deg, "
              f"lengths {lengths}, pitches {pitches}")
        for s in settles(points):
            reached = f"{s['reached']:.1f} s" if s["reached"] is not None else "never"
            if s["held"] is None:
                held = "next turn first"
            else:
                held = "held 5 s" if s["held"] else f"lost at t={s['broke']:.1f}"
            ok = s["reached"] is not None and s["reached"] <= WITHIN and s["held"] is not False and s["signs"] <= 2
            print(f"  turn {s['turn']:+.0f} deg at t={s['start']:.1f}: under {PASS:.0f} deg after {reached}, "
                  f"{held}, {s['signs']} sign changes -> {'pass' if ok else 'FAIL'}")


if __name__ == "__main__":
    main(sys.argv[1:])
