"""Tester identity changes preserve activity classes and process isolation."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

import package_mobile_apk as packager
from tools import apk_env

NS = "{http://schemas.android.com/apk/res/android}"


class TesterPackageTests(unittest.TestCase):
    def test_default_manifest_remains_unchanged(self):
        before = Path(packager.MANIFEST).read_bytes()
        path, package = packager.package_manifest()
        self.assertEqual(path, packager.MANIFEST)
        self.assertEqual(package, "com.openport.galaxyandroid")
        self.assertEqual(Path(packager.MANIFEST).read_bytes(), before)

    def test_independent_id_preserves_java_classes_and_game_process(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(packager, "OUT", tmp):
            path, package = packager.package_manifest("com.openport.galaxyandroid.a34test")
            manifest = ET.parse(path).getroot()
            self.assertEqual(manifest.get("package"), "com.openport.galaxyandroid.a34test")
            self.assertEqual(package, "com.openport.galaxyandroid")
            app = manifest.find("application")
            self.assertEqual(app.get(NS + "label"), "Super Mario Galaxy")
            activities = app.findall("activity")
            game = next(a for a in activities if a.get(NS + "name").endswith(".GameActivity"))
            self.assertEqual(game.get(NS + "name"), package + ".GameActivity")
            self.assertEqual(game.get(NS + "process"), ":game")
            self.assertEqual(game.get(NS + "exported"), "false")

    def test_invalid_ids_are_rejected(self):
        for application_id in ("../outside", "oneword", "bad..id", "com.bad-id"):
            with self.subTest(application_id=application_id), self.assertRaises(ValueError):
                packager.package_manifest(application_id)

    def test_local_signing_key_creates_parent_and_is_reused(self):
        with tempfile.TemporaryDirectory() as tmp:
            key = Path(tmp) / "out" / "dev.keystore"
            with patch.object(apk_env, "DEV_KEYSTORE", str(key)), \
                 patch.object(apk_env, "KEYSTORE", ""), \
                 patch.object(apk_env, "KEYSTORE_CANDIDATES", []), \
                 patch.dict(apk_env.os.environ, {"GALAXY_ALLOW_NEW_KEY": "1"}, clear=True), \
                 patch.object(apk_env.subprocess, "run") as run:
                self.assertEqual(apk_env.prepare_signing_key("tester"), str(key))
                self.assertTrue(key.parent.is_dir())
                run.assert_called_once()
                key.write_bytes(b"test key")
                apk_env.KEYSTORE = ""
                run.reset_mock()
                self.assertEqual(apk_env.prepare_signing_key("tester"), str(key))
                run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
