from pathlib import Path
import re
import sys


def verify(root):
    def read(relative):
        path = root / relative
        if not path.is_file() or not path.stat().st_size:
            raise ValueError(f"required transport-contract file is missing: {relative}")
        return path.read_text(encoding="utf-8")

    def require(condition, message):
        if not condition:
            raise ValueError(message)

    plugin = root / "CO-BETA/plugin"
    xmake = read("CO-BETA/plugin/xmake.lua")
    files = ["F4TNetworking.cpp", "F4TNetworkingPacketReaders.cpp",
             "F4TNetworkingInbound.cpp", "F4TNetworkingOutbound.cpp"]
    configured_files = ["F4TNetworking.cpp", "F4TNetworkingOutbound.cpp"]
    generic = re.search(r'add_files\("src/\*\*\.cpp([^"\n]*)"\)', xmake)
    require(generic is not None, "generic plugin source glob is missing")
    exclusions = generic[1].split("|")[1:]
    for name in files:
        read("CO-BETA/plugin/src/" + name)
        require((name in exclusions) == (name in configured_files),
                f"generic plugin source glob exclusion is wrong for {name}")
        require(not any(f'"src/{name}"' in match[1] for match in re.finditer(r'remove_files\(([^)]*)\)', xmake)),
                f"{name} must compile as its own production translation unit")
    for name in configured_files:
        configured = re.search(r'add_files\("src/' + re.escape(name) + r'",\s*\{([^}]+)\}\)', xmake)
        require(configured is not None, f"missing configured networking translation unit: {name}")
        require('defines = networking_legacy_defines' in configured[1], f"missing legacy remaps: {name}")
        require('forceincludes = "F4TNetworkingGnsCompile.h"' in configured[1], f"missing forced GNS shim: {name}")
    for define in ["ConnectAndValidateServer=ConnectAndValidateServerLegacy"]:
        require(f'"{define}"' in xmake, f"missing production networking legacy symbol remap: {define}")

    compile_header = read("CO-BETA/plugin/include/F4TNetworkingGnsCompile.h")
    read("CO-BETA/plugin/include/F4TNetworkingGnsInternal.h")
    wrapper = read("CO-BETA/plugin/src/F4TNetworkingGns.cpp")
    for token in ['#define F4T_GNS_WINSOCK_REDIRECT 1', '#include "F4TGnsWinsockShim.h"',
                  '#include "F4TNetworkingGnsInternal.h"']:
        require(token in compile_header, f"missing production GNS compile shim token: {token}")
    require('#include "F4TNetworkingGnsInternal.h"' in wrapper, "GNS wrapper must share internal declarations")
    for directory in [plugin / "src", plugin / "include"]:
        for path in directory.rglob("*"):
            if path.is_file():
                require(not re.search(r'#[ \t]*include[ \t]*[<"][^>"\n]+\.cpp[>"]', path.read_text(encoding="utf-8")),
                        f"production plugin source must not textually include .cpp files: {path.relative_to(root)}")
    require(xmake.count("    add_client_sources()") == 3, "AE, NG and OG targets must use the shared source configuration")
    native_runtime = read("CO-SERVER/server-cpp/src/ServerRuntime.inc")
    native_bootstrap = read("CO-SERVER/server-cpp/src/ServerBootstrap.cpp")
    native_support = read("CO-SERVER/server-cpp/src/ServerRuntimeSupport.cpp")
    require("co_gns_server_create(" in native_bootstrap, "native authoritative server must create the GNS listener")
    for name, source in (("ServerRuntime.inc", native_runtime),
                         ("ServerBootstrap.cpp", native_bootstrap),
                         ("ServerRuntimeSupport.cpp", native_support)):
        require(not re.search(r"TcpTransport|TcpListener|SOCK_STREAM", source),
                f"unexpected native TCP production path in {name}")
    acceptance = read(".github/workflows/co-server-acceptance.yml")
    require("name: Acceptance (native GNS)" in acceptance, "canonical acceptance must be native GNS")
    require(not re.search(r"end-to-end TCP|NamedAcceptance|NpcReplayAcceptance|Server\.Acceptance\.csproj", acceptance),
            "canonical acceptance must not use a managed TCP harness")
    require(not (root / ".github/workflows/co-server-gns.yml").exists(), "superseded managed GNS workflow was restored")


if __name__ == "__main__":
    try:
        verify(Path(__file__).resolve().parents[1])
    except (ValueError, OSError, UnicodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
    print("production transport contract verified: configured networking TUs + native server + acceptance are GNS-only")
