"""tools/camera_follow.py: settles after his turns from made-up follow lines."""
import importlib.util
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "camera_follow", Path(__file__).resolve().parent.parent / "tools" / "camera_follow.py")
cf = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(cf)


def log(points):
    """points: (t, heading, live): one clock line and one follow line each."""
    lines = []
    for t, hd, live in points:
        lines.append(f"[2] COOP t={t:.1f} f=1 on=true")
        lines.append(f"[2] CF hd={hd:.1f} h={live:.1f} e=0.0 live={live:.1f} off=0 len=14 pitch=-25 basis=Global")
    return lines


class CameraFollowTest(unittest.TestCase):
    def test_quarter_turn_settles(self):
        # behind him at 180, he turns to 90; the camera comes round in 1.5 s and stays
        pts = [(float(t), 180.0, 180.0) for t in range(3)]
        live = 180.0
        for i in range(30):
            live = max(90.0, live - 30.0)
            pts.append((3.0 + i * 0.5, 90.0, live))
        s = cf.settles(list(cf.samples(log(pts))))
        self.assertEqual(len(s), 1)
        self.assertAlmostEqual(s[0]["turn"], -90.0)
        self.assertLessEqual(s[0]["reached"], 2.0)
        self.assertTrue(s[0]["held"])
        self.assertEqual(s[0]["signs"], 0)

    def test_swinging_camera_fails(self):
        # across 180 / -180, and swinging either side of behind him
        pts = [(0.0, 179.0, -179.0), (1.0, 179.0, 179.0), (2.0, 120.0, 175.0)]
        for i in range(12):
            pts.append((3.0 + i, 120.0, 120.0 + (20.0 if i % 2 else -20.0)))
        s = cf.settles(list(cf.samples(log(pts))))
        self.assertEqual(len(s), 1)
        self.assertIsNone(s[0]["reached"])
        self.assertGreater(s[0]["signs"], 2)

    def test_main_prints_every_case(self):
        import contextlib, io, tempfile, os
        pts = [(0.0, 180.0, 180.0), (1.0, 90.0, 170.0), (2.0, 90.0, 95.0), (3.0, 90.0, 140.0)]
        with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
            f.write("\n".join(log(pts)))
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            cf.main([f.name])
        os.unlink(f.name)
        self.assertIn("lost at t=3.0", out.getvalue())

    def test_wrap(self):
        self.assertAlmostEqual(cf.wrap(190.0), -170.0)
        self.assertAlmostEqual(cf.wrap(-181.0), 179.0)


if __name__ == "__main__":
    unittest.main()
