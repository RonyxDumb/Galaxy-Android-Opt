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
import installer_fixtures as fixture
from manifest_archive import rarc_list
root = pathlib.Path(sys.argv[2])
source = root / 'disc'
(source / 'sys').mkdir(parents=True)
(source / 'sys/fst.bin').write_bytes(b'fst')
(source / 'sys/main.dol').write_bytes(fixture.dol(sys.argv[3] == 'missing_system'))
(source / 'files/ObjectData').mkdir(parents=True)
(source / 'files/MovieData').mkdir()
(source / 'files/note.txt').write_bytes(b'original')
archive = fixture.archive({'note.txt': b'ok', 'Anims/wait.bas': fixture.sound_animation(),
                           'Anims/walk.bck': fixture.animation_with_sound()})
(source / 'files/ObjectData/Coin.arc').write_bytes(archive)
movie = bytearray(0x44)
movie[:4] = b'THP\0'
struct.pack_into('>11I', movie, 4, 0x11000, 0, 0, 0, 0, 0, 0, 0x30, 0, 0x44, 0)
if sys.argv[3] == 'bad_movie': movie[:4] = b'BAD!'
(source / 'files/MovieData/intro.thp').write_bytes(movie)
events = []
movies = sys.argv[3] != 'skip_movies'
out = root / 'game'
# Model the JNI logger's separate writes, including print's argument fragments.
messages = []
class BridgeLogger:
    def write(self, text):
        messages.extend(line.strip() for line in text.splitlines() if line.strip())
    def flush(self): pass
original_stdout = sys.stdout
sys.stdout = BridgeLogger()
result = cook.convert(str(source), str(out), with_movies=movies,
                      progress=lambda done, total, *args: events.append((done, total)))
sys.stdout = original_stdout
failed = sys.argv[3] in ('bad_movie', 'missing_system')
assert result == int(failed), (result, messages)
if failed:
    filename = 'intro.thp' if sys.argv[3] == 'bad_movie' else 'main.dol'
    assert any(line.startswith('[Error]') and filename in line for line in messages), messages
assert (out / 'files/note.txt').read_bytes() == b'original'
assert (out / 'files/ObjectData/Coin.arc').read_bytes()[:4] == b'CRAR'
assert (source / 'files/ObjectData/Coin.arc').read_bytes() == archive
contents = dict(rarc_list((out / 'files/ObjectData/Coin.arc').read_bytes()))
assert contents['Root/Anims/wait.bas'] == fixture.sound_animation('<')
assert contents['Root/Anims/walk.bck'][0x60:] == fixture.sound_animation('<')
assert contents['Root/Anims/walk.bck'][:4] == b'1D3J'
assert (out / 'sys/ErrorMessageArchive.arc').read_bytes()[:4] == b'CRAR'
assert struct.unpack_from('<4I', (out / 'sys/StoryEvent.bcsv').read_bytes()) == (1, 2, 40, 8)
if sys.argv[3] != 'missing_system':
    assert struct.unpack_from('<4I', (out / 'sys/GalaxyID.bcsv').read_bytes()) == (1, 7, 100, 28)
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
            shutil.copyfile(ROOT / 'tools/tests/installer_fixtures.py', scripts / 'installer_fixtures.py')
            shutil.copyfile(ROOT / 'tools/rarc.py', scripts / 'manifest_archive.py')
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

    def test_missing_system_table_reports_the_actual_file(self):
        self.run_converter('missing_system')


if __name__ == '__main__':
    unittest.main()
