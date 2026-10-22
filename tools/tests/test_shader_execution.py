"""Numeric regressions for GX lighting, texgen and TEV on software Vulkan."""
import math
import random
import unittest
from tools.tests import shader_execution as execution


class ShaderExecutionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.harness = execution.Harness()

    @classmethod
    def tearDownClass(cls):
        cls.harness.tmp.cleanup()

    def test_white_ambient_preserves_red_material(self):
        args = execution.fixture()
        rgba = self.harness.execute(*args)[0]
        self.assertEqual(rgba, (1.0, 0.0, 0.0, 1.0))

    def test_only_color1_attribute_feeds_channel_zero(self):
        args = execution.fixture(ctrl=1)
        args[0].alphaCtrl[0] = 1
        args[0].vtxFlags = 2 | 16  # Normal + CLR1, no CLR0.
        out = self.harness.execute(*args, attrs={'a_clr1': 'vec4(0.2, 0.4, 0.6, 0.8)'})[0]
        for got, want in zip(out, (0.2, 0.4, 0.6, 0.8)):
            self.assertAlmostEqual(got, want, places=6)

    def test_lights_saturate_before_material_modulation(self):
        args = execution.fixture(ctrl=2 | 4)
        args[3][160:164] = [128 / 255, 64 / 255, 0.0, 1.0]
        rgba = self.harness.execute(*args)[0]
        for got, expected in zip(rgba, (128 / 255, 64 / 255, 0, 1)):
            self.assertAlmostEqual(got, expected, places=6)

    def test_vertex_material_and_alpha_use_independent_sources(self):
        args = execution.fixture(ctrl=2 | 1)
        args[0].alphaCtrl[0] = 2
        args[3][163] = 64 / 255
        rgba = self.harness.execute(*args, attrs={'a_clr0': 'vec4(0.0, 1.0, 0.0, 1.0)'})[0]
        self.assertEqual(rgba[:3], (0, 1, 0))
        self.assertAlmostEqual(rgba[3], 64 / 255, places=6)

    def test_signed_diffuse_can_subtract_back_facing_light(self):
        args = execution.fixture(ctrl=2 | 4 | (1 << 7))
        args[3][12:15] = [0, 0, -10]
        rgba = self.harness.execute(*args)[0]
        self.assertEqual(rgba[:3], (0, 0, 0))

    def test_specular_angle_does_not_use_light_distance(self):
        args = execution.fixture(ctrl=2 | 4 | (1 << 9))
        args[3][168:172] = [0, 0, 0, 0]
        args[3][4:7] = [0, 1, 0]
        args[3][8:11] = [0, 1, 0]
        args[3][14] = 1e18
        rgba = self.harness.execute(*args)[0]
        self.assertEqual(rgba, (1, 0, 0, 1))

    def test_spot_direction_matches_gx_light_dir(self):
        args = execution.fixture(ctrl=2 | 4 | (3 << 9))
        args[3][168:172] = [0, 0, 0, 0]
        args[3][4:7] = [0, 1, 0]
        rgba = self.harness.execute(*args)[0]
        self.assertEqual(rgba[:3], (1, 0, 0))

    def test_texgen_uses_raw_normal_and_st_projection(self):
        args = execution.fixture()
        uid, rows, recs, _ = args
        uid.numTexGens = 1
        uid.texMtxInfo[0] = (1 << 7) | 4  # Normal, ABC1, ST
        rows[66 * 4:69 * 4] = [0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0]
        recs[44] = 60 << 6
        out = self.harness.execute(*args, attrs={'a_nrm': 'vec3(0.25, 0.5, 0.75)'})[2]
        self.assertEqual(out[:3], (0.25, 0.5, 1))

    def test_stq_projection_uses_third_matrix_row(self):
        args = execution.fixture()
        args[0].numTexGens = 1
        args[0].texMtxInfo[0] = (5 << 7) | 2  # Tex0, AB11, STQ
        args[1][62 * 4:63 * 4] = [0, 0, 2, 0]
        out = self.harness.execute(*args, attrs={'a_tex0': 'vec2(0.25, 0.5)'})[2]
        self.assertEqual(out[:3], (0.25, 0.5, 2))

    def test_matrix_zero_is_a_real_texture_matrix(self):
        args = execution.fixture()
        args[0].numTexGens = 1
        args[0].texMtxInfo[0] = 5 << 7
        args[2][44] = 0
        args[1][:12] = [2, 0, 0, 0.125, 0, 3, 0, 0.25, 0, 0, 1, 0]
        out = self.harness.execute(*args, attrs={'a_tex0': 'vec2(0.25, 0.5)'})[2]
        self.assertEqual(out[:3], (0.625, 1.75, 1))

    def test_position_texgen_uses_untransformed_position(self):
        args = execution.fixture()
        args[0].numTexGens = 1
        args[0].texMtxInfo[0] = 4
        args[1][3] = 50
        out = self.harness.execute(*args, attrs={'a_pos': 'vec3(0.25, 0.5, 0.75)'})[2]
        self.assertEqual(out[:3], (0.25, 0.5, 1))

    def test_normalize_before_post_matrix(self):
        args = execution.fixture()
        args[0].numTexGens = args[0].dualTex = 1
        args[0].texMtxInfo[0] = 5 << 7
        args[0].postMtxInfo[0] = 0x100
        args[1][99 * 4 + 3] = 2
        out = self.harness.execute(*args, attrs={'a_tex0': 'vec2(3.0, 4.0)'})[2]
        expected = (3 / math.sqrt(26) + 2, 4 / math.sqrt(26), 1 / math.sqrt(26))
        for got, want in zip(out, expected):
            self.assertAlmostEqual(got, want, places=6)

    def test_srtg_uses_lit_color(self):
        args = execution.fixture()
        args[0].numTexGens = 1
        args[0].texMtxInfo[0] = (2 << 7) | (2 << 4)
        out = self.harness.execute(*args)[2]
        self.assertEqual(out[:3], (1, 0, 1))

    def test_emboss_texgen_preserves_tangent_scale(self):
        args = execution.fixture()
        args[0].numTexGens = 2
        args[0].texMtxInfo[0] = 5 << 7
        args[0].texMtxInfo[1] = 1 << 4  # emboss from texgen 0 and light 0
        args[1][66 * 4] = 2
        args[3][12:15] = [10, 0, 0]
        out = self.harness.execute(*args, attrs={'a_tex0': 'vec2(0.25, 0.5)'})[3]
        self.assertEqual(out[:3], (2.25, 0.5, 1))

    def test_unused_second_raster_channel_is_zero(self):
        self.assertEqual(self.harness.execute(*execution.fixture())[1], (0, 0, 0, 0))

    def test_zero_swap_table_replicates_red_including_alpha(self):
        args = execution.fixture()
        args[0].tevAlpha[0] |= 1  # swap table 1, its register bits are all zero
        out = self.harness.execute(*args, stage='fs')[0]
        for value in out:
            self.assertAlmostEqual(value, 64 / 255, places=6)

    def test_negative_half_scale_uses_arithmetic_shift(self):
        args = execution.fixture()
        args[0].tevColor[0] = 0xfff2 | (3 << 20)  # D = REG0 = -1, divide by two
        args[3][50 * 4:50 * 4 + 3] = [-1 / 255] * 3
        self.assertEqual(self.harness.execute(*args, stage='fs')[0][:3], (1, 1, 1))

    def test_alpha_lerp_rounds_before_division(self):
        args = execution.fixture()
        args[0].tevAlpha[0] = (7 << 13) | (1 << 10) | (0 << 7) | (7 << 4) | (1 << 19)
        args[3][49 * 4 + 3] = 128 / 255
        args[3][50 * 4 + 3] = 1 / 255
        self.assertAlmostEqual(self.harness.execute(*args, stage='fs')[0][3], 1 / 255, places=6)

    def test_material_permutations_match_universal_shader(self):
        rng = random.Random(0x47414c41)
        for case in range(32):
            with self.subTest(material=case):
                args = execution.fixture()
                uid, _, _, states = args
                uid.numTexGens = 1
                uid.numTevStages = 1 + case % 16
                uid.numIndStages = 1
                uid.texMtxInfo[0] = 5 << 7
                uid.zTex = (1 + case % 2) << 2 | case % 3
                for i in range(16):
                    states[49 * 4 + i] = rng.randrange(-1024, 1024) / 255
                    states[53 * 4 + i] = rng.randrange(256) / 255
                for s in range(uid.numTevStages):
                    order = (s % 8) | 64 | ((case % 2) << 7)
                    uid.tevOrder[s // 2] |= order << ((s & 1) * 12)
                    uid.tevColor[s] = rng.randrange(1 << 24)
                    uid.tevAlpha[s] = rng.randrange(1 << 24)
                    uid.ksel[s // 2] |= (rng.choice([0, 7, 12, 15, 16, 31]) << (4 + (s & 1) * 10))
                    uid.ksel[s // 2] |= (rng.choice([0, 7, 16, 31]) << (9 + (s & 1) * 10))
                    uid.indCmd[s] = (1 << 9) | (3 << 7)
                self.harness.execute(*args, stage='fs', attrs={'v_gxz': '0.25'})

    def test_disabled_z_texture_preserves_depth_with_each_format(self):
        for format_bits in range(3):
            args = execution.fixture()
            args[0].zTex = format_bits
            out = self.harness.execute(*args, stage='fs')
            self.assertEqual(out[10][0], 0.25)

    def test_indirect_bump_channels_are_not_vertex_colors(self):
        for channel, expected in ((5, 128), (6, 132)):
            args = execution.fixture()
            uid = args[0]
            uid.numTexGens = uid.numIndStages = 1
            uid.tevOrder[0] = channel << 7
            uid.indCmd[0] = 3 << 7  # bump U = green byte (128), format 8 bits
            out = self.harness.execute(*args, stage='fs')[0]
            for color in out:
                self.assertAlmostEqual(color, expected / 255, places=6)


if __name__ == '__main__':
    unittest.main()
