"""Exercise the install-time material scanner in the APK's flat module layout."""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from tools.tests import test_shape_attrs as fixture

ROOT = Path(__file__).resolve().parents[2]
MODULES = ('install_manifest.py', 'generate_shader_manifest.py', 'manifest_schema.py',
           'j3d_mat3_parser.py', 'j3d_shape_attrs.py', 'j3d_gx_state.py',
           'j3d_material_state.py', 'j3d_pipeline_key.py')


class InstallManifestTests(unittest.TestCase):
    def test_missing_inventory_keeps_existing_manifest(self):
        from tools.cook.install_manifest import prepare_graphics_manifest
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'files').mkdir()
            manifest = root / 'galaxy_shader_manifest.bin'
            manifest.write_bytes(b'previous valid inventory')
            self.assertIsNone(prepare_graphics_manifest(tmp))
            self.assertEqual(manifest.read_bytes(), b'previous valid inventory')

    def test_inventory_write_failure_is_optional_and_atomic(self):
        from tools.cook.install_manifest import prepare_graphics_manifest
        from tools.cook import generate_shader_manifest as scanner
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            manifest = root / 'galaxy_shader_manifest.bin'
            manifest.write_bytes(b'previous valid inventory')
            def fail_write(materials, target):
                Path(target).write_bytes(b'partial')
                raise OSError('synthetic full disk')
            with mock.patch.dict(sys.modules, {'generate_shader_manifest': scanner}), \
                 mock.patch.object(scanner, 'scan_directory_for_models', return_value=([object()], {})), \
                 mock.patch.object(scanner, 'write_manifest', side_effect=fail_write):
                self.assertIsNone(prepare_graphics_manifest(tmp))
            self.assertEqual(manifest.read_bytes(), b'previous valid inventory')
            self.assertFalse(Path(str(manifest) + '.tmp').exists())

    def test_cooked_archives_prepare_their_own_inventory(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            scripts = base / 'scripts'
            scripts.mkdir()
            for name in MODULES:
                shutil.copyfile(ROOT / 'tools/cook' / name, scripts / name)
            shutil.copyfile(ROOT / 'tools/rarc.py', scripts / 'manifest_archive.py')
            # This old converter reader deliberately does not understand CRAR.
            shutil.copyfile(ROOT / 'installer-mobile/assets/scripts/rarc.py', scripts / 'rarc.py')
            objects = base / 'game/files/ObjectData'
            objects.mkdir(parents=True)
            model = fixture.model([fixture.shp1([{fixture.GXA_POS, fixture.GXA_NRM, fixture.GXA_TEX0}]),
                                   fixture.mat3([0], [0])])
            archive = bytearray(0xA0 + len(model))
            archive[:4] = b'CRAR'
            struct.pack_into('<II', archive, 8, 0x20, 0x80)
            struct.pack_into('<6I', archive, 0x20, 1, 0x20, 1, 0x30, 16, 0x44)
            struct.pack_into('<IIHHI', archive, 0x40, 0, 0, 0, 1, 0)
            struct.pack_into('<HHIIII', archive, 0x50, 0, 0, 0x11000005, 0, len(model), 0)
            archive[0x64:0x6f] = b'Root\0m.bdl\0'
            archive[0xA0:] = model
            (objects / 'Mario.arc').write_bytes(archive)
            code = ('import sys; sys.path.insert(0, sys.argv[1]); import rarc; '
                    'from install_manifest import prepare_graphics_manifest; '
                    'prepare_graphics_manifest(sys.argv[2])')
            result = subprocess.run([sys.executable, '-I', '-c', code, str(scripts), str(base / 'game')],
                                    capture_output=True, text=True, cwd=tmp)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            data = (base / 'game/galaxy_shader_manifest.bin').read_bytes()
            self.assertEqual(struct.unpack_from('<I', data, 12)[0], 1)
            self.assertEqual(struct.unpack_from('<I', data, 20)[0], 0x22)
            self.assertIn('0 parser gaps', result.stdout)


if __name__ == '__main__':
    unittest.main()
