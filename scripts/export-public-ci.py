#!/usr/bin/env python3
"""Build the commit that public CI runs: one ref's tree minus private files, with no history.

Usage: export-public-ci.py <ref> <message>   (prints the new commit SHA)
"""
import os
import re
import subprocess
import tempfile
import sys
from fnmatch import fnmatchcase
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

DENY = (
    "docs/*",
    "wiki/*",
    "*/docs/*",
    "AGENTS.md",
    "*/AGENTS.md",
    "CLAUDE.md",
    "*/CLAUDE.md",
    ".claude/*",
    "*/.claude/*",
    ".codex/*",
    "*/.codex/*",
    "Commonwealth Online Roadmap - Backlog.tsv",
    ".github/workflows/*",
    ".github/public-ci/*",
)

PUBLIC_WORKFLOWS = ".github/public-ci/workflows/"

# Contract tests that read docs/ or Address Library bins; private-only-checks.yml runs them, and the export drops them and their steps.
PRIVATE_TESTS = (
    "CommonwealthOnlineUI/ci/CharacterCreatorSourcePins.py",
    "CO-BETA/tools/tests/test_md_links.py",
    "CO-BETA/tools/tests/test_engine_ids.py",
    "CO-BETA/plugin/tests/F4TChatContract.py",
    "CO-BETA/plugin/tests/F4TDownedContract.py",
    "CO-BETA/plugin/tests/F4TFactionsContract.py",
    "CO-BETA/plugin/tests/F4TFrameLimiterContract.py",
    "CO-BETA/plugin/tests/F4TNpcCoopCombatContract.py",
    "CO-BETA/plugin/tests/F4TProxySaveSafetyContract.py",
    "CO-BETA/plugin/tests/F4TSavePluginPreflightContract.py",
    "CO-BETA/plugin/tests/F4TSessionSaveGuardContract.py",
    "CO-BETA/plugin/tests/F4TTextEntryModeContract.py",
    "CO-BETA/plugin/tests/F4TLoadOrderContract.py",
    "CO-BETA/plugin/tests/F4TGraphOverrideContract.py",
    "CO-BETA/plugin/tests/F4TProxyLiveFireContract.py",
    "CO-BETA/plugin/tests/F4TProxyNoTalkContract.py",
    "CO-BETA/plugin/tests/F4TShotReplayContract.py",
    "tests/audit/test_character_slots_client_contract.py",
    "tests/audit/test_character_slots_core_contract.py",
    "tests/audit/test_character_slots_server_contract.py",
    "tests/audit/test_faction_list_server_contract.py",
    "tests/audit/test_settlement_capture_contract.py",
    "tests/audit/test_workshop_server_contract.py",
)


def git(*args, env=None, input=None):
    data = input.encode("utf-8") if input is not None else None
    out = subprocess.run(["git", *args], cwd=ROOT, env=env, input=data, capture_output=True, check=True).stdout
    return out.decode("utf-8")


def exported(path):
    return path not in PRIVATE_TESTS and not any(fnmatchcase(path, pattern) for pattern in DENY)


def public_workflow(sha, name):
    text = git("cat-file", "blob", sha)
    for test in PRIVATE_TESTS:
        for ref in {test, test.removeprefix("CO-BETA/")}:
            text = re.sub(rf"(?m)^      - name: .*\n        run: python3 {re.escape(ref)}\n\n?", "", text)
        if Path(test).name in text:
            raise SystemExit(f"{name} still refers to private test {test}")
    return git("hash-object", "-w", "--stdin", input=text).strip()


def main():
    ref, message = sys.argv[1], sys.argv[2]
    entries = []
    for line in git("ls-tree", "-r", "--full-tree", ref).splitlines():
        meta, path = line.split("\t", 1)
        mode, kind, sha = meta.split()
        if path.startswith(PUBLIC_WORKFLOWS) and path.endswith(".yml"):
            entries.append(f"100644 {public_workflow(sha, path)}\t.github/workflows/{path[len(PUBLIC_WORKFLOWS):]}")
        elif exported(path):
            entries.append(f"{mode} {sha}\t{path}")
    paths = [entry.split("\t", 1)[1] for entry in entries]
    for required in ("co-core/", "CO-SERVER/", "CO-BETA/plugin/", ".github/workflows/co-native-ci.yml"):
        if not any(path.startswith(required) for path in paths):
            raise SystemExit(f"export is missing {required}")
    leaked = [path for path in paths if path.startswith(("docs/", "wiki/")) or "/docs/" in path or path.endswith("AGENTS.md")
              or "code-notes" in path or "Roadmap" in path]
    if leaked:
        raise SystemExit(f"export would publish internal files: {leaked[:5]}")

    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, GIT_INDEX_FILE=str(Path(tmp) / "index"))
        git("update-index", "--add", "--index-info", env=env, input="\n".join(entries) + "\n")
        tree = git("write-tree", env=env).strip()
    print(git("commit-tree", tree, "-m", message).strip())


if __name__ == "__main__":
    main()
