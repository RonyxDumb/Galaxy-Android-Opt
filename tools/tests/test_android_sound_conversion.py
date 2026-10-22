"""Exercise sound-animation and collision helper imports in the APK modules."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROBE = r'''
import collections, struct, sys
sys.path.insert(0, sys.argv[1])
sys.path.insert(0, sys.argv[2])
import cook, collision
import installer_fixtures as fixture
mode = sys.argv[3]
stats = collections.Counter()
if mode == 'bas':
    data = bytearray(fixture.sound_animation())
    cook.cook_blob('wait.bas', data, stats)
    assert data == fixture.sound_animation('<')
    assert stats['bas'] == 1
elif mode == 'empty':
    data = bytearray(struct.pack('>HHI', 0, 0, 0))
    cook.cook_blob('empty.bas', data, stats)
    assert struct.unpack('<HHI', data) == (0, 0, 0)
    assert stats['bas'] == 1
elif mode == 'embedded':
    data = bytearray(fixture.animation_with_sound())
    cook.cook_blob('walk.bck', data, stats)
    assert data[:4] == b'1D3J'
    assert data[0x20:0x24] == b'1KNA'
    assert struct.unpack_from('<I', data, 0x1c)[0] == 0x60
    assert data[0x60:] == fixture.sound_animation('<')
    assert stats['j3d-anim'] == 1
elif mode == 'collision':
    original = ((0., 0., 0.), (0., 0., 100.), (100., 0., 0.))
    data, kept = collision.build_kcl([original], 1.)
    assert kept == [0]
    decoded = collision.kcl_triangles(data)
    assert len(decoded) == 1
    for actual, expected in zip(decoded[0], original):
        assert all(abs(a-b) < 0.0001 for a,b in zip(actual,expected))
print('ANDROID MODULE VERIFIED: ' + mode)
'''


class AndroidSoundConversionTests(unittest.TestCase):
    def run_probe(self, mode):
        result = subprocess.run([sys.executable, '-I', '-c', PROBE,
                                 str(ROOT / 'installer-mobile/assets/scripts'),
                                 str(ROOT / 'tools/tests'), mode],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('ANDROID MODULE VERIFIED', result.stdout)

    def test_bas_sound_records_are_converted(self):
        self.run_probe('bas')

    def test_bas_without_sounds_is_accepted(self):
        self.run_probe('empty')

    def test_bck_embedded_sound_records_are_converted(self):
        self.run_probe('embedded')

    def test_collision_helpers_round_trip_one_triangle(self):
        self.run_probe('collision')


if __name__ == '__main__':
    unittest.main()
