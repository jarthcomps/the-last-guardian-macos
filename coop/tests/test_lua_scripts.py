"""Runs the Lua tests in tests/lua under a Lua 5.1 interpreter: lua5.1, or LuaJIT, which speaks
5.1 too (skipped without either)."""
import shutil
import subprocess
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
LUA = shutil.which("lua5.1") or shutil.which("lua51") or shutil.which("luajit")


@unittest.skipUnless(LUA, "neither lua5.1 nor luajit is installed")
class LuaScriptsTest(unittest.TestCase):
    def test_lua_suites(self):
        suites = sorted((HERE / "lua").glob("test_*.lua"))
        self.assertTrue(suites)
        for suite in suites:
            with self.subTest(suite=suite.name):
                run = subprocess.run([LUA, str(suite), str(HERE.parent / "lua")],
                                     capture_output=True, text=True, timeout=60)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                self.assertTrue(run.stdout.strip().endswith("ok"), run.stdout)

    def test_every_script_compiles(self):
        luac = shutil.which("luac5.1")
        for script in sorted((HERE.parent / "lua").glob("*.lua")):
            with self.subTest(script=script.name):
                # luajit -b compiles to bytecode (here thrown away), like luac -p
                cmd = [luac, "-p", str(script)] if luac else [LUA, "-b", str(script), "/dev/null"]
                run = subprocess.run(cmd, capture_output=True, text=True)
                self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == "__main__":
    unittest.main()
