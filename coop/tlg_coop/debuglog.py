"""Decode the co-op scripts' messages from an emulator log.

The scripts send text three characters at a time as light bar colours (see coop_debug.lua).
shadPS4 logs each call at Debug level when the log filter contains ``Lib.Pad:Debug``:

    [Lib.Pad] <Debug> pad.cpp:573 scePadSetLightBar: called handle = 2 rgb = 67 79 79
"""
from __future__ import annotations

import re
import sys
from collections import defaultdict

HELLO = (0xC0, 0x0F, 0xEE)  # the scripts' hello to the emulator (coop_input.lua), not text
LINE = re.compile(r"scePadSetLightBar: called handle = (-?\d+) rgb = (\d+) (\d+) (\d+)")
OPEN = re.compile(r"scePadOpen: called user_id = (-?\d+), type = (\d+), index = (\d+), "
                  r"player index = (-?\d+), out handle = (-?\d+)")


def decode(lines) -> list[tuple[int, str]]:
    """(handle, message) pairs in the order their last character arrived."""
    partial: dict[int, list[str]] = defaultdict(list)
    out: list[tuple[int, str]] = []
    for line in lines:
        m = LINE.search(line)
        if not m:
            continue
        handle = int(m.group(1))
        if tuple(int(v) for v in m.group(2, 3, 4)) == HELLO:
            continue
        for value in m.group(2, 3, 4):
            ch = chr(int(value) & 0xFF)
            if ch == "\n":
                out.append((handle, "".join(partial[handle]).rstrip(" ")))
                partial[handle] = []
            else:
                partial[handle].append(ch)
    return out


def pad_opens(lines) -> list[dict]:
    opens = []
    for line in lines:
        m = OPEN.search(line)
        if m:
            user, typ, index, player, handle = (int(g) for g in m.groups())
            opens.append({"user_id": user, "type": typ, "index": index,
                          "player_index": player, "handle": handle})
    return opens


def main(argv=None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    if not argv:
        print("usage: python3 -m tlg_coop.debuglog LOGFILE...", file=sys.stderr)
        return 2
    for path in argv:
        with open(path, encoding="utf-8", errors="replace") as fh:
            lines = fh.readlines()
        for info in pad_opens(lines):
            print(f"# pad open: {info}")
        for handle, message in decode(lines):
            print(f"[{handle}] {message}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
