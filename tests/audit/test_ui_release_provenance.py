import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "CO-BETA" / "ci" / "package-release.sh"
PROVENANCE_FIELDS = {"sourceCommit", "uiSourceDirty", "uiDllSha256", "uiViewsFileCount", "uiViewsSha256"}


def function_source() -> str:
    text = SCRIPT.read_text(encoding="utf-8").replace("\r\n", "\n")
    match = re.search(r"^write_ui_provenance\(\) \{\n.*?^PY\n\}\n", text, re.S | re.M)
    assert match, "write_ui_provenance is missing from package-release.sh"
    return match.group(0)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(*args: str) -> str:
    return subprocess.run(["git", "-C", str(ROOT), *args], check=True, capture_output=True, text=True).stdout


def test_write_ui_provenance_records_source_and_artifact_hashes():
    bash = shutil.which("bash")
    assert bash, "bash is required to run write_ui_provenance"
    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        data = tmp_dir / "Data"
        plugins = data / "F4SE" / "Plugins"
        views = data / "PrismaUI_F4" / "views"
        (views / "CommonwealthOnline" / "MapMarkers").mkdir(parents=True)
        plugins.mkdir(parents=True, exist_ok=True)
        dll = plugins / "CommonwealthOnlineUI.dll"
        dll.write_bytes(b"ui-dll")
        index = views / "index.html"
        index.write_bytes(b"<html>")
        marker = views / "CommonwealthOnline" / "MapMarkers" / "marker.png"
        marker.write_bytes(b"png")

        shim_dir = tmp_dir / "bin"
        shim_dir.mkdir()
        shim = shim_dir / "python3"
        shim.write_text(f'#!/bin/sh\nexec "{Path(sys.executable).as_posix()}" "$@"\n', encoding="utf-8", newline="\n")
        shim.chmod(0o755)

        runner = tmp_dir / "run.sh"
        runner.write_text(
            "set -euo pipefail\n"
            + function_source()
            + f'REPO_DIR="{ROOT.as_posix()}"\n'
            + f'write_ui_provenance "{data.as_posix()}"\n',
            encoding="utf-8",
            newline="\n",
        )
        env = dict(os.environ, PATH=f"{shim_dir}{os.pathsep}{os.environ['PATH']}")
        subprocess.run([bash, str(runner)], check=True, capture_output=True, text=True, env=env)

        provenance = json.loads((plugins / "CommonwealthOnlineUI.provenance.json").read_text(encoding="utf-8"))
        assert set(provenance) == PROVENANCE_FIELDS, sorted(provenance)
        assert provenance["sourceCommit"] == git("rev-parse", "HEAD").strip()
        assert provenance["uiSourceDirty"] is (git("status", "--porcelain", "--", "CommonwealthOnlineUI").strip() != "")
        assert provenance["uiDllSha256"] == sha256(dll)
        views_lines = sorted(
            [
                f"index.html\t{sha256(index)}\n",
                f"CommonwealthOnline/MapMarkers/marker.png\t{sha256(marker)}\n",
            ]
        )
        assert provenance["uiViewsFileCount"] == 2
        assert provenance["uiViewsSha256"] == hashlib.sha256("".join(views_lines).encode("utf-8")).hexdigest()


if __name__ == "__main__":
    test_write_ui_provenance_records_source_and_artifact_hashes()
    print("UI release provenance contract passed")
