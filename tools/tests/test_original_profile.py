import unittest
from tools.tests import test_android_runtime as runtime


class OriginalProfileTests(unittest.TestCase):
    setUp = runtime.NativeProbe.setUp
    build = runtime.NativeProbe.build
    run_scenarios = runtime.NativeProbe.run_scenarios

    def test_original_profile_and_touch_controls(self):
        sources = ["platform/mobile/ui/TouchInputHandler.cpp",
                   "platform/mobile/ui/TouchInputHandlerPointers.cpp",
                   "platform/mobile/ui/Settings.cpp", "platform/mobile/ui/LayoutManager.cpp",
                   "platform/mobile/display/ViewportManager.cpp"]
        probe = self.build("runtime_touch_probe", sources, ndk=True,
                           extra_flags=["-Wno-unused-parameter", "-Wno-unused-variable"])
        self.run_scenarios(probe, ["profile", "fixed-stick", "pointer-camera", "dpad", "multitouch", "buttons"])


if __name__ == '__main__':
    unittest.main()
