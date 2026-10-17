"""Opt-in integration test using the player's local PKG; writes only to a temporary folder.

TLG_TEST_PKG=... TLG_TEST_PKG_TOOL=... TLG_TEST_REFERENCE=... python3 -m unittest ...
No game data is included in this repository.
"""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest


@unittest.skipUnless(os.environ.get("TLG_TEST_PKG"), "requires a local test PKG")
class RealPKGProgressTest(unittest.TestCase):
    def test_live_byte_reports_and_extracted_files(self):
        tool = os.environ["TLG_TEST_PKG_TOOL"]
        pkg = os.environ["TLG_TEST_PKG"]
        reference = Path(os.environ["TLG_TEST_REFERENCE"])
        with tempfile.TemporaryDirectory(prefix="guardian-pkg-") as output:
            launcher = os.environ.get("TLG_TEST_LAUNCHER")
            command = [launcher, "--install-pkg", pkg, output] if launcher else [tool, pkg, output]
            process = subprocess.Popen(command, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, text=True, bufsize=1)
            reports = []
            completed = False
            try:
                for line in process.stdout:
                    if line.startswith("TLG_BYTES "):
                        _, done, total = line.split()
                        done, total = int(done), int(total)
                        reports.append((done, total, time.monotonic()))
                        self.assertLessEqual(done, total)
                        if 0 < done < total:
                            self.assertIsNone(process.poll(), "progress arrived only after exit")
                    if line.startswith("Extraction complete."):
                        completed = True
                self.assertEqual(process.wait(timeout=10), 0)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
                process.stdout.close()
            self.assertTrue(completed)
            self.assertGreater(len(reports), 3)
            self.assertEqual(reports[0][0], 0)
            self.assertEqual(reports[-1][0], reports[-1][1])
            self.assertEqual(len({r[1] for r in reports}), 1)
            self.assertEqual([r[0] for r in reports], sorted(r[0] for r in reports))
            self.assertGreater(reports[-1][2] - reports[1][2], 0.25)

            actual = next(Path(output).glob("CUSA*"))
            files = lambda root: {p.relative_to(root): p for p in root.rglob("*") if p.is_file()}
            expected_files, actual_files = files(reference), files(actual)
            self.assertEqual(actual_files.keys(), expected_files.keys())
            def digest(path):
                with path.open("rb") as stream:
                    result = hashlib.sha256()
                    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                        result.update(chunk)
                    return result.digest()
            for relative, path in actual_files.items():
                self.assertEqual(path.stat().st_size, expected_files[relative].stat().st_size, str(relative))
                self.assertEqual(digest(path), digest(expected_files[relative]), str(relative))
            print(f"Verified {len(reports)} live reports, {reports[-1][1]:,} bytes, "
                  f"{len(actual_files)} matching files")


if __name__ == "__main__":
    unittest.main()
