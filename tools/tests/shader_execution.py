"""Harness for running complete generated shader bodies as Vulkan compute jobs.

Only stage inputs/outputs are adapted; lighting, matrix math and TEV expressions
come from production code. This checks numerics, not rasterization or Mali.
"""
import ctypes
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unittest

from tools.tests import test_shadergen as generator


class Uid(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32 * count if count > 1 else ctypes.c_uint32)
                for name, count in (
                    ('vtxFlags', 1), ('numTexGens', 1), ('numChans', 1), ('dualTex', 1),
                    ('colorCtrl', 2), ('alphaCtrl', 2), ('texMtxInfo', 8), ('postMtxInfo', 8),
                    ('numTevStages', 1), ('numIndStages', 1), ('tevOrder', 8), ('tevColor', 16),
                    ('tevAlpha', 16), ('ksel', 8), ('alphaFunc', 1), ('fogSel', 1),
                    ('indCmd', 16), ('iref', 1), ('indScale', 2), ('hud', 1), ('zTex', 1),
                    ('cutaway', 1), ('screenTexGens', 1))]


class Harness:
    def __init__(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.compiler = generator.find_compiler()
        self.glslang = os.environ.get('GLSLANG_VALIDATOR') or shutil.which('glslangValidator')
        if not self.compiler or not self.glslang:
            self.unavailable('C++ compiler and glslangValidator required')
        self.probe = self.base / 'generator'
        self.runner = self.base / 'runner'
        includes = ['-I', str(generator.ROOT / 'platform/src'), '-I', str(generator.GX),
                    '-I', str(generator.ROOT / 'platform/include')]
        self.run([self.compiler, '-std=c++17', '-pthread', *includes, str(generator.PROBE_SRC),
                  *map(str, generator.GENERATOR_SRCS), '-o', str(self.probe)])
        headers = os.environ.get('VULKAN_INCLUDE', '/usr/include')
        if not (Path(headers) / 'vulkan/vulkan.h').is_file():
            self.unavailable('Vulkan development headers required')
        built = subprocess.run([self.compiler, '-std=c++17', '-I', headers,
                                str(generator.ROOT / 'tools/tests/shader_execution_probe.cpp'),
                                '-Wl,-l:libvulkan.so.1', '-o', str(self.runner)], capture_output=True, text=True)
        if built.returncode:
            raise AssertionError(built.stderr)

    def unavailable(self, reason, cleanup=True):
        if cleanup:
            self.tmp.cleanup()
        if os.environ.get('GALAXY_REQUIRE_SHADER_EXECUTION') == '1':
            raise AssertionError(reason)
        raise unittest.SkipTest(reason)

    @staticmethod
    def run(cmd):
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=45)
        if r.returncode:
            raise AssertionError(r.stdout + r.stderr)
        return r.stdout

    def execute(self, uid, rows, recs, states, stage='vs', attrs=None, universal=False):
        (self.base / 'uid').write_bytes(bytes(uid))
        source = self.run([str(self.probe), '--uid', ('u' if universal else '') + stage, str(self.base / 'uid')])
        if universal:
            states = list(states)
            recs = list(recs)
            ub = len(states) // 4
            states.extend(struct.unpack('<100f', bytes(uid) + bytes(4)))
            recs[47] = ub
        # First validate the original vertex/fragment interface without adaptation.
        original = self.base / ('original.' + ('vert' if stage == 'vs' else 'frag'))
        original.write_text(source)
        self.run([self.glslang, '-V', str(original), '-o', str(self.base / 'original.spv')])
        values = dict(a_pos='vec3(0.0)', a_nrm='vec3(0.0, 0.0, 1.0)',
                      a_clr0='vec4(1.0)', a_clr1='vec4(1.0)', a_bin='vec3(0.0, 1.0, 0.0)',
                      a_tan='vec3(1.0, 0.0, 0.0)', v_pix='49u',
                      v_clr0='vec4(0.25, 0.5, 0.75, 1.0)', v_clr1='vec4(0.0)',
                      v_viewz='0.0', v_gxz='0.0', v_uid=str(ub if universal else 0) + 'u')
        values.update(attrs or {})

        def variable(m):
            direction, kind, name = m.groups()
            init = values.get(name, kind + '(0)') if direction == 'in' else kind + '(0)'
            return f'{kind} {name} = {init};'

        source = re.sub(r'layout\(location = \d+\) (?:flat )?(in|out) (\w+) (\w+);', variable, source)
        source = source.replace('void main()', 'void gx_main()').replace('discard;', 'return;')
        source = source.replace('#version 450', '#version 450\nlayout(local_size_x = 1) in;\n'
                                'vec4 gl_Position;\nconst int gl_VertexIndex = 0;\n'
                                'float gx_frag_depth = 0.25; const vec4 gx_frag_coord = vec4(0.5, 0.5, 0.25, 1.0);')
        source = source.replace('gl_Position', 'gx_position').replace('gl_VertexIndex', 'gx_vertex_index')
        source = source.replace('gl_FragDepth', 'gx_frag_depth').replace('gl_FragCoord', 'gx_frag_coord')
        source += '\nlayout(std430, set = 2, binding = 0) buffer Results { vec4 results[]; };\n'
        source += 'void main() { gx_main(); '
        if stage == 'vs':
            source += 'results[0] = v_clr0; results[1] = v_clr1; '
            for i in range(uid.numTexGens):
                source += f'results[{i + 2}] = vec4(v_tex{i}, 1.0); '
        else:
            source += 'results[0] = o_color; results[10] = vec4(gx_frag_depth); '
        source += '}\n'
        compute = self.base / 'body.comp'
        compute.write_text(source)
        spirv = self.base / 'body.spv'
        self.run([self.glslang, '-V', str(compute), '-o', str(spirv)])
        for name, fmt, data in [('rows', 'f', rows), ('recs', 'I', recs), ('states', 'f', states)]:
            (self.base / name).write_bytes(struct.pack('<' + fmt * len(data), *data))
        r = subprocess.run([str(self.runner), str(spirv), str(self.base / 'rows'), str(self.base / 'recs'),
                            str(self.base / 'states'), str(self.base / 'result')], capture_output=True, text=True, timeout=30)
        if r.returncode == 77:
            self.unavailable('no usable Vulkan device or software ICD', cleanup=False)
        if r.returncode:
            raise AssertionError(r.stdout + r.stderr)
        result = struct.unpack('<64f', (self.base / 'result').read_bytes())
        result = [result[i:i + 4] for i in range(0, len(result), 4)]
        if not universal:
            general = self.execute(uid, rows, recs, states, stage, attrs, universal=True)
            count = uid.numTexGens + 2 if stage == 'vs' else 1
            for i in range(count):
                for a, b in zip(result[i], general[i]):
                    if abs(a - b) > 1e-5:
                        raise AssertionError(f'universal shader differs at output {i}: {result[i]} vs {general[i]}')
            if stage == 'fs' and abs(result[10][0] - general[10][0]) > 1e-5:
                raise AssertionError(f'universal depth differs: {result[10][0]} vs {general[10][0]}')
        return result


def fixture(ctrl=2):
    uid = Uid(vtxFlags=2 | 4 | 8 | 16 | 32, numChans=1, numTevStages=1, alphaFunc=63)
    uid.colorCtrl[0] = ctrl
    uid.ksel[0], uid.ksel[1] = 4, 14  # identity swap table
    uid.tevColor[0] = 0xfffa | (1 << 19)  # D = raster RGB
    uid.tevAlpha[0] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19)
    rows = [0.0] * (123 * 4)
    for base in [0, 60, *range(66, 123, 3)]:
        for i in range(3):
            rows[(base + i) * 4 + i] = 1.0
    recs = [0] * 48
    for i in range(22):
        recs[i] = i * 3
    for i in range(11):
        recs[22 + i] = 66 + i * 3
    for i in range(8):
        recs[33 + i] = 99 + i * 3
    recs[41:46] = [0, 44, 49, sum(60 << (i * 6) for i in range(1, 5)), sum(60 << (i * 6) for i in range(4))]
    states = [0.0] * (77 * 4)
    for i in range(4):
        states[(44 + i) * 4 + i] = 1.0
    states[48 * 4] = 1.0
    states[40 * 4:41 * 4] = [1.0, 0.0, 0.0, 1.0]  # red material
    states[42 * 4:43 * 4] = [1.0] * 4  # white ambient
    states[0:4] = [1.0] * 4  # light 0 white
    states[4:7] = [1.0, 0.0, 0.0]  # constant angular attenuation
    states[8:11] = [1.0, 0.0, 0.0]  # constant distance attenuation
    states[12:15] = [0.0, 0.0, 10.0]
    states[16:19] = [0.0, 0.0, 1.0]
    return uid, rows, recs, states
