#!/usr/bin/env python3
"""Run the repository plain-Python test groups, one process per file."""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

GROUPS = {
    "audit": ("tests/audit/*.py", "tests/*.py"),
    "plugin": ("CO-BETA/plugin/tests/*.py",),
    "tools": ("CO-BETA/tools/tests/test_*.py",),
    "scripts": ("scripts/test-*.py",),
    "ui": ("CommonwealthOnlineUI/ci/*Tests.py",),
}

COMPILE_RE = re.compile(r"clang\+\+|--cmake|--compiler|[\"']cmake[\"']")


def selected(groups):
    paths = set()
    for group in groups:
        for pattern in GROUPS[group]:
            paths.update(ROOT.glob(pattern))
    return sorted(paths)


def relative(path):
    return path.relative_to(ROOT).as_posix()


def compiles_cpp(path):
    return bool(COMPILE_RE.search(path.read_text(encoding="utf-8", errors="replace")))


def run_file(path):
    name = relative(path)
    print(f"::group::{name}", flush=True)
    try:
        process = subprocess.Popen(
            [sys.executable, str(path)], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace", bufsize=1,
        )
        for line in process.stdout:
            print(line, end="", flush=True)
        code = process.wait()
    finally:
        print("::endgroup::", flush=True)
    if code == 0:
        print(f"PASS {name}", flush=True)
        return True
    print(f"::error file={name}::{name} failed with exit code {code}", flush=True)
    print(f"FAIL {name}", flush=True)
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--group", action="append", choices=sorted(GROUPS))
    parser.add_argument("--exclude", action="append", default=[], metavar="PATH")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--fail-fast", action="store_true")
    parser.add_argument("--compile-only", action="store_true")
    args = parser.parse_args()

    groups = sorted(set(args.group)) if args.group else sorted(GROUPS)
    excluded = {Path(item.replace("\\", "/")).as_posix() for item in args.exclude}
    excluded.discard(".")

    failed_groups = []
    files_by_group = {}
    for group in groups:
        candidates = selected((group,))
        if args.compile_only:
            candidates = [path for path in candidates if compiles_cpp(path)]
        files = [path for path in candidates if relative(path) not in excluded]
        files_by_group[group] = files
        if not files and not (args.compile_only and candidates and all(relative(path) in excluded for path in candidates)):
            failed_groups.append(group)
            print(f"::error::{group} matched no test files", flush=True)

    matched = {relative(path) for group in groups for pattern in GROUPS[group] for path in ROOT.glob(pattern)}
    for name in sorted(excluded - matched):
        failed_groups.append(name)
        print(f"::error::--exclude {name} matched no test file", flush=True)

    if args.list:
        for group in groups:
            for path in files_by_group[group]:
                print(f"{group} {relative(path)}")
        return 1 if failed_groups else 0

    failures = []
    total = 0
    for group in groups:
        for path in files_by_group[group]:
            if sys.platform != "win32" and compiles_cpp(path):
                print(f"SKIP {relative(path)} (compiles C++; runs on Windows only)", flush=True)
                continue
            total += 1
            if not run_file(path):
                failures.append(relative(path))
                if args.fail_fast:
                    break
        if args.fail_fast and failures:
            break

    for group in groups:
        files = files_by_group[group]
        print(f"{group}: {len(files)} file(s)", flush=True)
    print(f"ran {total} file(s); {total - len(failures)} passed, {len(failures)} failed", flush=True)
    for name in failures:
        print(f"FAILED {name}", flush=True)
    if failed_groups:
        print(f"FAIL: empty groups or unmatched excludes: {', '.join(sorted(set(failed_groups)))}", flush=True)
    print("RESULT: " + ("PASS" if not failures and not failed_groups else "FAIL"), flush=True)
    return 1 if failures or failed_groups else 0


if __name__ == "__main__":
    sys.exit(main())
