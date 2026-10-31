import glob
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHIPPING_SOURCES = "src/**.cpp"
PRODUCTION_GLOB = "src/**/*.cpp"

shipping_xmake = (ROOT / "xmake.lua").read_text(encoding="utf-8")
check_xmake = (ROOT / "xmake-ci.lua").read_text(encoding="utf-8")


def target_body(text, name):
    start = text.index(f'target("{name}")')
    end = text.find('\ntarget("', start + 1)
    return text[start:] if end == -1 else text[start:end]


shipping_body = target_body(shipping_xmake, "CommonwealthOnlineUI")
check_body = target_body(check_xmake, "CommonwealthOnlineUIProductionCompileCheck")

assert f'add_files("{SHIPPING_SOURCES}")' in shipping_body, "shipping target no longer globs src/**.cpp"

production = {Path(path).resolve().relative_to(ROOT).as_posix() for path in glob.glob(str(ROOT / PRODUCTION_GLOB), recursive=True)}
assert production, "no production sources found under src/"

covered = set()
for call, args in re.findall(r"(add_files|del_files)\((.*?)\)", check_body, re.S):
    for name in re.findall(r'"([^"]+)"', args):
        paths = {Path(path).resolve().relative_to(ROOT).as_posix() for path in glob.glob(str(ROOT / name), recursive=True)}
        covered = covered | paths if call == "add_files" else covered - paths

missing = sorted(production - covered)
assert not missing, f"production sources missing from CommonwealthOnlineUIProductionCompileCheck: {missing}"

print(f"production compile coverage contract passed: {len(covered & production)}/{len(production)} production sources")
