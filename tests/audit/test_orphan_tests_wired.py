from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

TEST_GLOBS = (
    "tests/**/*.py",
    "Website/test/*.test.js",
    "supabase/**/*.test.ts",
    "supabase/tests/*.py",
)

KNOWN_UNWIRED = {
    "supabase/tests/directory_quota_probe.py",
}


def referenced_text() -> str:
    sources = list((ROOT / ".github" / "workflows").glob("*.yml")) + [ROOT / "Website" / "package.json"]
    return "\n".join(source.read_text(encoding="utf-8") for source in sources)


def test_every_test_is_wired_or_known() -> None:
    text = referenced_text()
    orphans = []
    for pattern in TEST_GLOBS:
        for path in sorted(ROOT.glob(pattern)):
            relative = path.relative_to(ROOT).as_posix()
            if "node_modules" in path.parts or path.name == "__init__.py" or relative in KNOWN_UNWIRED:
                continue
            if path.name not in text:
                orphans.append(relative)
    assert not orphans, f"tests referenced by no workflow or Website test script: {orphans}"


def test_known_unwired_entries_exist_and_stay_unwired() -> None:
    text = referenced_text()
    for relative in sorted(KNOWN_UNWIRED):
        assert (ROOT / relative).is_file(), f"{relative} no longer exists; drop it from KNOWN_UNWIRED"
        assert Path(relative).name not in text, f"{relative} is wired now; drop it from KNOWN_UNWIRED"


def main() -> int:
    test_every_test_is_wired_or_known()
    test_known_unwired_entries_exist_and_stay_unwired()
    print("orphan test wiring contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
