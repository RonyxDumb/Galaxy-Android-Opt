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
            "platform/src/gx/vk_shader_startup.cpp",
            "platform/src/gx/vk_shader_inventory_diag.cpp",
            "platform/src/gx/vk/vk_shader_inventory_persist.cpp",
            "platform/src/gx/vk_shader_manifest.cpp",
            "platform/src/gx/vk_shader_catalog.cpp",
        ]
        probe = self.build("runtime_graphics_probe", sources, ndk=True,
                           preinclude="runtime_audio_compat.h",
                           extra_flags=["-pthread", "-Wno-unused-parameter"])
        self.run_scenarios(probe, ["deduplicate", "shutdown", "failure", "pause", "inventory", "startup"])

    def test_copy_clears_full_source_after_downsampling(self):
        probe = self.build("runtime_copy_probe", ["platform/src/gx/vk/vk_copy_dispatch.cpp"],
                           ndk=True, preinclude="runtime_audio_compat.h",
                           extra_flags=["-Wno-unused-parameter"])
        self.run_scenarios(probe, ["full", "half", "clipped", "no-clear"])

    def test_shader_cache_has_a_resident_and_disk_budget(self):
        probe = self.build("runtime_shader_cache_probe", ["platform/src/gx/vk_shadergen.cpp"],
                           ndk=True, preinclude="runtime_audio_compat.h",
                           extra_flags=["-pthread", "-Wno-unused-parameter"])
        self.run_scenarios(probe, ["budget"])


if __name__ == "__main__":
    unittest.main()
