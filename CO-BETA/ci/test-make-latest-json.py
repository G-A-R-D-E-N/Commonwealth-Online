import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "make-latest-json.py")
VERSION = "0.0.0.7"
BASE = "https://github.com/G-A-R-D-E-N/Commonwealth-Online/releases/download/0.0.0.7"
FINGERPRINT = "a" * 64
MANIFEST = "Data/F4SE/Plugins/CommonwealthOnlineClient.json"
LAUNCHER_NAME = "CommonwealthOnline-Launcher-%s.zip" % VERSION
UI_NAME = "CommonwealthOnlineUI-%s.zip" % VERSION
RUNTIME_NAMES = {key: "CommonwealthOnline-%s-%s.zip" % (tag, VERSION) for key, tag in (("og", "OG"), ("ng", "NG"), ("ae", "AE"))}
TOP_KEYS = ["schema", "version", "minLauncher", "runtimes", "ui", "contentFingerprint"]


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
        for name in RUNTIME_NAMES.values():
            manifest = {"clientVersion": "0.1.0", "contentManifestFingerprint": FINGERPRINT}
            write_zip(os.path.join(self.dir, name), {MANIFEST: json.dumps(manifest)})
        write_zip(os.path.join(self.dir, UI_NAME), {"Data/PrismaUI_F4/views/index.html": "stub"})
        launcher_dir = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, launcher_dir, True)
        self.launcher = os.path.join(launcher_dir, LAUNCHER_NAME)
        write_zip(self.launcher, {"CommonwealthOnline-Launcher.exe": "launcher stub"})

    def run_script(self, base=BASE, extra=()):
        return subprocess.run(
            [sys.executable, SCRIPT, "--dir", self.dir, "--version", VERSION, "--base-url", base, *extra],
            capture_output=True,
            text=True,
        )

    def load(self):
        with open(self.out, encoding="utf-8") as handle:
            return handle.read()

    def assert_failed(self, result, fragment):
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(fragment, result.stderr)
        self.assertFalse(os.path.exists(self.out))

    def test_writes_schema_2_without_launcher(self):
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        text = self.load()
        self.assertTrue(text.endswith("\n"))
        latest = json.loads(text)
        self.assertEqual(list(latest), TOP_KEYS)
        self.assertEqual(latest["schema"], 2)
        self.assertEqual(latest["version"], VERSION)
        self.assertEqual(latest["minLauncher"], "0.0.0.0")
        self.assertEqual(latest["contentFingerprint"], FINGERPRINT)
        self.assertEqual(list(latest["runtimes"]), ["og", "ng", "ae"])
        for key, name in RUNTIME_NAMES.items():
            path = os.path.join(self.dir, name)
            self.assertEqual(list(latest["runtimes"][key]), ["url", "sha256", "size"])
            self.assertEqual(latest["runtimes"][key], {
                "url": BASE + "/" + name,
                "sha256": sha256_of(path),
                "size": os.path.getsize(path),
            })
        ui_path = os.path.join(self.dir, UI_NAME)
        self.assertEqual(latest["ui"], {
            "url": BASE + "/" + UI_NAME,
            "sha256": sha256_of(ui_path),
            "size": os.path.getsize(ui_path),
        })

    def test_launcher_entry_is_added_when_supplied(self):
        result = self.run_script(extra=["--launcher-zip", self.launcher])
        self.assertEqual(result.returncode, 0, result.stderr)
        latest = json.loads(self.load())
        self.assertEqual(list(latest), ["schema", "version", "minLauncher", "runtimes", "ui", "launcher", "contentFingerprint"])
        self.assertEqual(latest["launcher"], {
            "url": BASE + "/" + LAUNCHER_NAME,
            "sha256": sha256_of(self.launcher),
            "size": os.path.getsize(self.launcher),
        })

    def test_unified_archive_satisfies_all_legacy_manifest_entries(self):
        name = "CommonwealthOnline-%s.zip" % VERSION
        path = os.path.join(self.dir, name)
        write_zip(path, {MANIFEST: json.dumps({"contentManifestFingerprint": FINGERPRINT})})
        for old_name in [*RUNTIME_NAMES.values(), UI_NAME]:
            os.remove(os.path.join(self.dir, old_name))
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        latest = json.loads(self.load())
        expected = {"url": BASE + "/" + name, "sha256": sha256_of(path), "size": os.path.getsize(path)}
        self.assertEqual(latest["ui"], expected)
        self.assertTrue(all(part == expected for part in latest["runtimes"].values()))
        listed = subprocess.run([sys.executable, os.path.join(HERE, "public-release-assets.py"), self.dir], capture_output=True, text=True)
        self.assertEqual(listed.returncode, 0, listed.stderr)
        self.assertEqual(sorted(os.path.basename(line) for line in listed.stdout.splitlines()), sorted([name, "latest.json", "latest.json.sig"]))

    def test_min_launcher_argument_is_written(self):
        result = self.run_script(extra=["--min-launcher", "0.2.3"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(self.load())["minLauncher"], "0.2.3")

    def test_output_is_deterministic(self):
        self.assertEqual(self.run_script(extra=["--launcher-zip", self.launcher]).returncode, 0)
        first = self.load()
        os.remove(self.out)
        self.assertEqual(self.run_script(extra=["--launcher-zip", self.launcher]).returncode, 0)
        self.assertEqual(first, self.load())

    def test_client_version_is_not_compared_to_release_version(self):
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_fingerprint_mismatch_fails(self):
        write_zip(
            os.path.join(self.dir, RUNTIME_NAMES["ng"]),
            {MANIFEST: json.dumps({"clientVersion": "0.1.0", "contentManifestFingerprint": "b" * 64})},
        )
        self.assert_failed(self.run_script(), "contentManifestFingerprint")

    def test_malformed_fingerprint_fails(self):
        write_zip(
            os.path.join(self.dir, RUNTIME_NAMES["og"]),
            {MANIFEST: json.dumps({"clientVersion": "0.1.0", "contentManifestFingerprint": "A" * 64})},
        )
        self.assert_failed(self.run_script(), "64-hex contentManifestFingerprint")

    def test_missing_ui_zip_fails(self):
        os.remove(os.path.join(self.dir, UI_NAME))
        self.assert_failed(self.run_script(), UI_NAME)

    def test_http_base_url_fails(self):
        self.assert_failed(self.run_script(base="http://example.test/releases"), "https")

    def test_invalid_version_fails(self):
        result = subprocess.run(
            [sys.executable, SCRIPT, "--dir", self.dir, "--version", "v0.0.0.7", "--base-url", BASE],
            capture_output=True,
            text=True,
        )
        self.assert_failed(result, "dotted digits")

    def test_invalid_min_launcher_fails(self):
        self.assert_failed(self.run_script(extra=["--min-launcher", "0.1.0-beta"]), "min-launcher")

    def test_launcher_zip_with_wrong_name_fails(self):
        wrong = os.path.join(os.path.dirname(self.launcher), "CommonwealthOnline-Launcher-0.0.0.6.zip")
        os.rename(self.launcher, wrong)
        self.assert_failed(self.run_script(extra=["--launcher-zip", wrong]), "must be named")

    def test_launcher_zip_that_is_not_a_zip_fails(self):
        with open(self.launcher, "w", encoding="utf-8") as handle:
            handle.write("not a zip")
        self.assert_failed(self.run_script(extra=["--launcher-zip", self.launcher]), "not a valid zip")

    def test_missing_launcher_zip_fails(self):
        missing = os.path.join(os.path.dirname(self.launcher), "missing", LAUNCHER_NAME)
        self.assert_failed(self.run_script(extra=["--launcher-zip", missing]), "launcher zip not found")

    def test_version_component_limit_is_u32(self):
        ok = subprocess.run(
            [sys.executable, SCRIPT, "--dir", self.dir, "--version", VERSION, "--base-url", BASE, "--min-launcher", "4294967295"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(ok.returncode, 0, ok.stderr)
        self.assertEqual(json.loads(self.load())["minLauncher"], "4294967295")
        os.remove(self.out)
        self.assert_failed(self.run_script(extra=("--min-launcher", "4294967296")), "each at most")

    def test_public_release_assets_lists_named_files_only(self):
        self.assertEqual(self.run_script().returncode, 0)
        listed = subprocess.run(
            [sys.executable, os.path.join(HERE, "public-release-assets.py"), self.dir],
            capture_output=True,
            text=True,
        )
        self.assertEqual(listed.returncode, 0, listed.stderr)
        names = [os.path.basename(line) for line in listed.stdout.splitlines()]
        self.assertEqual(sorted(names), sorted([*RUNTIME_NAMES.values(), UI_NAME, "latest.json", "latest.json.sig"]))


if __name__ == "__main__":
    unittest.main()
