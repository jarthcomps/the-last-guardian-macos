"""tools/edge_check.py: on-ground verdicts from made-up holds."""
import importlib.util
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "edge_check", Path(__file__).resolve().parent.parent / "tools" / "edge_check.py")
ec = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ec)


def log(rows):
    """rows: (t, on, z, ground) where ground is a dy or None for off the mesh."""
    lines = []
    for t, on, z, ground in rows:
        lines.append(f"[2] COOP t={t:.1f} f=1")
        lines.append("[2] TG off" if ground is None else f"[2] TG on xz=0.00 dy={ground:.2f}")
        lines.append(f"[2] TR ctrl {'on' if on else 'off'} p=0.0,5.0,{z:.1f} g=- an=c_walkC")
        lines.append(f"[2] TE reach=9.0 fit=0 back=30.4 edge={max(0.0, 6.0 - z):.1f} why=reach")
    return lines


class EdgeCheckTest(unittest.TestCase):
    def test_stops_at_the_edge(self):
        rows = [(float(t), True, min(t, 4.5), -0.1) for t in range(8)] + [(8.0, False, 4.5, -0.1)]
        h = ec.holds(log(rows))
        self.assertEqual(len(h), 1)
        self.assertTrue(ec.verdict(h[0]))
        self.assertEqual(min(h[0]["edges"]), 1.5)

    def test_walks_off(self):
        rows = [(float(t), True, t, -0.1 if t < 6 else None) for t in range(8)] + [(8.0, False, 8.0, -4.0)]
        h = ec.holds(log(rows))
        self.assertFalse(ec.verdict(h[0]))
        self.assertGreater(h[0]["off"], 0)

    def test_drops_below_the_ground(self):
        rows = [(0.0, True, 0.0, -0.1), (1.0, True, 1.0, -1.6), (2.0, False, 1.0, -1.6)]
        self.assertFalse(ec.verdict(ec.holds(log(rows))[0]))


if __name__ == "__main__":
    unittest.main()
