import unittest
from tools.tests import test_android_runtime as runtime


class GraphicsPreparationTests(unittest.TestCase):
    setUp = runtime.NativeProbe.setUp
    build = runtime.NativeProbe.build
    run_scenarios = runtime.NativeProbe.run_scenarios

    def test_exact_pipelines_and_inventory(self):
        sources = [
            "platform/src/gx/vk/vk_shader_worker.cpp",
            "platform/src/gx/vk/vk_pipeline_manager.cpp",
            "platform/src/gx/vk_shader_inventory.cpp",
            "platform/src/gx/vk_shader_inventory_diag.cpp",
            "platform/src/gx/vk/vk_shader_inventory_persist.cpp",
            "platform/src/gx/vk_shader_manifest.cpp",
            "platform/src/gx/vk_shader_catalog.cpp",
        ]
        probe = self.build("runtime_graphics_probe", sources, ndk=True,
                           preinclude="runtime_audio_compat.h",
                           extra_flags=["-pthread", "-Wno-unused-parameter"])
        self.run_scenarios(probe, ["deduplicate", "shutdown", "failure", "pause", "inventory"])


if __name__ == "__main__":
    unittest.main()
