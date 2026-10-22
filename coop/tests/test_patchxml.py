import unittest
import xml.etree.ElementTree as ET

from tlg_coop import patchxml

RES = """<?xml version='1.0' encoding='utf-8'?>
<Patch><TitleID><ID>CUSA03745</ID></TitleID>
<Metadata Title="The Last Guardian" Name="Resolution Patch: Native 1280x720" AppVer="01.03" isEnabled="true">
<PatchList><Line Type="bytes" Address="0x0104fa6a" Value="9090" /></PatchList></Metadata></Patch>"""


class PatchXmlTest(unittest.TestCase):
    def test_combined_keeps_resolution_and_adds_coop_once(self):
        text = patchxml.combine(patchxml.combine(RES))
        root = ET.fromstring(text)
        metas = root.findall("Metadata")
        self.assertEqual([m.get("Name") for m in metas],
                         ["Resolution Patch: Native 1280x720", patchxml.COOP_PATCH_NAME])
        coop = metas[1]
        self.assertEqual(coop.get("isEnabled"), "true")
        self.assertEqual(coop.get("AppVer"), "01.03")
        # shadPS4 reads the lines from the Metadata element's first child
        self.assertEqual(coop[0].tag, "PatchList")
        line = coop[0][0]
        self.assertEqual((line.get("Type"), line.get("Address"), line.get("Value")),
                         ("bytes", "0x010f808c", "e9030000"))

    def test_value_is_user_1001_little_endian(self):
        self.assertEqual(int.from_bytes(bytes.fromhex(patchxml.COOP_VALUE), "little"), 1001)
        self.assertEqual(int.from_bytes(bytes.fromhex(patchxml.COOP_ORIGINAL), "little", signed=True), -1)

    def test_rejects_other_xml(self):
        with self.assertRaises(ValueError):
            patchxml.combine("<NotAPatch/>")

    def test_coop_only(self):
        root = ET.fromstring(patchxml.coop_only())
        self.assertEqual(root.find("TitleID/ID").text, "CUSA03745")
        self.assertEqual(len(root.findall("Metadata")), 1)


if __name__ == "__main__":
    unittest.main()
