"""Run one guarded, muted game session with the co-op mod and decode what the scripts reported.

Uses the private test runtime (logs/checkpoint-performance-20261017/run) and the shared runner
tools/guardian_outdoor_experiments.py, so the SOTC guard, muting and owned-child rules all
apply. The mod is installed into the private runtime's CUSA03745-mods only for the run and
removed afterwards; the user's own game folders are never touched.

    python3 -m tlg_coop.probe TAG [--seconds 40] [--input "20:button cross 1"] [--no-pad2]
        [--engine PATH --coop] [--strategy goal|steer|interest|forward] [--pulse aimhold|both|aim|hold|emblem|command] [--camera SET] [--save DIR]

The probe always installs the scripts with the debug channel on (its own coop_config.lua).

--save DIR plays another save (DIR is a CUSA03745 save folder, or a folder holding one). The
runner rebuilds the private runtime's home from a snapshot on every start, so the save is put in
both places: the snapshot (what the game loads) and the runtime. The originals are moved aside to
logs/coop-20261021/saves/runtime-backup-<tag>/ and moved back afterwards, untouched; both folders
are shared with the performance runners.
"""
from __future__ import annotations

import argparse
import contextlib
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
TITLE = "CUSA03745"
SAVE = "1000/savedata/" + TITLE    # inside a user home
SNAPSHOTS = {False: "logs/live-profile-20261017/autosave-snapshot/home",   # as the runner picks them
             True: "logs/checkpoint-performance-20261017/snapshot-user/home"}


def engine_sha256(engine: Path) -> str:
    sidecar = engine.with_name(engine.name + ".sha256")
    if sidecar.is_file():
        return sidecar.read_text().split()[0]
    return install.sha256_file(engine)


def lua_list(names: str) -> str:
    return "{ " + ", ".join(f'"{n.strip()}"' for n in names.split(",") if n.strip()) + " }"


def probe_config(strategy: str, pulse: str = "aimhold", camera: str | None = None,
                 camera_dump: str | None = None, settings: list[str] | None = None) -> str:
    text = ("-- tlg-coop settings written by tlg_coop.probe\n"
            "CoopDebug.enabled = true\n"
            f"CoopTrico.strategy = \"{strategy}\"\n"
            f"CoopPulse.method = \"{pulse}\"\n")
    if camera and "," in camera:
        text += f"CoopCamera.cycle = {lua_list(camera)}\n"
    elif camera:
        text += f"CoopCamera.tricoParam = \"{camera}\"\n"
    if camera_dump:
        text += f"CoopCamera.dumpSets = {lua_list(camera_dump)}\n"
    for line in settings or []:
        text += line + "\n"
    return text


def probe_lua_dir(dest: Path, strategy: str, lua_dir: Path = install.LUA_DIR,
                  pulse: str = "aimhold", camera: str | None = None,
                  camera_dump: str | None = None, settings: list[str] | None = None) -> Path:
    """A copy of the scripts with the probe's coop_config.lua."""
    for script in lua_dir.glob("*.lua"):
        shutil.copy2(script, dest / script.name)
    (dest / "coop_config.lua").write_text(probe_config(strategy, pulse, camera, camera_dump,
                                                       settings))
    return dest


def save_source(save: Path) -> Path:
    """The CUSA03745 folder of a --save argument."""
    if (save / TITLE).is_dir():
        save = save / TITLE
    if not (save / "sce_sdmemory").is_dir():
        raise ValueError(f"{save} is not a {TITLE} save folder (no sce_sdmemory)")
    return save


def save_targets(root: Path, pool: bool) -> dict[str, Path]:
    """The save folders the probe replaces: the runner's snapshot and the private runtime's."""
    return {"snapshot": root / SNAPSHOTS[pool] / SAVE, "run": root / RUNTIME / "user/home" / SAVE}


@contextlib.contextmanager
def swapped_save(save: Path, targets: dict[str, Path], backup: Path):
    """Put a copy of `save` at every target; afterwards put the originals back as they were."""
    if backup.exists():
        raise RuntimeError(f"{backup} already exists; refusing to mix up save backups")
    source = save_source(save)
    moved: dict[str, Path] = {}
    try:
        for name, target in targets.items():
            if target.exists():
                aside = backup / name / TITLE
                aside.parent.mkdir(parents=True)
                target.rename(aside)
                moved[name] = aside
            shutil.copytree(source, target, symlinks=True)
        yield
    finally:
        for name, target in targets.items():
            if target.exists():
                shutil.rmtree(target)
            if name in moved:
                moved[name].rename(target)
                moved[name].parent.rmdir()
        if backup.exists():
            backup.rmdir()


def runner_command(root: Path, tag: str, seconds: float, patch: Path, inputs: list[str],
                   extra: list[str], engine: Path | None = None, coop: bool = False,
                   one_per_sample: bool = True) -> list[str]:
    engine = engine or root / ENGINE
    sha = engine_sha256(engine)
    cmd = [sys.executable, str(root / "tools/guardian_outdoor_experiments.py"), tag, "combined",
           "--seconds", str(seconds), "--driver", str(root / DRIVER), "--engine", str(engine),
           "--engine-sha256", sha, "--output-root", str(root / OUTPUT),
           "--patch-file", str(patch), "--game-config", f"Log.filter={LOG_FILTER}"]
    for step in inputs:
        cmd += ["--input", step]
    if inputs and one_per_sample:
        # a press and its release never in the same frame (missed swaps otherwise)
        cmd += ["--input-one-per-sample"]
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
    p.add_argument("--strategy", default="goal", choices=("goal", "steer", "interest", "forward"),
                   help="how the scripts steer Trico")
    p.add_argument("--pulse", default="aimhold",
                   choices=("aimhold", "both", "aim", "hold", "emblem", "command"),
                   help="how the scripts fire Trico's lightning")
    p.add_argument("--input-all-at-once", action="store_true",
                   help="send every due input step in the same metrics sample (the runner's default)")
    p.add_argument("--camera", metavar="SET",
                   help="game camera parameter set for the Trico framing (e.g. TricoAutoRunTest_near); "
                        "a comma-separated list cycles through them on each Select")
    p.add_argument("--camera-dump", metavar="SETS",
                   help="comma-separated camera parameter sets to print once the game camera is up")
    p.add_argument("--set", action="append", default=[], metavar="LUA",
                   help='an extra line for the probe\'s coop_config.lua, e.g. "CoopSwapFx.arcTime = 3"')
    p.add_argument("--save", type=Path, help="play this save (copied in, originals put back)")
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
        lua_dir = probe_lua_dir(Path(tmp), args.strategy, pulse=args.pulse, camera=args.camera,
                                camera_dump=args.camera_dump, settings=args.set)
        print(json.dumps({"install": install.install(game, lua_dir)}), flush=True)
    save = None
    if args.save:
        save = args.save if args.save.is_absolute() else root / args.save
        save_source(save)
    try:
        cmd = runner_command(root, args.tag, args.seconds, patch, args.input, args.runner_arg,
                             engine, args.coop, one_per_sample=not args.input_all_at_once)
        with contextlib.ExitStack() as stack:
            if save:
                backup = out_dir / "saves" / f"runtime-backup-{args.tag}"
                stack.enter_context(swapped_save(save, save_targets(root, "--pool" in args.runner_arg),
                                                 backup))
                print(f"playing save {save}; originals kept in {backup} for the run", flush=True)
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
