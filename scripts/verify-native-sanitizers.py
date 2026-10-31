import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--build-dir", type=Path, required=True)
parser.add_argument("--sanitizers", nargs="+", choices=("asan", "ubsan", "tsan"), required=True)
args = parser.parse_args()
directory = args.build_dir / "linux/x86_64/debug"
for name in (
    "libCommonwealthOnlineCore.a",
    "libCommonwealthOnlineServerPersistence.a",
    "CommonwealthOnlineServerPersistenceTests",
    "CommonwealthOnlineServerPersistenceGateTests",
):
    artifact = directory / name
    assert artifact.is_file(), f"Missing sanitizer artifact: {artifact}"
    command = [os.environ.get("NM", "nm")]
    if artifact.suffix == ".a":
        command.append("--undefined-only")
    command.append(str(artifact))
    result = subprocess.run(command, capture_output=True, text=True, check=True)
    symbols = {line.split()[-1] for line in result.stdout.splitlines() if line.split()}
    counts = {sanitizer: sum(symbol.startswith("__" + sanitizer + "_") for symbol in symbols)
              for sanitizer in args.sanitizers}
    assert all(counts.values()), f"Missing sanitizer references in {artifact}: {counts}"
    print(json.dumps({"artifact": str(artifact), "sha256": hashlib.sha256(artifact.read_bytes()).hexdigest(),
                      "sanitizer_symbols": counts}))
