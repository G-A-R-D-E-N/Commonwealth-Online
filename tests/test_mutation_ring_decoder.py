# source:co-core/include/F4TMutationRing.h CO-defined dump layout
import inspect
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_mutation_ring_decoder_self_test_decodes_committed_fixture():
    result = subprocess.run(
        [sys.executable, str(ROOT / "scripts/decode-mutation-ring.py"), "--self-test"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "self-test PASS" in result.stdout
    assert "8 records" in result.stdout


def main() -> int:
    failures: list[str] = []
    tests = [(name, value) for name, value in sorted(globals().items()) if name.startswith("test_") and callable(value)]
    for name, func in tests:
        try:
            if "tmp_path" in inspect.signature(func).parameters:
                with tempfile.TemporaryDirectory() as directory:
                    func(Path(directory))
            else:
                func()
        except AssertionError as exc:
            failures.append(f"{name}: {exc}")
        except Exception as exc:  # noqa: BLE001 - report, don't hide, unexpected errors
            failures.append(f"{name}: unexpected {type(exc).__name__}: {exc}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print(f"{Path(__file__).stem}: {len(tests)} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
