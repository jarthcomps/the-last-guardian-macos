"""tools/steer_latency.py: holds, pace and the stop after release from a made-up script log."""
import importlib.util
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "steer_latency", Path(__file__).resolve().parent.parent / "tools" / "steer_latency.py")
steer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(steer)


def log(points):
    """points: (t, on, z, anim): one clock line and one TR report each."""
    lines = []
    for t, on, z, anim in points:
        lines.append(f"[2] COOP t={t:.1f} f=1 on=true")
        lines.append(f"[2] TR ctrl {'on' if on else 'off'} p=0.0,1.0,{z:.2f} g=- pm=ForwardMove an={anim}")
    return lines


class SteerLatencyTest(unittest.TestCase):
    def test_steady_hold(self):
        # 1 s standing, then 1.5 m/s for 6 s; let go, he slows over a second and stands
        pts = [(10.0, True, 0.0, "c_stand"), (10.5, True, 0.0, "c_stand")]
        pts += [(11.0 + i, True, 1.5 * i, "c_walk_fastC") for i in range(7)]
        pts += [(18.0, False, 9.5, "c_walk_fast_stop"), (19.0, False, 10.0, "c_stand"),
                (20.0, False, 10.0, "c_stand"), (21.0, False, 10.0, "c_stand")]
        held = list(steer.stretches(log(pts)))
        self.assertEqual(len(held), 1)
        s = held[0]
        self.assertAlmostEqual(s["held"], 8.0)
        speeds, mean, steady = steer.pace(s["samples"], s["start"])
        self.assertEqual(len(speeds), 6)
        self.assertAlmostEqual(mean, 1.5)
        self.assertEqual(steady, 1.0)
        self.assertEqual(s["anims"].most_common(1)[0][0], "c_walk_fastC")
        self.assertAlmostEqual(steer.settle(s["after"]), 1.0)
        self.assertAlmostEqual(s["striding"], 11.0)
        mean2, steady2 = steer.pace2(s["samples"], s["start"])
        self.assertAlmostEqual(mean2, 1.5)
        self.assertEqual(steady2, 1.0)

    def test_stop_start_is_unsteady(self):
        # alternating 2 m and 0.3 m seconds: the mean is fine, the pace is not
        z, pts = 0.0, []
        for i in range(9):
            pts.append((i * 1.0, True, z, "c_walkC"))
            z += 2.0 if i % 2 else 0.3
        pts.append((9.0, False, z, "c_stand"))
        s = next(steer.stretches(log(pts)))
        speeds, mean, steady = steer.pace(s["samples"], s["start"])
        self.assertLess(steady, 0.5)
        walked, stops, reversals = steer.motion(s["samples"])
        self.assertEqual(stops, 0)

    def test_no_stop_after_release(self):
        pts = [(0.0, True, 0.0, "c_walkC"), (1.0, True, 1.0, "c_walkC"),
               (2.0, False, 2.0, "c_walkC"), (3.0, False, 3.0, "c_walkC")]
        s = next(steer.stretches(log(pts)))
        self.assertIsNone(steer.settle(s["after"]))


if __name__ == "__main__":
    unittest.main()
