"""Execute production EFB copies on Vulkan and compare independently decoded bytes."""
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import unittest
from tools.tests import test_android_runtime as runtime


def expand(value, bits):
    code = value >> (8 - bits)
    return {3: lambda: (code << 5) | (code << 2) | (code >> 1),
            4: lambda: code * 17, 5: lambda: (code << 3) | (code >> 2),
            6: lambda: (code << 2) | (code >> 4)}[bits]()


def expected_pixel(c, fmt, intensity):
    r, g, b, a = c
    if intensity:
        r = (66 * r + 129 * g + 25 * b + 4096 + 128) // 256
    if fmt == 0: return [expand(r, 4)] * 4
    if fmt in (1, 8): return [r] * 4
    if fmt == 2: return [expand(r, 4)] * 3 + [expand(a, 4)]
    if fmt == 3: return [r] * 3 + [a]
    if fmt == 4: return [expand(r, 5), expand(g, 6), expand(b, 5), 255]
    if fmt == 5:
        return ([expand(v, 5) for v in (r, g, b)] + [255] if a >= 224 else
                [expand(v, 4) for v in (r, g, b)] + [expand(a, 3)])
    if fmt == 7: return [a] * 4
    if fmt == 9: return [g] * 4
    if fmt == 10: return [b] * 4
    if fmt == 11: return [r] * 3 + [g]
    if fmt == 12: return [g] * 3 + [b]
    return [r, g, b, a]


class EfbExecutionTests(unittest.TestCase):
    setUp = runtime.NativeProbe.setUp
    build = runtime.NativeProbe.build

    def test_formats_alpha_depth_intensity_and_downsampling(self):
        glslang = os.environ.get('GLSLANG_VALIDATOR') or shutil.which('glslangValidator')
        if not glslang:
            if os.environ.get('GALAXY_REQUIRE_SHADER_EXECUTION') == '1': self.fail('glslang required')
            self.skipTest('glslang required')
        emitter = self.build('efb_source_probe', ['platform/src/gx/gx_efb_shader.cpp'])
        runner = self.build('efb_execution_probe', [
            'platform/src/gx/vk/vk_efb_copy_dispatch.cpp', 'platform/src/gx/vk/vk_image_sets.cpp'],
            ndk=True, preinclude='runtime_audio_compat.h', extra_flags=[
                '-pthread', '-Wno-unused-parameter', '-Wno-missing-field-initializers',
                '-Wl,--no-as-needed', '-Wl,-l:libvulkan.so.1'])
        base = Path(self.tmp.name)
        source, spirv = base / 'copy.comp', base / 'copy.spv'
        source.write_text(subprocess.check_output([emitter], text=True))
        subprocess.run([glslang, '-V', str(source), '-o', str(spirv)], check=True, capture_output=True)
        colors = [[(17 + i * 13) % 256, (243 - i * 9) % 256, i * 15, (i % 4) * 80] for i in range(16)]
        depths = [0, 0.25, 0.5, 1] * 4
        (base / 'colors').write_bytes(bytes(v for c in colors for v in c))
        (base / 'depths').write_bytes(struct.pack('<16f', *depths))
        for pixel_format in range(4):
            for half in (False, True):
                prepared = []
                for c, z in zip(colors, depths):
                    if pixel_format == 3:
                        value = math.floor(z * 16777215 + 0.5)
                        prepared.append([(value >> 16) & 255, (value >> 8) & 255, value & 255, 255])
                    elif pixel_format == 1:
                        prepared.append([expand(v, 6) for v in c])
                    elif pixel_format == 2:
                        prepared.append([expand(c[0], 5), expand(c[1], 6), expand(c[2], 5), 255])
                    else:
                        prepared.append(c[:3] + [255])
                if half:
                    prepared = [[(sum(prepared[y * 4 + x + d][k] for d in (0, 1, 4, 5)) + 2) // 4
                                 for k in range(4)] for y in (0, 2) for x in (0, 2)]
                for fmt in range(13):
                    intensity = fmt < 4
                    ctrl = ((fmt & 7) << 4) | (fmt & 8) | (32768 if intensity else 0) | (512 if half else 0)
                    with self.subTest(pixel_format=pixel_format, half=half, fmt=fmt):
                        result = subprocess.run([runner, spirv, str(ctrl), str(pixel_format), base / 'colors',
                                                 base / 'depths', base / 'out'], capture_output=True, text=True, timeout=20)
                        if result.returncode == 77:
                            if os.environ.get('GALAXY_REQUIRE_SHADER_EXECUTION') == '1': self.fail('Vulkan unavailable')
                            self.skipTest('Vulkan unavailable')
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertNotIn('Validation Error', result.stdout + result.stderr)
                        if os.environ.get('GALAXY_REQUIRE_VK_VALIDATION'):
                            self.assertIn('Validation layer active', result.stderr)
                        want = bytes(v for c in prepared for v in expected_pixel(c, fmt, intensity))
                        self.assertEqual((base / 'out').read_bytes(), want)


if __name__ == '__main__': unittest.main()
