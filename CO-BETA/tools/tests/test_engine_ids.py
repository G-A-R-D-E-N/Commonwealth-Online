from pathlib import Path
import hashlib
import json
import os
import re
import struct
import unittest

CO_BETA_ROOT = Path(__file__).resolve().parents[2]
LOCK_PATH = CO_BETA_ROOT / "tools" / "engine-ids.lock.json"
SCAN_DIRS = ("plugin/src", "plugin/include")
SCAN_SUFFIXES = (".cpp", ".h")
LITERAL_RE = re.compile(r"REL::(?:ID|VariantID|RelocationID)(?:\s+\w+)?\s*[{(]\s*(\d+(?:\s*,\s*\d+)*)")
DEFAULT_BIN_DIR = Path("E:/RE-work/commonlib_NonVR/AddressBins/F4SE/Plugins")
REPO_ROOT = CO_BETA_ROOT.parent
UI_ID_FUNCTION_RE = re.compile(r"inline std::optional<std::uint64_t> (\w+AddressLibraryId)\(.*?\n\t\}", re.S)
UI_ID_CASE_RE = re.compile(r"\{ (\d+, \d+, \d+), 0 \}\) \{\s*return (\d+);")
BIN_NAME_RE = re.compile(r"version-\d+-\d+-\d+-\d+\.bin")
UI_RUNTIMES = {"1, 10, 163": "OG", "1, 10, 984": "NG", "1, 11, 240": "AE"}


def ui_source_ids(path):
    text = path.read_text(encoding="utf-8")
    functions = list(UI_ID_FUNCTION_RE.finditer(text))
    declared = re.findall(r"^[^\S\n]*[^\s(/][^\n(]*\s\w+AddressLibraryId\(", text, re.M)
    if len(functions) != len(declared):
        raise ValueError(f"{path.name}: parsed {len(functions)} of {len(declared)} AddressLibraryId functions")
    found = set()
    for function in functions:
        cases = UI_ID_CASE_RE.findall(function.group(0))
        returns = re.findall(r"\breturn\s+(?!std::nullopt)", function.group(0))
        if len(cases) != len(returns):
            raise ValueError(f"{path.name}: {function.group(1)} has {len(returns)} id returns but {len(cases)} parsed cases")
        for version, engine_id in cases:
            if version not in UI_RUNTIMES:
                raise ValueError(f"{path.name}: {function.group(1)} has unsupported runtime {version}")
            found.add((function.group(1), UI_RUNTIMES[version], int(engine_id)))
    return found


def source_literals(path):
    text = path.read_text(encoding="utf-8")
    return {int(value) for group in LITERAL_RE.findall(text) for value in re.findall(r"\d+", group)}


def scan_sources():
    found = set()
    for directory in SCAN_DIRS:
        for path in sorted((CO_BETA_ROOT / directory).rglob("*")):
            if path.is_file() and path.suffix in SCAN_SUFFIXES:
                relative = path.relative_to(CO_BETA_ROOT).as_posix()
                found |= {(relative, value) for value in source_literals(path)}
    return found


def bin_rvas(path, wanted):
    data = path.read_bytes()
    count = struct.unpack_from("<Q", data, 0)[0]
    if len(data) != 8 + 16 * count:
        raise ValueError(f"{path.name}: size {len(data)} does not match count {count}")
    found = {}
    for engine_id, rva in struct.iter_unpack("<QQ", memoryview(data)[8:]):
        if engine_id in wanted:
            found[engine_id] = rva
    return found


class EngineIdLockTests(unittest.TestCase):
    def setUp(self):
        self.lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
        self.rows = self.lock["rows"]

    def test_every_source_literal_has_a_lock_row(self):
        locked = {(row["file"], row["id"]) for row in self.rows}
        self.assertEqual(sorted(scan_sources() - locked), [])

    def test_every_lock_row_literal_is_still_in_its_file(self):
        for row in self.rows:
            with self.subTest(symbol=row["symbol"], id=row["id"]):
                path = CO_BETA_ROOT / row["file"]
                self.assertTrue(path.is_file(), row["file"])
                self.assertIn(row["id"], source_literals(path))

    def test_ui_source_ids_match_ui_lock_rows(self):
        self.assertTrue(self.lock["ui_rows"])
        keys = [(row["file"], row["symbol"], row["id"]) for row in self.lock["ui_rows"]]
        self.assertEqual(len(keys), len(set(keys)))
        files = {row["file"] for row in self.lock["ui_rows"]}
        source = set().union(*(ui_source_ids(REPO_ROOT / name) for name in files))
        locked = {(row["symbol"], runtime, row["id"]) for row in self.lock["ui_rows"] for runtime in row["runtimes"]}
        self.assertEqual(sorted(source ^ locked), [])

    def test_ui_files_using_engine_ids_are_all_locked(self):
        locked = {row["file"] for row in self.lock["ui_rows"]}
        uses_ids = set()
        for path in sorted(p for directory in ("src", "include") for p in (REPO_ROOT / "CommonwealthOnlineUI" / directory).rglob("*")):
            if path.suffix not in SCAN_SUFFIXES:
                continue
            text = path.read_text(encoding="utf-8")
            if UI_ID_FUNCTION_RE.search(text) or LITERAL_RE.search(text):
                uses_ids.add(path.relative_to(REPO_ROOT).as_posix())
        self.assertEqual(sorted(uses_ids - locked), [])

    def test_rows_are_unique_with_nonzero_id_and_rva_per_runtime(self):
        keys = [(row["file"], row["id"]) for row in self.rows]
        self.assertEqual(len(keys), len(set(keys)))
        for row in self.rows + self.lock["ui_rows"]:
            with self.subTest(symbol=row["symbol"], id=row["id"]):
                self.assertGreater(row["id"], 0)
                self.assertTrue(row["runtimes"])
                self.assertLessEqual(set(row["runtimes"]), {"OG", "NG", "AE"})
                self.assertEqual(set(row["rva"]), set(row["runtimes"]))
                for runtime in row["runtimes"]:
                    self.assertGreater(int(row["rva"][runtime], 16), 0)

    def test_bin_names_in_preflight_and_readme_match_the_lock(self):
        locked = {entry["file"] for entry in self.lock["header"]["bins"].values()}
        for name in ("co-core/src/session/F4TInstallPreflight.cpp", "CO-BETA/README.md"):
            with self.subTest(file=name):
                text = (REPO_ROOT / name).read_text(encoding="utf-8")
                self.assertEqual(set(BIN_NAME_RE.findall(text)), locked)

    def test_rvas_match_address_library_bins(self):
        directory = Path(os.environ.get("CO_ADDRESS_BINS", DEFAULT_BIN_DIR))
        bins = self.lock["header"]["bins"]
        if not all((directory / entry["file"]).is_file() for entry in bins.values()):
            self.skipTest(f"Address Library bins not found in {directory} (set CO_ADDRESS_BINS)")
        for runtime, entry in bins.items():
            path = directory / entry["file"]
            with self.subTest(runtime=runtime, bin=entry["file"]):
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), entry["sha256"])
                rows = [row for row in self.rows + self.lock["ui_rows"] if runtime in row["runtimes"]]
                found = bin_rvas(path, {row["id"] for row in rows})
                for row in rows:
                    self.assertEqual(found.get(row["id"]), int(row["rva"][runtime], 16), row["symbol"])


if __name__ == "__main__":
    unittest.main()
