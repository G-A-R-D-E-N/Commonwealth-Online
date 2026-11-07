import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROJECTS = (
    ("CO-BETA/ThirdParty/Addictol", "Addictol", ".Build/F4SE/Plugins/Addictol.dll",
     ("F4SEPlugin_Load", "F4SEPlugin_Preload", "F4SEPlugin_Query", "F4SEPlugin_Version")),
    ("CO-BETA/ThirdParty/AddictolCrashLogger", "AddictolCrashLogger", "build/windows/x64/release/AddictolCrashLogger.dll",
     ("F4SEPlugin_Load", "F4SEPlugin_Version")),
)


def tool(name):
    found = shutil.which(name)
    if not found:
        raise RuntimeError(f"Required tool is missing: {name}")
    return found


def run(*args, cwd, capture=False):
    print("+", subprocess.list2cmdline([str(arg) for arg in args]), flush=True)
    return subprocess.run([str(arg) for arg in args], cwd=cwd, check=True, text=True, capture_output=capture).stdout


def build(relative, target, output, exports):
    project = ROOT / relative
    if not (project / "xmake.lua").is_file():
        raise RuntimeError(f"{relative} is not checked out; initialize its submodules first")
    xmake = tool("xmake")
    run(xmake, "f", "-y", "-p", "windows", "-a", "x64", "-m", "release", cwd=project)
    run(xmake, "build", "-y", target, cwd=project)
    dll = project / output
    if not dll.is_file():
        raise RuntimeError(f"{target} build did not produce {dll}")
    listing = run(tool("dumpbin"), "/exports", dll, cwd=project, capture=True)
    missing = [name for name in exports if name not in listing]
    if missing:
        raise RuntimeError(f"{dll.name} is missing exports: {', '.join(missing)}")
    print(f"PASS: {dll.name}: exports {', '.join(exports)}", flush=True)
    return dll


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: windows-addictol-ci.py <outdir>")
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for relative, target, output, exports in PROJECTS:
        shutil.copy2(build(relative, target, output, exports), out)


if __name__ == "__main__":
    main()
