"""Install, remove and inspect the co-op mod for The Last Guardian (CUSA03745, update 1.03).

The mod is a rebuilt copy of the update's ``trico_bootp.psarc`` placed in the emulator's
``<game>-mods`` overlay folder. The copy keeps every shipped entry byte for byte and adds:

* the co-op scripts under ``game/scripts/coop/``, and
* ``game/scripts/states/main.lua`` with one line appended that loads them.

The game's own script is read from the user's archives at install time and edited at a fixed
anchor, so no game code is shipped with the mod. The edit is refused when the script does not
match a known version.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from dataclasses import dataclass
from pathlib import Path

from . import psarc

MOD_VERSION = "0.1.0"
MARKER = "tlg-coop.json"
PATCH_ARCHIVE = "trico_bootp.psarc"
BASE_ARCHIVE = "trico_boot.psarc"
LUA_DIR = Path(__file__).resolve().parent.parent / "lua"
SCRIPT_PREFIX = "game/scripts/coop/"


@dataclass(frozen=True)
class Edit:
    """Append ``insert`` after the single line ``anchor`` of a game script."""
    path: str
    anchor: str
    insert: str
    known_sha256: tuple[str, ...]


MAIN_EDIT = Edit(
    path="game/scripts/states/main.lua",
    anchor='if SysInfo.ps4 then Load("game/scripts/common/InitialPreload") end',
    insert='-- tlg-coop\nif not pcall(Load, "game/scripts/coop/coop_main") then CoopLoadFailed = true end\n',
    known_sha256=(
        # CUSA03745 base game (the update does not replace this script)
        "fa3da4c0b380fe0acc338e0ce90d9a8609ce245d788da2ad2019dcf705381a7f",
    ),
)
EDITS = (MAIN_EDIT,)


class InstallError(RuntimeError):
    pass


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path, chunk: int = 8 << 20) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        while True:
            buf = fh.read(chunk)
            if not buf:
                return h.hexdigest()
            h.update(buf)


def apply_edit(source: bytes, edit: Edit, check_hash: bool = True) -> bytes:
    """Return ``source`` with ``edit.insert`` placed on the line after the anchor."""
    if check_hash and sha256_bytes(source) not in edit.known_sha256:
        raise InstallError(f"{edit.path}: unknown version of the game script; refusing to edit it")
    text = source.decode("utf-8")
    lines = text.splitlines(keepends=True)
    hits = [i for i, line in enumerate(lines) if line.rstrip("\r\n") == edit.anchor]
    if len(hits) != 1:
        raise InstallError(f"{edit.path}: anchor found {len(hits)} times, expected once")
    i = hits[0]
    newline = "\r\n" if lines[i].endswith("\r\n") or (not lines[i].endswith("\n") and "\r\n" in text) else "\n"
    if not lines[i].endswith("\n"):
        lines[i] += newline
    lines.insert(i + 1, edit.insert.replace("\n", newline))
    return "".join(lines).encode("utf-8")


def mod_scripts(lua_dir: Path = LUA_DIR) -> dict[str, bytes]:
    files = {}
    for path in sorted(lua_dir.glob("*.lua")):
        files[SCRIPT_PREFIX + path.name.lower()] = path.read_bytes()
    if SCRIPT_PREFIX + "coop_main.lua" not in files:
        raise InstallError(f"{lua_dir}: coop_main.lua is missing")
    return files


@dataclass
class Layout:
    game: Path
    update: Path
    mods: Path

    @classmethod
    def from_game(cls, game: Path) -> "Layout":
        game = Path(game)
        if game.name.endswith("-UPDATE"):
            game = game.with_name(game.name[: -len("-UPDATE")])
        return cls(game, game.with_name(game.name + "-UPDATE"), game.with_name(game.name + "-mods"))

    @property
    def target(self) -> Path:
        return self.mods / PATCH_ARCHIVE

    @property
    def marker(self) -> Path:
        return self.mods / MARKER


def read_marker(layout: Layout) -> dict | None:
    try:
        return json.loads(layout.marker.read_text())
    except FileNotFoundError:
        return None


def read_game_script(layout: Layout, name: str) -> bytes:
    """A script as the game sees it: the update archive wins over the base archive."""
    for archive in (layout.update / PATCH_ARCHIVE, layout.game / BASE_ARCHIVE):
        if archive.is_file():
            with psarc.Archive.open(str(archive)) as arc:
                data = arc.read(name)
            if data is not None:
                return data
    raise InstallError(f"{name} was not found in the game or update archives")


def build_replacements(layout: Layout, lua_dir: Path = LUA_DIR, check_hash: bool = True
                       ) -> dict[str, bytes]:
    replacements = mod_scripts(lua_dir)
    for edit in EDITS:
        replacements[edit.path] = apply_edit(read_game_script(layout, edit.path), edit, check_hash)
    return replacements


def install(game: Path, lua_dir: Path = LUA_DIR, check_hash: bool = True, progress=None) -> dict:
    layout = Layout.from_game(game)
    source = layout.update / PATCH_ARCHIVE
    if not source.is_file():
        raise InstallError(f"{source} is missing: the co-op mod needs update 1.03")
    if layout.target.exists() and read_marker(layout) is None:
        raise InstallError(f"{layout.target} belongs to another mod; remove it first")
    replacements = build_replacements(layout, lua_dir, check_hash)
    layout.mods.mkdir(exist_ok=True)
    with psarc.Archive.open(str(source)) as src:
        result = psarc.write_patched(src, replacements, str(layout.target), progress)
    stat = source.stat()
    marker = {
        "mod": "tlg-coop",
        "version": MOD_VERSION,
        "source": {"path": str(source), "size": stat.st_size, "mtime": int(stat.st_mtime)},
        "scripts": {name: sha256_bytes(data) for name, data in sorted(replacements.items())},
        "archive_size": layout.target.stat().st_size,
    }
    layout.marker.write_text(json.dumps(marker, indent=2) + "\n")
    return {**result, "target": str(layout.target)}


def uninstall(game: Path) -> bool:
    layout = Layout.from_game(game)
    if read_marker(layout) is None:
        if layout.target.exists():
            raise InstallError(f"{layout.target} was not installed by the co-op mod; left alone")
        return False
    if layout.target.exists():
        layout.target.unlink()
    layout.marker.unlink()
    try:
        layout.mods.rmdir()
    except OSError:
        pass  # other mods still live there
    return True


def status(game: Path, lua_dir: Path = LUA_DIR) -> dict:
    layout = Layout.from_game(game)
    marker = read_marker(layout)
    if marker is None:
        return {"installed": False, "foreign_archive": layout.target.exists()}
    source = layout.update / PATCH_ARCHIVE
    stale = []
    if not layout.target.exists():
        stale.append("archive missing")
    elif layout.target.stat().st_size != marker.get("archive_size"):
        stale.append("archive changed")
    if source.exists():
        st = source.stat()
        if st.st_size != marker["source"]["size"] or int(st.st_mtime) != marker["source"]["mtime"]:
            stale.append("update archive changed")
    try:
        current = {n: sha256_bytes(d) for n, d in mod_scripts(lua_dir).items()}
        installed = {n: h for n, h in marker["scripts"].items() if n.startswith(SCRIPT_PREFIX)}
        if current != installed:
            stale.append("scripts changed")
    except InstallError:
        pass
    if marker.get("version") != MOD_VERSION:
        stale.append("older mod version")
    return {"installed": True, "version": marker.get("version"), "stale": stale}


def main(argv=None) -> int:
    p = argparse.ArgumentParser(prog="tlg_coop", description=__doc__.splitlines()[0])
    p.add_argument("command", choices=("install", "uninstall", "status"))
    p.add_argument("game", type=Path, help="the game folder (CUSA03745), next to CUSA03745-UPDATE")
    p.add_argument("--lua-dir", type=Path, default=LUA_DIR)
    p.add_argument("--ignore-script-version", action="store_true",
                   help="edit main.lua even when its hash is unknown (anchor still required)")
    args = p.parse_args(argv)
    try:
        if args.command == "install":
            result = install(args.game, args.lua_dir, not args.ignore_script_version,
                             psarc.copy_progress_printer("copying"))
        elif args.command == "uninstall":
            result = {"removed": uninstall(args.game)}
        else:
            result = status(args.game, args.lua_dir)
    except (InstallError, OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
