from pathlib import Path
import re
import runpy
import tempfile

ROOT = Path(__file__).resolve().parents[2]
verify = runpy.run_path(str(ROOT / "scripts/verify-production-transport.py"))["verify"]
XMAKE = "CO-BETA/plugin/xmake.lua"
PLUGIN_SOURCES = ["CO-BETA/plugin/src/" + name for name in (
    "F4TNetworking.cpp", "F4TNetworkingPacketReaders.cpp", "F4TNetworkingInbound.cpp",
    "F4TNetworkingOutbound.cpp", "F4TNetworkingGns.cpp")]
GNS_COMPILE = "CO-BETA/plugin/include/F4TNetworkingGnsCompile.h"
GNS_INTERNAL = "CO-BETA/plugin/include/F4TNetworkingGnsInternal.h"
RUNTIME = "CO-SERVER/server-cpp/src/ServerRuntime.inc"
BOOTSTRAP = "CO-SERVER/server-cpp/src/ServerBootstrap.cpp"
RUNTIME_SUPPORT = "CO-SERVER/server-cpp/src/ServerRuntimeSupport.cpp"
ACCEPTANCE = ".github/workflows/co-server-acceptance.yml"
FILES = [XMAKE, *PLUGIN_SOURCES, GNS_COMPILE, GNS_INTERNAL, RUNTIME, BOOTSTRAP, RUNTIME_SUPPORT, ACCEPTANCE]
with tempfile.TemporaryDirectory(prefix="co-production-transport-") as directory:
    root = Path(directory)
    for name in FILES:
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes((ROOT / name).read_bytes())
    verify(root)
    glob = re.compile(r'(add_files\("src/\*\*\.cpp[^"\n]*)(?="\))')
    xmake_path = root / XMAKE
    xmake_path.write_text(glob.sub(r'\1|UnrelatedProbe*.cpp', xmake_path.read_text(encoding="utf-8"), count=1), encoding="utf-8")
    verify(root)
    rejected = 0

    def reject(name, change):
        global rejected
        path = root / name
        previous = path.read_text(encoding="utf-8")
        changed = change(previous)
        assert changed != previous
        path.write_text(changed, encoding="utf-8")
        try:
            try:
                verify(root)
            except ValueError:
                rejected += 1
            else:
                raise AssertionError(f"accepted invalid transport contract: {name}")
        finally:
            path.write_text(previous, encoding="utf-8")

    for source in ("F4TNetworking.cpp", "F4TNetworkingOutbound.cpp"):
        reject(XMAKE, lambda text, source=source: text.replace("|" + source, "", 1))
        reject(XMAKE, lambda text, source=source: text.replace(
            f'add_files("src/{source}", {{\n        defines = networking_legacy_defines,',
            f'add_files("src/{source}", {{\n        defines = {{}},', 1))
        reject(XMAKE, lambda text, source=source: text.replace(
            f'add_files("src/{source}", {{\n        defines = networking_legacy_defines,\n        forceincludes = "F4TNetworkingGnsCompile.h"',
            f'add_files("src/{source}", {{\n        defines = networking_legacy_defines,\n        forceincludes = "incorrect.h"', 1))
    reject(XMAKE, lambda text: glob.sub(r'\1|F4TNetworkingInbound.cpp', text, count=1))
    reject(XMAKE, lambda text: text + '\nremove_files("src/F4TNetworkingInbound.cpp", "src/F4TNetworkingOutbound.cpp")\n')
    reject(XMAKE, lambda text: text.replace('    add_client_sources()', '    missing_sources()', 1))
    reject(RUNTIME, lambda text: text + "\nTcpListener\n")
    reject(BOOTSTRAP, lambda text: text + "\nSOCK_STREAM\n")
    reject(RUNTIME_SUPPORT, lambda text: text + "\nTcpTransport\n")
    reject(BOOTSTRAP, lambda text: text.replace("co_gns_server_create(", "co_gns_server_missing(", 1))
    reject(ACCEPTANCE, lambda text: text + "\nNamedAcceptance\n")
    reject(PLUGIN_SOURCES[0], lambda text: text + '\n#include "hidden.cpp"\n')
    reject(GNS_COMPILE, lambda text: text.replace('#define F4T_GNS_WINSOCK_REDIRECT 1', '#define F4T_GNS_WINSOCK_REDIRECT 0'))
    missing = root / GNS_INTERNAL
    previous = missing.read_bytes()
    missing.unlink()
    try:
        try:
            verify(root)
        except ValueError:
            rejected += 1
        else:
            raise AssertionError("accepted missing GNS internal header")
    finally:
        missing.write_bytes(previous)
    legacy = root / ".github/workflows/co-server-gns.yml"
    legacy.write_text("legacy", encoding="utf-8")
    try:
        try:
            verify(root)
        except ValueError:
            rejected += 1
        else:
            raise AssertionError("accepted restored managed GNS workflow")
    finally:
        legacy.unlink()
    verify(root)
    assert rejected == 18
print("production transport contract: valid split and 15 rejected mutations PASS")
