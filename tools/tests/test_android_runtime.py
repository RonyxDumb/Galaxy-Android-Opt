"""Execute production C++ with mocked Android/Vulkan services on the host.

NDK headers supply the real API signatures; no device, driver or game assets
are required. These tests establish logic behavior, not on-device performance.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


class NativeProbe(unittest.TestCase):
    def setUp(self):
        self.compiler = shutil.which("g++") or shutil.which("clang++")
        if not self.compiler:
            self.skipTest("host C++ compiler unavailable")
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def build(self, name, sources, ndk=False, preinclude=None):
        cmd = [self.compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
               "-I", str(HERE / "fakes"), "-I", str(ROOT / "platform"),
               "-I", str(ROOT / "platform/include"), "-I", str(ROOT / "platform/src"),
               "-I", str(ROOT / "decomp/libs/RVL_SDK/include")]
        if preinclude:
            cmd += ["-include", str(HERE / preinclude)]
        if ndk:
            ndk_root = os.environ.get("ANDROID_NDK_HOME") or os.environ.get("ANDROID_NDK_ROOT")
            headers = list(Path(ndk_root).glob("toolchains/llvm/prebuilt/*/sysroot/usr/include")) if ndk_root else []
            if not headers:
                self.skipTest("set ANDROID_NDK_HOME for the Android/Vulkan API headers")
            # System libc/C++ headers take precedence over Android's libc.
            cmd += ["-D__INTRODUCED_IN(x)=", "-D__DEPRECATED_IN(x)=", "-idirafter", str(headers[0])]
        probe = Path(self.tmp.name) / name
        cmd += [str(HERE / (name + ".cpp"))] + [str(ROOT / p) for p in sources] + ["-o", str(probe)]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return probe

    def run_scenarios(self, probe, scenarios):
        for scenario in scenarios:
            with self.subTest(scenario=scenario):
                result = subprocess.run([str(probe), scenario], capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_pacer(self):
        probe = self.build("runtime_pacer_probe", ["platform/mobile/display/FramePacer.cpp"])
        self.run_scenarios(probe, ["cadence", "interrupted", "stall"])

    def test_audio(self):
        probe = self.build("runtime_audio_probe", ["platform/src/audio/aaudio_out.cpp"], ndk=True,
                           preinclude="runtime_audio_compat.h")
        self.run_scenarios(probe, ["reconnect", "pause", "resume", "retry", "channels"])

    def test_gamepad(self):
        probe = self.build("runtime_gamepad_probe", ["platform/mobile/input/GamepadInput.cpp"], ndk=True)
        self.run_scenarios(probe, ["camera", "pointer", "reset"])

    def test_vulkan_submission(self):
        probe = self.build("runtime_vk_submit_probe", [], ndk=True)
        self.run_scenarios(probe, ["skipped-present", "failure"])


if __name__ == "__main__":
    unittest.main()
