import dataclasses
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tlg_coop import install, psarc

MAIN = (b"Load(\"game/scripts/common/ExitCallback\")\r\n"
        b"SetMaxFps(30)\r\n"
        b"if SysInfo.ps4 then Load(\"game/scripts/common/InitialPreload\") end\r\n")


class ApplyEditTest(unittest.TestCase):
    def edit(self, **kw):
        return dataclasses.replace(install.MAIN_EDIT, known_sha256=(install.sha256_bytes(MAIN),), **kw)

    def test_inserts_after_anchor_with_crlf(self):
        out = install.apply_edit(MAIN, self.edit())
        lines = out.decode().split("\r\n")
        i = lines.index('if SysInfo.ps4 then Load("game/scripts/common/InitialPreload") end')
        self.assertEqual(lines[i + 1], "-- tlg-coop")
        self.assertIn('pcall(Load, "game/scripts/coop/coop_main")', lines[i + 2])
        self.assertNotIn("\n", out.decode().replace("\r\n", ""))
        self.assertTrue(out.startswith(MAIN))

    def test_lf_files_stay_lf(self):
        src = MAIN.replace(b"\r\n", b"\n")
        edit = dataclasses.replace(self.edit(), known_sha256=(install.sha256_bytes(src),))
        out = install.apply_edit(src, edit)
        self.assertNotIn(b"\r", out)

    def test_anchor_without_trailing_newline(self):
        src = MAIN.rstrip(b"\r\n")
        edit = dataclasses.replace(self.edit(), known_sha256=(install.sha256_bytes(src),))
        out = install.apply_edit(src, edit)
        self.assertIn(b"end\r\n-- tlg-coop\r\n", out)

    def test_unknown_version_refused(self):
        with self.assertRaises(install.InstallError):
            install.apply_edit(MAIN + b"-- changed\r\n", self.edit())
        install.apply_edit(MAIN + b"-- changed\r\n", self.edit(), check_hash=False)

    def test_anchor_must_be_unique(self):
        doubled = MAIN + MAIN
        with self.assertRaises(install.InstallError):
            install.apply_edit(doubled, self.edit(), check_hash=False)
        with self.assertRaises(install.InstallError):
            install.apply_edit(b"nothing here\n", self.edit(), check_hash=False)

    def test_real_main_lua_hash_is_pinned(self):
        self.assertEqual(len(install.MAIN_EDIT.known_sha256), 1)
        self.assertRegex(install.MAIN_EDIT.known_sha256[0], r"^[0-9a-f]{64}$")


class InstallTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.game = root / "CUSA03745"
        self.update = root / "CUSA03745-UPDATE"
        self.mods = root / "CUSA03745-mods"
        self.game.mkdir()
        self.update.mkdir()
        psarc.create(str(self.game / "trico_boot.psarc"),
                     {"game/scripts/states/main.lua": MAIN, "game/x.lua": b"base x"},
                     patch_style=False, compress=True)
        psarc.create(str(self.update / "trico_bootp.psarc"),
                     {"game/x.lua": b"patched x", "game/y.lua": b"y"})
        self.lua = root / "lua"
        self.lua.mkdir()
        (self.lua / "coop_main.lua").write_text("-- main\n")
        (self.lua / "coop_debug.lua").write_text("-- debug\n")
        known = dataclasses.replace(install.MAIN_EDIT, known_sha256=(install.sha256_bytes(MAIN),))
        patcher = mock.patch.object(install, "EDITS", (known,))
        patcher.start()
        self.addCleanup(patcher.stop)

    def tearDown(self):
        self.tmp.cleanup()

    def contents(self):
        with psarc.Archive.open(str(self.mods / "trico_bootp.psarc")) as arc:
            return {n: arc.read(n) for n in arc.names}

    def test_install_builds_overlay_archive(self):
        result = install.install(self.game, self.lua)
        self.assertEqual(result["target"], str(self.mods / "trico_bootp.psarc"))
        files = self.contents()
        self.assertEqual(files["game/x.lua"], b"patched x")
        self.assertEqual(files["game/y.lua"], b"y")
        self.assertEqual(files["game/scripts/coop/coop_main.lua"], b"-- main\n")
        self.assertEqual(files["game/scripts/coop/coop_debug.lua"], b"-- debug\n")
        self.assertIn(b"coop_main", files["game/scripts/states/main.lua"])
        self.assertNotIn("game/x.lua", [n for n in files if n.startswith("game/scripts/coop")])
        marker = json.loads((self.mods / install.MARKER).read_text())
        self.assertEqual(marker["version"], install.MOD_VERSION)
        self.assertEqual(install.status(self.game, self.lua), {
            "installed": True, "version": install.MOD_VERSION, "stale": []})

    def test_update_folder_argument_works_too(self):
        install.install(self.update, self.lua)
        self.assertTrue((self.mods / "trico_bootp.psarc").exists())

    def test_reinstall_is_idempotent(self):
        install.install(self.game, self.lua)
        first = (self.mods / "trico_bootp.psarc").read_bytes()
        install.install(self.game, self.lua)
        self.assertEqual((self.mods / "trico_bootp.psarc").read_bytes(), first)

    def test_script_edit_in_update_archive_wins(self):
        psarc.create(str(self.update / "trico_bootp.psarc"),
                     {"game/scripts/states/main.lua": MAIN + b"-- 1.03\r\n"})
        with self.assertRaises(install.InstallError):
            install.install(self.game, self.lua)  # unknown hash for the newer script
        self.assertFalse(self.mods.exists())
        install.install(self.game, self.lua, check_hash=False)
        self.assertIn(b"-- 1.03", self.contents()["game/scripts/states/main.lua"])

    def test_status_reports_stale_scripts(self):
        install.install(self.game, self.lua)
        (self.lua / "coop_main.lua").write_text("-- main v2\n")
        self.assertIn("scripts changed", install.status(self.game, self.lua)["stale"])

    def test_foreign_archive_left_alone(self):
        self.mods.mkdir()
        (self.mods / "trico_bootp.psarc").write_bytes(b"someone else's")
        with self.assertRaises(install.InstallError):
            install.install(self.game, self.lua)
        with self.assertRaises(install.InstallError):
            install.uninstall(self.game)
        self.assertEqual((self.mods / "trico_bootp.psarc").read_bytes(), b"someone else's")
        self.assertEqual(install.status(self.game, self.lua),
                         {"installed": False, "foreign_archive": True})

    def test_uninstall_removes_only_our_files(self):
        install.install(self.game, self.lua)
        (self.mods / "other-mod.txt").write_text("keep")
        self.assertTrue(install.uninstall(self.game))
        self.assertEqual(sorted(p.name for p in self.mods.iterdir()), ["other-mod.txt"])
        self.assertFalse(install.uninstall(self.game))

    def test_uninstall_removes_empty_folder(self):
        install.install(self.game, self.lua)
        install.uninstall(self.game)
        self.assertFalse(self.mods.exists())

    def test_needs_update(self):
        (self.update / "trico_bootp.psarc").unlink()
        with self.assertRaises(install.InstallError):
            install.install(self.game, self.lua)

    def test_cli(self):
        with mock.patch("builtins.print"):
            self.assertEqual(install.main(["install", str(self.game), "--lua-dir", str(self.lua)]), 0)
            self.assertEqual(install.main(["status", str(self.game), "--lua-dir", str(self.lua)]), 0)
            self.assertEqual(install.main(["uninstall", str(self.game)]), 0)
            self.assertEqual(install.main(["install", str(self.game / "nope")]), 1)


class ShippedScriptsTest(unittest.TestCase):
    def test_shipped_lua_folder_has_entry_point(self):
        files = install.mod_scripts()
        self.assertIn("game/scripts/coop/coop_main.lua", files)
        for name in files:
            self.assertEqual(name, name.lower())


if __name__ == "__main__":
    unittest.main()
