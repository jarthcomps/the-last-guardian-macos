import os
import tempfile
import unittest
from pathlib import Path

from tlg_coop import psarc


def names_and_data(path):
    with psarc.Archive.open(str(path)) as arc:
        return {name: arc.read(name) for name in arc.names}


class PsarcTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def fixture(self, files, **kwargs):
        path = self.dir / "src.psarc"
        psarc.create(str(path), files, **kwargs)
        return path

    def test_round_trip_patch_style(self):
        files = {"game/a.lua": b"print(1)\n", "game/b.bin": os.urandom(70000), "game/empty": b""}
        path = self.fixture(files, block_size=0x10000)
        with psarc.Archive.open(str(path)) as arc:
            self.assertEqual(arc.name_separator, b"\0")
            self.assertEqual(sorted(arc.names), sorted(files))
            for name, data in files.items():
                self.assertEqual(arc.read(name), data)
            self.assertIsNone(arc.read("game/missing"))

    def test_round_trip_compressed_base_style(self):
        files = {"z/text.lua": b"local x = 1\n" * 20000, "a/rand": os.urandom(5000)}
        path = self.fixture(files, patch_style=False, compress=True)
        with psarc.Archive.open(str(path)) as arc:
            self.assertEqual(arc.name_separator, b"\n")
            self.assertEqual(arc.names, list(files))  # base archives keep their own order
            self.assertLess(os.path.getsize(path), 100000)
            for name, data in files.items():
                self.assertEqual(arc.read(name), data)

    def test_lookup_ignores_case(self):
        path = self.fixture({"game/scripts/common/delayexec.lua": b"x"})
        with psarc.Archive.open(str(path)) as arc:
            self.assertEqual(arc.read("game/scripts/common/DelayExec.lua"), b"x")

    def test_replace_and_add_keep_other_entries(self):
        files = {f"f/{i:03}.dat": os.urandom(1000 + i) for i in range(20)}
        src = self.fixture(files, block_size=0x400)
        out = self.dir / "out.psarc"
        new = {"f/005.dat": b"replaced", "new/coop.lua": b"-- coop\n" * 300}
        with psarc.Archive.open(str(src)) as arc:
            result = psarc.write_patched(arc, new, str(out))
        got = names_and_data(out)
        expected = dict(files, **new)
        self.assertEqual(got, expected)
        with psarc.Archive.open(str(out)) as arc:
            self.assertEqual(arc.names, sorted(arc.names), "patch manifests stay sorted")
            md5s = [e.md5 for e in arc.entries[1:]]
            self.assertEqual(md5s, sorted(md5s), "patch TOC stays sorted by MD5")
            self.assertEqual(len(arc.entries), len(expected) + 1)
        # untouched data is copied byte for byte
        raw_src = src.read_bytes()
        with psarc.Archive.open(str(src)) as arc:
            start = arc.data_start()
        self.assertIn(raw_src[start:], out.read_bytes())

    def test_toc_growth_shifts_data(self):
        files = {"a": b"1", "b": b"2"}
        src = self.fixture(files, block_size=0x10000)
        new = {f"n/{i:04}": bytes([i % 256]) * 3 for i in range(3000)}
        out = self.dir / "out.psarc"
        with psarc.Archive.open(str(src)) as arc:
            result = psarc.write_patched(arc, new, str(out))
        self.assertGreater(result["shift"], 0)
        self.assertEqual(names_and_data(out), dict(files, **new))

    def test_writing_twice_from_source_is_identical(self):
        src = self.fixture({"a": b"1"})
        outs = []
        for i in range(2):
            out = self.dir / f"o{i}.psarc"
            with psarc.Archive.open(str(src)) as arc:
                psarc.write_patched(arc, {"b": b"2", "a": b"3"}, str(out))
            outs.append(out.read_bytes())
        self.assertEqual(outs[0], outs[1])
        self.assertFalse(any(p.name.endswith(".partial") for p in self.dir.iterdir()))

    def test_rejects_other_files(self):
        bad = self.dir / "bad.psarc"
        bad.write_bytes(b"NOPE" + b"\0" * 64)
        with self.assertRaises(ValueError):
            psarc.Archive.open(str(bad))

    def test_progress_reports_whole_copy(self):
        src = self.fixture({"a": os.urandom(100000)})
        seen = []
        with psarc.Archive.open(str(src)) as arc:
            psarc.write_patched(arc, {"b": b"2"}, str(self.dir / "o.psarc"),
                                progress=lambda done, total: seen.append((done, total)))
        self.assertTrue(seen)
        self.assertEqual(seen[-1][0], seen[-1][1])


if __name__ == "__main__":
    unittest.main()
