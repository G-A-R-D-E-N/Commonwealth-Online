from pathlib import Path

SOURCE = (Path(__file__).resolve().parents[2] / "scripts/windows-compile-ci.py").read_text(encoding="utf-8")


def test_compile_only_flags_never_reach_release_or_tests():
    assert 'COMPILE_ONLY_CL = "/Od /Z7"' in SOURCE
    assert SOURCE.count('os.environ["_CL_"]') == 1
    assert 'if args.component in (*dlls, "ui"):\n        os.environ["_CL_"] = COMPILE_ONLY_CL' in SOURCE


if __name__ == "__main__":
    test_compile_only_flags_never_reach_release_or_tests()
    print("windows compile-only flags ok")
