import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
SOURCE = REPO_ROOT / "tools" / "probe_vulkan_driver.cpp"
VULKAN_HEADERS = REPO_ROOT / "externals" / "vulkan-headers" / "include"


class VulkanDriverProbeTest(unittest.TestCase):
    def test_capability_decision_self_test(self):
        compiler = shutil.which("clang++") or shutil.which("clang")
        if not compiler:
            self.fail("clang/clang++ is required for the standalone probe test")
        with tempfile.TemporaryDirectory(prefix="shadps4-vulkan-probe-") as tmp:
            binary = Path(tmp) / "probe_vulkan_driver"
            command = [
                compiler,
                "-std=c++20",
                "-O2",
                "-arch",
                "x86_64",
                "-I",
                str(VULKAN_HEADERS),
                str(SOURCE),
                "-o",
                str(binary),
            ]
            subprocess.run(command, cwd=REPO_ROOT, check=True, capture_output=True, text=True)
            completed = subprocess.run(
                [str(binary), "--self-test"],
                cwd=REPO_ROOT,
                check=True,
                capture_output=True,
                text=True,
            )
            result = json.loads(completed.stdout)
            self.assertTrue(result["selfTest"])
            self.assertTrue(result["passed"])
            self.assertTrue(result["positive"])
            self.assertEqual(
                set(result["missing"]),
                {
                    "apiVersion",
                    "swapchain",
                    "pushDescriptor",
                    "vertexAttributeDivisor",
                    "robustness2",
                    "robustBufferAccess2",
                    "robustImageAccess2",
                    "nullDescriptor",
                },
            )
            self.assertTrue(all(result["missing"].values()))
            self.assertEqual(result["jsonEscape"], 'probe "quote" \\\n')


if __name__ == "__main__":
    unittest.main()
