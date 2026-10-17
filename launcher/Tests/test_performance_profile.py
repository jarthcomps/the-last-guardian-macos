"""Keep the shader-header optimization paired with precise readback correctness."""
import json
from pathlib import Path
import unittest

RESOURCES = Path(__file__).resolve().parents[1] / "Resources/defaults"


class PerformanceProfileTests(unittest.TestCase):
    def test_fast_header_reads_keep_precise_readbacks_and_feather_synchronization(self):
        defaults = json.loads((RESOURCES / "config.json").read_text())["GPU"]
        profile = json.loads(
            (RESOURCES / "custom_configs/game.readback.json").read_text()
        )["GPU"]
        effective = defaults | profile
        self.assertIs(effective["shader_backing_reads"], True)
        self.assertIs(effective["shader_backing_verify"], False)
        self.assertEqual(effective["readbacks_mode"], 2)
        self.assertIs(effective["readback_linear_images_enabled"], True)
        self.assertIs(effective["readback_async_fences_enabled"], True)
        self.assertIs(effective["readback_async_guest_faults"], True)
        self.assertEqual(effective["readback_fence_wait_shortcut"], 3)
        self.assertEqual(effective["cp_full_fence_kinds"], 31)
        self.assertEqual(effective["readback_prefetch_lifetime"], 60)

    def test_release_profile_has_no_readback_profiling_overhead(self):
        profile = json.loads(
            (RESOURCES / "custom_configs/game.readback.json").read_text()
        )["GPU"]
        self.assertIs(profile["readback_stats_enabled"], False)
        self.assertIs(profile["readback_stats_gpu_timing_enabled"], False)


if __name__ == "__main__":
    unittest.main()
