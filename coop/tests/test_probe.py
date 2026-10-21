import tempfile
import unittest
from pathlib import Path

from tlg_coop import probe


class ProbeTest(unittest.TestCase):
    def test_runner_command_uses_release_engine_patch_and_pad_logging(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "logs/plan-20261018").mkdir(parents=True)
            (root / (probe.ENGINE + ".sha256")).write_text("abc123  engine\n")
            cmd = probe.runner_command(root, "t1", 30, root / "p.xml", ["5:screenshot"], ["--pool"])
        self.assertEqual(cmd[2:4], ["t1", "combined"])
        self.assertEqual(cmd[cmd.index("--engine-sha256") + 1], "abc123")
        self.assertEqual(cmd[cmd.index("--patch-file") + 1], str(root / "p.xml"))
        self.assertIn("Log.filter=*:Warning Lib.Pad:Debug Loader:Info", cmd)
        self.assertEqual(cmd[cmd.index("--input") + 1], "5:screenshot")
        self.assertEqual(cmd[-1], "--pool")

    def test_runner_command_with_another_engine_and_coop_mode(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            engine = root / "shadps4"
            engine.write_bytes(b"engine")
            cmd = probe.runner_command(root, "t2", 30, root / "p.xml", [], [], engine, coop=True)
        self.assertEqual(cmd[cmd.index("--engine") + 1], str(engine))
        self.assertEqual(cmd[cmd.index("--engine-sha256") + 1],
                         probe.install.sha256_bytes(b"engine"))
        self.assertEqual(cmd[cmd.index("--env") + 1], "SHADPS4_TLG_COOP=1")

    def test_probe_lua_dir_turns_debug_on_and_picks_strategy(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = probe.probe_lua_dir(Path(tmp), "forward")
            config = (out / "coop_config.lua").read_text()
            self.assertTrue((out / "coop_main.lua").is_file())
        self.assertIn("CoopDebug.enabled = true", config)
        self.assertIn('CoopTrico.strategy = "forward"', config)

    def test_summary_prefers_log_with_messages(self):
        with tempfile.TemporaryDirectory() as tmp:
            a, b = Path(tmp) / "a.log", Path(tmp) / "b.log"
            a.write_text("nothing\n")
            rgb = " ".join(str(ord(c)) for c in "hi\n")
            b.write_text("[Loader] <Info> emulator.cpp:547 Run: Files found in game mods folder\n"
                         f"[Lib.Pad] <Debug> pad.cpp:573 scePadSetLightBar: called handle = 2 rgb = {rgb}\n")
            summary = probe.summarize([a, b, Path(tmp) / "missing"])
        self.assertEqual(summary["messages"], ["[2] hi"])
        self.assertEqual(summary["emulator_coop"], [])
        self.assertTrue(summary["mods_folder_seen"])


if __name__ == "__main__":
    unittest.main()
