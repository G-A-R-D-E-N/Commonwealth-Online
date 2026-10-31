import argparse
from pathlib import Path
import re

parser = argparse.ArgumentParser()
parser.add_argument("--workflow", type=Path, default=Path(__file__).resolve().parents[1] / ".github/workflows/co-native-ci.yml")
args = parser.parse_args()
workflow = args.workflow.read_text(encoding="utf-8")
job = workflow.split("\n  linux-tests:", 1)[1].split("\n  sanitizers:", 1)[0]
names = re.findall(r"^      - name: (.+)$", job, re.MULTILINE)
positions = {name: index for index, name in enumerate(names)}
setup = positions["Set up xmake"]
guard = positions["Initialize canonical CommonLib"]
for name in ("Test shared core", "Test C++ server protocol and persistence"):
    assert setup < positions[name] < guard, f"{name} must run before plugin-only credentials"
assert guard < positions["Test plugin integration"]
commonlib = job.split("      - name: Initialize canonical CommonLib", 1)[1].split("      - name:", 1)[0]
assert "secrets." not in commonlib, "CommonLib comes from public GitHub; its checkout needs no credentials"
assert 'submodule update --init --recursive --force --checkout -- CO-BETA/plugin/lib/commonlibf4' in commonlib
print("Native Linux independent core/server before guarded plugin: PASS")
