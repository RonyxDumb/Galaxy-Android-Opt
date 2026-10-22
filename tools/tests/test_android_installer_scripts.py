"""Run the APK's actual converter modules in their isolated flat layout."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

PROBE = r'''
import pathlib, struct, sys
sys.path.insert(0, sys.argv[1])
import cook
root = pathlib.Path(sys.argv[2])
source = root / 'disc'
(source / 'sys').mkdir(parents=True)
(source / 'sys/fst.bin').write_bytes(b'fst')
(source / 'files/ObjectData').mkdir(parents=True)
(source / 'files/MovieData').mkdir()
(source / 'files/note.txt').write_bytes(b'original')
# Minimal real RARC containing a text file.
archive = bytearray(0xA0 + 2)
archive[:4] = b'RARC'
struct.pack_into('>7I', archive, 4, len(archive), 0x20, 0x80, 2, 2, 0, 0)
struct.pack_into('>6I', archive, 0x20, 1, 0x20, 1, 0x30, 16, 0x44)
struct.pack_into('>IIHHI', archive, 0x40, 0x524f4f54, 0, 0, 1, 0)
struct.pack_into('>HHIIII', archive, 0x50, 0, 0, 0x11000005, 0, 2, 0)
archive[0x64:0x72] = b'Root\0note.txt\0'
archive[0xA0:] = b'ok'
(source / 'files/ObjectData/Coin.arc').write_bytes(archive)
movie = bytearray(0x44)
movie[:4] = b'THP\0'
struct.pack_into('>11I', movie, 4, 0x11000, 0, 0, 0, 0, 0, 0, 0x30, 0, 0x44, 0)
if sys.argv[3] == 'bad_movie': movie[:4] = b'BAD!'
(source / 'files/MovieData/intro.thp').write_bytes(movie)
# DOL table discovery has its own tests; no copyrighted executable is needed.
cook.cook_dol_data = lambda *args: []
events = []
movies = sys.argv[3] != 'skip_movies'
out = root / 'game'
result = cook.convert(str(source), str(out), with_movies=movies,
                      progress=lambda done, total, *args: events.append((done, total)))
assert result == (1 if sys.argv[3] == 'bad_movie' else 0), result
assert (out / 'files/note.txt').read_bytes() == b'original'
assert (out / 'files/ObjectData/Coin.arc').read_bytes()[:4] == b'CRAR'
assert (source / 'files/ObjectData/Coin.arc').read_bytes() == archive
assert events[-1] == ((3, 3) if movies else (2, 2)), events
if not movies:
    assert not (out / 'files/MovieData').exists()
elif result == 0:
    assert (out / 'files/MovieData/intro.thp').read_bytes()[4:8] == b'\0\x10\x01\0'
    assert (source / 'files/MovieData/intro.thp').read_bytes() == movie
print('PACKAGED CONVERSION VERIFIED')
'''


class AndroidInstallerScriptTests(unittest.TestCase):
    def run_converter(self, mode):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            scripts = root / 'scripts'
            shutil.copytree(ROOT / 'installer-mobile/assets/scripts', scripts,
                            ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
            result = subprocess.run([sys.executable, '-I', '-c', PROBE, str(scripts), tmp, mode],
                                    cwd=tmp, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PACKAGED CONVERSION VERIFIED', result.stdout)

    def test_archive_copy_and_movie_jobs_complete(self):
        self.run_converter('movies')

    def test_skipping_movies_still_completes_other_jobs(self):
        self.run_converter('skip_movies')

    def test_invalid_movie_reports_conversion_failure(self):
        self.run_converter('bad_movie')


if __name__ == '__main__':
    unittest.main()
