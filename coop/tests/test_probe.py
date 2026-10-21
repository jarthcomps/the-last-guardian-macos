import shutil
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


def make_save(folder: Path, data: bytes) -> Path:
    (folder / "sce_sdmemory/sce_sys").mkdir(parents=True)
    (folder / "sce_sdmemory/memory.dat").write_bytes(data)
    (folder / "sce_sdmemory/sce_sys/param.sfo").write_bytes(b"sfo-" + data)
    return folder


def tree(folder: Path) -> dict:
    return {str(f.relative_to(folder)): (f.read_bytes(), f.stat().st_mtime_ns)
            for f in sorted(folder.rglob("*")) if f.is_file()}


class SaveSwapTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.save = make_save(self.root / "saves/main" / probe.TITLE, b"main")
        self.targets = probe.save_targets(self.root, pool=False)
        for name, target in self.targets.items():
            make_save(target, name.encode())
        self.before = {name: tree(t) for name, t in self.targets.items()}
        self.backup = self.root / "saves/runtime-backup-t"

    def tearDown(self):
        self.tmp.cleanup()

    def test_targets_are_the_runner_snapshot_and_the_private_runtime(self):
        self.assertEqual(self.targets["snapshot"], self.root / probe.SNAPSHOTS[False] /
                         "1000/savedata/CUSA03745")
        self.assertEqual(self.targets["run"], self.root / probe.RUNTIME /
                         "user/home/1000/savedata/CUSA03745")
        self.assertEqual(probe.save_targets(self.root, pool=True)["snapshot"],
                         self.root / probe.SNAPSHOTS[True] / "1000/savedata/CUSA03745")

    def test_save_is_in_place_during_the_run_and_originals_come_back(self):
        with probe.swapped_save(self.save.parent, self.targets, self.backup):
            for target in self.targets.values():
                self.assertEqual((target / "sce_sdmemory/memory.dat").read_bytes(), b"main")
            self.assertEqual((self.backup / "snapshot/CUSA03745/sce_sdmemory/memory.dat")
                             .read_bytes(), b"snapshot")
            # the game writes to its copy; the source save must not change
            (self.targets["run"] / "sce_sdmemory/memory.dat").write_bytes(b"autosaved")
        self.assertEqual({n: tree(t) for n, t in self.targets.items()}, self.before)
        self.assertEqual((self.save / "sce_sdmemory/memory.dat").read_bytes(), b"main")
        self.assertFalse(self.backup.exists())

    def test_originals_come_back_when_the_run_fails(self):
        with self.assertRaises(KeyboardInterrupt):
            with probe.swapped_save(self.save, self.targets, self.backup):
                raise KeyboardInterrupt
        self.assertEqual({n: tree(t) for n, t in self.targets.items()}, self.before)
        self.assertFalse(self.backup.exists())

    def test_a_missing_original_stays_missing(self):
        shutil.rmtree(self.targets["run"])
        with probe.swapped_save(self.save, self.targets, self.backup):
            self.assertTrue((self.targets["run"] / "sce_sdmemory/memory.dat").is_file())
        self.assertFalse(self.targets["run"].exists())
        self.assertEqual(tree(self.targets["snapshot"]), self.before["snapshot"])

    def test_refuses_a_non_save_folder_and_an_existing_backup(self):
        with self.assertRaises(ValueError):
            with probe.swapped_save(self.root / "saves", self.targets, self.backup):
                pass
        self.backup.mkdir(parents=True)
        with self.assertRaises(RuntimeError):
            with probe.swapped_save(self.save, self.targets, self.backup):
                pass
        self.assertEqual({n: tree(t) for n, t in self.targets.items()}, self.before)


if __name__ == "__main__":
    unittest.main()
