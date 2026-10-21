"""Game patch XML for co-op: the resolution patch plus the line that opens the second pad.

The Last Guardian creates two pad objects but gives the second one the user id -1, so it never
opens it. Changing that constant to 1001 (the emulator's second user) makes the game open and
read pad 2 itself, which fills the scripts' ``Input.pad2``.

eboot 1.03: ``mov dword [rbx+0x1c], 0xffffffff`` (c7 43 1c ff ff ff ff) at image offset 0xcf8089
(shadPS4 patch address 0x10f8089); the immediate starts three bytes later.
"""
from __future__ import annotations

import sys
import xml.etree.ElementTree as ET

COOP_PATCH_NAME = "Co-op: open the second controller"
COOP_ADDRESS = "0x010f808c"
COOP_ORIGINAL = "ffffffff"
COOP_VALUE = "e9030000"  # 1001, little endian
APP_VER = "01.03"


def coop_metadata(title: str = "The Last Guardian") -> ET.Element:
    meta = ET.Element("Metadata", {
        "Title": title, "Name": COOP_PATCH_NAME, "Note": "Second player (tlg-coop)",
        "Author": "tlg-coop", "PatchVer": "1.0", "AppVer": APP_VER, "AppElf": "eboot.bin",
        "isEnabled": "true"})
    plist = ET.SubElement(meta, "PatchList")
    ET.SubElement(plist, "Line", {"Type": "bytes", "Address": COOP_ADDRESS, "Value": COOP_VALUE})
    return meta


def combine(resolution_xml: str) -> str:
    """The resolution patch XML text with the co-op block added (once)."""
    root = ET.fromstring(resolution_xml)
    if root.tag != "Patch":
        raise ValueError("not a shadPS4 patch file")
    for meta in root.findall("Metadata"):
        if meta.get("Name") == COOP_PATCH_NAME:
            root.remove(meta)
    title = next((m.get("Title") for m in root.findall("Metadata") if m.get("Title")),
                 "The Last Guardian")
    root.append(coop_metadata(title))
    return "<?xml version='1.0' encoding='utf-8'?>\n" + ET.tostring(root, encoding="unicode") + "\n"


def coop_only() -> str:
    root = ET.Element("Patch")
    ids = ET.SubElement(root, "TitleID")
    ET.SubElement(ids, "ID").text = "CUSA03745"
    root.append(coop_metadata())
    return "<?xml version='1.0' encoding='utf-8'?>\n" + ET.tostring(root, encoding="unicode") + "\n"


def main(argv=None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 2:
        print("usage: python3 -m tlg_coop.patchxml RESOLUTION_PATCH.xml OUT.xml", file=sys.stderr)
        return 2
    with open(argv[0], encoding="utf-8") as fh:
        text = combine(fh.read())
    with open(argv[1], "w", encoding="utf-8") as fh:
        fh.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
