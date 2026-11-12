import json
import os
import subprocess
import sys
import tempfile
import unittest
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "make-release-info.py")
VERSION = "0.0.0.7"
ENTRY = "Data/CommonwealthOnline/release.json"


def write_zip(path, entries):
    with zipfile.ZipFile(path, "w") as archive:
        for name, text in entries.items():
            archive.writestr(name, text)


class MakeReleaseInfoTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.dir = directory.name
        self.out = os.path.join(self.dir, "release.json")
        self.zip = os.path.join(self.dir, "CommonwealthOnline-%s.zip" % VERSION)
        write_zip(self.zip, {"Data/CommonwealthOnline.esp": "esp"})

    def run_script(self, *args):
        return subprocess.run([sys.executable, SCRIPT, *args], capture_output=True, text=True)

    def assert_failed(self, result, fragment):
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(fragment, result.stderr)

    def test_out_writes_schema_1_with_the_version(self):
        result = self.run_script("--version", VERSION, "--out", self.out)
        self.assertEqual(result.returncode, 0, result.stderr)
        with open(self.out, encoding="utf-8") as handle:
            text = handle.read()
        self.assertTrue(text.endswith("\n"))
        self.assertEqual(json.loads(text), {"schema": 1, "version": VERSION})
        self.assertEqual(list(json.loads(text)), ["schema", "version"])

    def test_inject_adds_the_file_and_keeps_the_other_entries(self):
        result = self.run_script("--version", VERSION, "--inject", self.zip)
        self.assertEqual(result.returncode, 0, result.stderr)
        with zipfile.ZipFile(self.zip) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(archive.namelist(), ["Data/CommonwealthOnline.esp", ENTRY])
            self.assertEqual(archive.read("Data/CommonwealthOnline.esp"), b"esp")
            self.assertEqual(json.loads(archive.read(ENTRY)), {"schema": 1, "version": VERSION})

    def test_inject_matches_the_out_file_byte_for_byte(self):
        self.assertEqual(self.run_script("--version", VERSION, "--inject", self.zip, "--out", self.out).returncode, 0)
        with open(self.out, "rb") as handle, zipfile.ZipFile(self.zip) as archive:
            self.assertEqual(handle.read(), archive.read(ENTRY))

    def test_injecting_twice_fails_and_leaves_one_copy(self):
        self.assertEqual(self.run_script("--version", VERSION, "--inject", self.zip).returncode, 0)
        self.assert_failed(self.run_script("--version", VERSION, "--inject", self.zip), "already has")
        with zipfile.ZipFile(self.zip) as archive:
            self.assertEqual(archive.namelist().count(ENTRY), 1)

    def test_a_zip_that_is_not_a_zip_fails(self):
        with open(self.zip, "w", encoding="utf-8") as handle:
            handle.write("not a zip")
        self.assert_failed(self.run_script("--version", VERSION, "--inject", self.zip), "not a valid zip")

    def test_bad_versions_fail(self):
        for version in ("v0.0.0.7", "0.0.x", "", "0..1", "1234567890.1", "1." * 20 + "1", "0.0.0.7\n"):
            self.assert_failed(self.run_script("--version", version, "--out", self.out), "dotted digits")
            self.assertFalse(os.path.exists(self.out), version)

    def test_an_output_is_required(self):
        self.assert_failed(self.run_script("--version", VERSION), "--out, --inject")


if __name__ == "__main__":
    unittest.main()
