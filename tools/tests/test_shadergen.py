"""Deterministic checks on the generated GX vertex shaders.

The generator is plain string building (platform/src/gx/vk_shadergen_*.cpp), so
the part that decides what a lit vertex computes can be checked on the host: a
small probe is compiled against the real generator and the emitted GLSL is
inspected.  No GPU and no Android toolchain are involved.

Skipped, not silently passed, when no C++ compiler is available.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
GX = ROOT / 'platform' / 'src' / 'gx'
PROBE_SRC = Path(__file__).resolve().parent / 'shadergen_probe.cpp'
GENERATOR_SRCS = [GX / name for name in (
    'vk_shadergen_vs.cpp', 'vk_shadergen_fs.cpp', 'vk_shadergen_tables.cpp',
    'vk_shadergen_lighting.cpp', 'vk_shadergen_texgen.cpp', 'gx_shader_uid.cpp', 'gx_builder_state.cpp')]

# XF channel control (GXLight.c: GXSetChanCtrl).  Bit 1 enables the channel,
# bit 2 includes GX_LIGHT0, bits 7-8 are the diffuse function, bit 9 is
# "attenuation enable" and bit 10 is "attenuation select".
ENABLE = 1 << 1
LIGHT0 = 1 << 2
DIFFUSE_CLAMP = 2 << 7
ATTEN_ENABLE = 1 << 9
ATTEN_SELECT = 1 << 10

AF_NONE = ENABLE | LIGHT0 | DIFFUSE_CLAMP
AF_SPEC = AF_NONE | ATTEN_ENABLE
AF_SPOT = AF_NONE | ATTEN_ENABLE | ATTEN_SELECT
UNLIT = LIGHT0 | DIFFUSE_CLAMP  # channel not enabled: material colour only


def find_compiler():
    for name in ('g++', 'clang++', 'c++'):
        path = shutil.which(name)
        if path:
            return path
    return None


@unittest.skipUnless(all(s.is_file() for s in GENERATOR_SRCS) and PROBE_SRC.is_file(), 'shader generator sources not present')
class ShaderGeneratorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = find_compiler()
        if not cls.compiler:
            raise unittest.SkipTest('no C++ compiler on PATH (c++, g++ or clang++)')
        cls.tmp = tempfile.TemporaryDirectory()
        cls.probe = Path(cls.tmp.name) / 'shadergen_probe'
        cmd = [cls.compiler, '-std=c++17', '-pthread', '-I', str(ROOT / 'platform' / 'src'), '-I', str(GX), '-I', str(ROOT / 'platform' / 'include'),
               str(PROBE_SRC)] + [str(s) for s in GENERATOR_SRCS] + ['-o', str(cls.probe)]
        built = subprocess.run(cmd, capture_output=True, text=True)
        if built.returncode != 0:
            cls.tmp.cleanup()
            raise AssertionError('the shader generator no longer builds on the host:\n' + built.stderr)

    @classmethod
    def tearDownClass(cls):
        if getattr(cls, 'tmp', None):
            cls.tmp.cleanup()

    def shader(self, ctrl, alpha=None):
        cmd = [str(self.probe), hex(ctrl)] + ([hex(alpha)] if alpha is not None else [])
        out = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stderr)
        return out.stdout

    def fragment_shader(self, ctrl=0, alpha=None, alpha_func=0):
        cmd = [str(self.probe), '--fs', hex(ctrl), hex(alpha if alpha is not None else ctrl), hex(alpha_func)]
        out = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stderr)
        return out.stdout

    def test_af_none_does_not_attenuate(self):
        """GX_AF_NONE leaves lighting unattenuated."""
        src = self.shader(AF_NONE)
        self.assertIn('float a = 1.0;', src)
        self.assertNotIn('float angle =', src)

    def test_concurrent_generation_is_deterministic(self):
        result = subprocess.run([str(self.probe), '--threads'], capture_output=True, text=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_recorded_light_position_and_zero_matrix_indices_are_preserved(self):
        result = subprocess.run([str(self.probe), '--state'], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_af_spec_attenuates_specular(self):
        """GX_AF_SPEC computes distance/specular attenuation."""
        src = self.shader(AF_SPEC)
        self.assertIn('dot(nrm, lvec) >= 0.0', src)
        self.assertIn('dot(nrm, fetch_st(b + 4u).xyz)', src)
        self.assertIn('dot(da, angles)', src)

    def test_af_spot_still_attenuates(self):
        """Spot and distance attenuation keep working for GX_AF_SPOT/GX_AF_DIST."""
        src = self.shader(AF_SPOT)
        self.assertIn('dot(lvec, fetch_st(b + 4u).xyz)', src)
        self.assertIn('dot(da, vec3(1.0, dist, d2))', src)

    def test_diffuse_is_never_gated_on_the_specular_angle(self):
        """AF_NONE and AF_SPOT do not gate on the specular angle."""
        for ctrl in (AF_NONE, AF_SPOT):
            with self.subTest(ctrl=hex(ctrl)):
                self.assertIn('illum = max(0.0, dot(nrm, lvec));', self.shader(ctrl))

    def test_disabled_channel_falls_back_to_the_material_colour(self):
        src = self.shader(UNLIT)
        self.assertIn('v_clr0.rgb = matColor[0].rgb;', src)
        self.assertNotIn('lit = ambColor', src)

    def test_second_channel_is_zero_when_only_one_is_enabled(self):
        """XF exposes only numChans channels to TEV."""
        src = self.shader(AF_SPEC)
        self.assertIn('v_clr1 = vec4(0.0);', src)
        self.assertNotIn('v_clr1.rgb = matColor[1].rgb;', src)

    def test_mario_eye_alpha_wrap_and_alpha_test(self):
        """Mario's eye and eyelid materials rely on 8-bit alpha wrap semantics (int(uint(a) & 255u))."""
        # alpha_func = 6 | (7 << 3) (GX_GEQUAL on comp0 with ref0, GX_ALWAYS on comp1, AND logic)
        fs = self.fragment_shader(ctrl=UNLIT, alpha_func=(6 | (7 << 3)))
        self.assertIn('int(uint(', fs)
        self.assertIn('& 255u)', fs)
        self.assertNotIn('clamp(ivec4(', fs)
        self.assertIn('float a = float(outi.a);', fs)
        self.assertIn('(a >= alphaRef.x)', fs)

    def test_tev_intermediate_inputs_and_final_rgb_wrap(self):
        """Signed 11-bit registers feed A/B/C through their low eight bits."""
        fs = self.fragment_shader()
        for value in ('a', 'b', 'c'):
            line = next(s for s in fs.splitlines() if 'tevin_' + value + ' =' in s)
            self.assertIn('& ivec4(255)', line)
            self.assertNotIn('clamp(', line)
        self.assertIn('ivec3(uvec3(', fs)
        self.assertIn('& uvec3(255u)', fs)


if __name__ == '__main__':
    unittest.main()
