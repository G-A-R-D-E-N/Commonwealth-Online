#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys
from pathlib import Path

workflow = Path(__file__).resolve().parents[1] / ".github/public-ci/sync/public-ci-sync.yml"
if not workflow.exists() and os.environ.get("GITHUB_REPOSITORY") == "G-A-R-D-E-N/Commonwealth-Online":
    print("SKIP: private sync workflow is excluded from the public snapshot")
    sys.exit(0)
text = workflow.read_text(encoding="utf-8")
start = text.index("          export_one() {")
end = text.index("\n          }", start) + len("\n          }")
function = "\n".join(line[10:] for line in text[start:end].splitlines())
shell = shutil.which("bash") or r"C:\Program Files\Git\bin\bash.exe"
prefix = r'''set -u
PUBLIC_REPO=public REPOSITORY=private TOKEN=x private_url=private exporter=abc work=/ignored FORCE=${FORCE:-false}
awk() { :; }
gh() { case "$*" in *'// ""'*) printf '%s\n' "$REVISION";; *) printf '%s\n' "$STATES";; esac; }
git() { echo EXPORT_ATTEMPT >&2; return 1; }
sleep() { :; }
'''
cases = [
    ("success", "Exporter abc", "false", "1", False),
    ("success", "Exporter old", "false", "1", True),
    ("success", "", "false", "1", True),
    ("pending", "Exporter abc", "false", "1", True),
    ("success", "Exporter abc", "true", "1", True),
    ("success", "Exporter abc", "false", "", True),
]
for states, revision, force, pr, expected in cases:
    env = dict(os.environ, STATES=states, REVISION=revision, FORCE=force)
    result = subprocess.run([shell, "-c", prefix + function + f'\nexport_one ci/test abc "{pr}"\n'],
                            env=env, capture_output=True, text=True)
    assert ("EXPORT_ATTEMPT" in result.stderr) == expected, (states, revision, force, pr, result.stderr)
print("public CI snapshot refresh checks passed")
