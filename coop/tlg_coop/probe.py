"""Run one guarded, muted game session with the co-op mod and decode what the scripts reported.

Uses the private test runtime (logs/checkpoint-performance-20261017/run) and the shared runner
tools/guardian_outdoor_experiments.py, so the SOTC guard, muting and owned-child rules all
apply. The mod is installed into the private runtime's CUSA03745-mods only for the run and
removed afterwards; the user's own game folders are never touched.

    python3 -m tlg_coop.probe TAG [--seconds 40] [--input "20:button cross 1"] [--no-pad2]
        [--engine PATH --coop] [--strategy interest|forward|slide]

The probe always installs the scripts with the debug channel on (its own coop_config.lua).
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from . import debuglog, install, patchxml

ROOT = Path(os.environ.get("TLG_ROOT", Path.home() / "Games/Ps4"))
RUNTIME = "logs/checkpoint-performance-20261017/run"
OUTPUT = "logs/coop-20261021"
ENGINE = "logs/plan-20261018/engine-release-117"
DRIVER = "logs/plan-20261018/driver-release-117.dylib"
LOG_FILTER = "*:Warning Lib.Pad:Debug Loader:Info"


def engine_sha256(engine: Path) -> str:
    sidecar = engine.with_name(engine.name + ".sha256")
    if sidecar.is_file():
        return sidecar.read_text().split()[0]
    return install.sha256_file(engine)


def probe_config(strategy: str) -> str:
    return ("-- tlg-coop settings written by tlg_coop.probe\n"
            "CoopDebug.enabled = true\n"
            f"CoopTrico.strategy = \"{strategy}\"\n")


def probe_lua_dir(dest: Path, strategy: str, lua_dir: Path = install.LUA_DIR) -> Path:
    """A copy of the scripts with the probe's coop_config.lua."""
    for script in lua_dir.glob("*.lua"):
        shutil.copy2(script, dest / script.name)
    (dest / "coop_config.lua").write_text(probe_config(strategy))
    return dest


def runner_command(root: Path, tag: str, seconds: float, patch: Path, inputs: list[str],
                   extra: list[str], engine: Path | None = None, coop: bool = False) -> list[str]:
    engine = engine or root / ENGINE
    sha = engine_sha256(engine)
    cmd = [sys.executable, str(root / "tools/guardian_outdoor_experiments.py"), tag, "combined",
           "--seconds", str(seconds), "--driver", str(root / DRIVER), "--engine", str(engine),
           "--engine-sha256", sha, "--output-root", str(root / OUTPUT),
           "--patch-file", str(patch), "--game-config", f"Log.filter={LOG_FILTER}"]
    for step in inputs:
        cmd += ["--input", step]
    if coop:
        cmd += ["--env", "SHADPS4_TLG_COOP=1"]
    return cmd + list(extra)


def summarize(log_paths: list[Path]) -> dict:
    lines: list[str] = []
    for path in log_paths:
        if path.is_file():
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
            if any(debuglog.LINE.search(line) for line in lines):
                break
    messages = debuglog.decode(lines)
    return {
        "pad_opens": debuglog.pad_opens(lines),
        "mods_folder_seen": any("Files found in game mods folder" in line for line in lines),
        "emulator_coop": [line.split("TLG co-op: ", 1)[1].strip() for line in lines
                          if "TLG co-op: " in line],
        "messages": [f"[{h}] {m}" for h, m in messages],
    }


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("tag")
    p.add_argument("--seconds", type=float, default=40)
    p.add_argument("--input", action="append", default=[])
    p.add_argument("--no-pad2", action="store_true", help="leave out the second-pad patch line")
    p.add_argument("--runner-arg", action="append", default=[],
                   help="extra argument passed to guardian_outdoor_experiments.py")
    p.add_argument("--engine", type=Path, help="emulator build to run (default: release 117)")
    p.add_argument("--coop", action="store_true", help="turn on the emulator's co-op mode")
    p.add_argument("--strategy", default="interest", choices=("interest", "forward", "slide"),
                   help="how the scripts steer Trico")
    args = p.parse_args(argv)

    root = ROOT
    runtime = root / RUNTIME
    out_dir = root / OUTPUT
    out_dir.mkdir(parents=True, exist_ok=True)
    patches = runtime / "user/patches"
    resolution = (patches / "Guardian-720p.xml").read_text(encoding="utf-8")
    patch = patches / ("Guardian-720p.xml" if args.no_pad2 else "Guardian-720p-coop.xml")
    if not args.no_pad2:
        patch.write_text(patchxml.combine(resolution), encoding="utf-8")

    game = runtime / "CUSA03745"
    engine = args.engine if args.engine is None or args.engine.is_absolute() else root / args.engine
    with tempfile.TemporaryDirectory() as tmp:
        lua_dir = probe_lua_dir(Path(tmp), args.strategy)
        print(json.dumps({"install": install.install(game, lua_dir)}), flush=True)
    try:
        cmd = runner_command(root, args.tag, args.seconds, patch, args.input, args.runner_arg,
                             engine, args.coop)
        print("running:", " ".join(cmd), flush=True)
        code = subprocess.call(cmd, cwd=root)
    finally:
        install.uninstall(game)
        print("co-op mod removed from the private runtime", flush=True)
    shad_log = runtime / "user/log/shad_log.txt"
    kept = out_dir / f"{args.tag}-shad_log.txt"
    if shad_log.is_file():
        shutil.copy2(shad_log, kept)
    summary = summarize([out_dir / f"{args.tag}.log", kept])
    summary["runner_exit"] = code
    (out_dir / f"{args.tag}-coop.json").write_text(json.dumps(summary, indent=2) + "\n")
    text = "\n".join(summary["messages"]) + "\n"
    (out_dir / f"{args.tag}-coop.txt").write_text(text)
    print(json.dumps({k: v for k, v in summary.items() if k != "messages"}, indent=2))
    print(f"{len(summary['messages'])} script messages in {out_dir / (args.tag + '-coop.txt')}")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
