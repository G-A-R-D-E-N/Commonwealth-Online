#!/usr/bin/env python3
"""test_networking_protocol_contracts_part2: consolidated contract sections.

Consolidated from 1 single-contract scripts so the group runs one process
instead of 1. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python tests/audit/test_networking_protocol_contracts_part2.py
"""
from __future__ import annotations
import os
import sys
import traceback
from pathlib import Path

_MERGE_ROOT = Path(__file__).resolve().parents[2]


class _SectionExit(Exception):
    """Raised where a moved contract used to call sys.exit()/raise SystemExit."""

    def __init__(self, code=None):
        super().__init__(code)
        self.code = code


def _merge_exit(code=None):
    raise _SectionExit(code)


def _run_section(name, section):
    cwd = os.getcwd()
    environ = dict(os.environ)
    ok = True
    try:
        section()
    except _SectionExit as exc:
        ok = exc.code in (0, None)
        if not ok:
            print("FAIL: " + name + " exited with " + repr(exc.code))
    except BaseException as exc:  # noqa: BLE001 - a bad contract must not stop the rest
        if isinstance(exc, KeyboardInterrupt):
            raise
        traceback.print_exc()
        ok = False
    finally:
        os.chdir(cwd)
        if dict(os.environ) != environ:
            os.environ.clear()
            os.environ.update(environ)
    print(("PASS " if ok else "FAIL ") + name)
    return ok




def _section_008_test_mutation_ring_decoder():
    """Contract moved verbatim from tests/test_mutation_ring_decoder.py."""
    __file__ = str(_MERGE_ROOT / "tests/test_mutation_ring_decoder.py")
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
        tests = [(name, value) for name, value in sorted(_merge_ns.items()) if name.startswith("test_") and callable(value)]
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


    _merge_ns = dict(locals())
    if True:
        _merge_exit(main())

_SECTIONS = [
    ("tests/test_mutation_ring_decoder.py", _section_008_test_mutation_ring_decoder),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "test_networking_protocol_contracts_part2: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
