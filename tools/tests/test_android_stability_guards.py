"""Static regression guards for the native Android launcher.

These fast checks do not replace an NDK build or on-device tests.
"""
import pathlib
import unittest
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[2]
ANDROID_NS = "{http://schemas.android.com/apk/res/android}"


class AndroidStabilityGuards(unittest.TestCase):
    def test_native_game_is_in_own_process(self):
        manifest = ET.parse(ROOT / "platform/mobile/AndroidManifest.xml").getroot()
        activities = manifest.findall("./application/activity")
        game = next(
            a for a in activities
            if a.get(ANDROID_NS + "name") == "com.openport.galaxyandroid.GameActivity"
        )
        self.assertEqual(game.get(ANDROID_NS + "process"), ":game")
        self.assertEqual(game.get(ANDROID_NS + "exported"), "false")

    def test_launcher_never_forcibly_overwrites_existing_mappings(self):
        source = (ROOT / "platform/android/launcher.c").read_text()
        self.assertIn("rangePreReserved", source)
        self.assertIn("MAP_FIXED_NOREPLACE", source)
        self.assertNotIn("MAP_FIXED, -1, 0", source)

    def test_frame_pacer_never_spins_waiting_for_deadline(self):
        source = (ROOT / "platform/mobile/display/FramePacer.cpp").read_text()
        self.assertIn("clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME", source)
        self.assertNotIn("sched_yield()", source)
        self.assertNotIn('asm volatile("yield"', source)

    def test_vulkan_stall_telemetry(self):
        render = (ROOT / "platform/src/gx/vk/vk_render.cpp").read_text()
        present = (ROOT / "platform/src/gx/vk/vk_texture_presenter.cpp").read_text()
        self.assertIn("Vulkan render queue idle stall", render)
        self.assertIn("Vulkan swapchain image acquire stall", present)


if __name__ == "__main__":
    unittest.main()
