import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "make-latest-json.py")
VERSION = "0.0.0.7"
BASE = "https://example.test/releases/download/0.0.0.7"
FINGERPRINT = "a" * 64
MANIFEST = "Data/F4SE/Plugins/CommonwealthOnlineClient.json"


def write_zip(path, entries):
    with zipfile.ZipFile(path, "w") as archive:
        for name, text in entries.items():
            archive.writestr(name, text)


def sha256_of(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


class MakeLatestJsonTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.dir = directory.name
        self.out = os.path.join(self.dir, "latest.json")
        for tag in ("AE", "NG", "OG"):
            manifest = {"clientVersion": "0.1.0", "contentManifestFingerprint": FINGERPRINT}
            write_zip(
                os.path.join(self.dir, "CommonwealthOnline-%s-%s.zip" % (tag, VERSION)),
                {MANIFEST: json.dumps(manifest)},
            )
        write_zip(
            os.path.join(self.dir, "CommonwealthOnlineUI-%s.zip" % VERSION),
            {"Data/F4SE/Plugins/CommonwealthOnlineUI.dll": "stub"},
        )

    def run_script(self, base=BASE):
        return subprocess.run(
            [sys.executable, SCRIPT, "--dir", self.dir, "--version", VERSION, "--base-url", base],
            capture_output=True,
            text=True,
        )

    def test_writes_latest_json_schema(self):
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        with open(self.out, encoding="utf-8") as handle:
            text = handle.read()
        self.assertTrue(text.endswith("\n"))
        latest = json.loads(text)
        self.assertEqual(list(latest), ["version", "runtimes", "ui", "contentFingerprint"])
        self.assertEqual(latest["version"], VERSION)
        self.assertEqual(latest["contentFingerprint"], FINGERPRINT)
        self.assertEqual(list(latest["runtimes"]), ["og", "ng", "ae"])
        for key, tag in (("og", "OG"), ("ng", "NG"), ("ae", "AE")):
            name = "CommonwealthOnline-%s-%s.zip" % (tag, VERSION)
            self.assertEqual(latest["runtimes"][key], {
                "url": BASE + "/" + name,
                "sha256": sha256_of(os.path.join(self.dir, name)),
            })
        ui_name = "CommonwealthOnlineUI-%s.zip" % VERSION
        self.assertEqual(latest["ui"], {
            "url": BASE + "/" + ui_name,
            "sha256": sha256_of(os.path.join(self.dir, ui_name)),
        })

    def test_client_version_is_not_compared_to_release_version(self):
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_fingerprint_mismatch_fails(self):
        write_zip(
            os.path.join(self.dir, "CommonwealthOnline-NG-%s.zip" % VERSION),
            {MANIFEST: json.dumps({"clientVersion": "0.1.0", "contentManifestFingerprint": "b" * 64})},
        )
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("contentManifestFingerprint", result.stderr)
        self.assertFalse(os.path.exists(self.out))

    def test_missing_ui_zip_fails(self):
        ui_name = "CommonwealthOnlineUI-%s.zip" % VERSION
        os.remove(os.path.join(self.dir, ui_name))
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(ui_name, result.stderr)
        self.assertFalse(os.path.exists(self.out))

    def test_http_base_url_fails(self):
        result = self.run_script(base="http://example.test/releases")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("https", result.stderr)
        self.assertFalse(os.path.exists(self.out))


if __name__ == "__main__":
    unittest.main()
