#!/usr/bin/env bash
# Builds the CommonwealthOnline release assets for one version tag.
#
# This reproduces the layout of the published 0.0.0.x releases, which is NOT
# CO-BETA/stage-mod.bat's: the runtime archives root at Data/ and carry only the
# runtime-specific plugin DLL, the GNS bridge, the packaged client manifest and
# the repo content. The UI plugin ships as its own CommonwealthOnlineUI archive
# instead of inside the runtime archives. Verified against the 0.0.0.3 assets:
#
#   CommonwealthOnline-AE|NG|OG-<version>.zip  ->  Data/...
#     Data/CommonwealthOnline.esp
#     Data/F4SE/Plugins/CommonwealthOnline.dll (AE) | -NG.dll | -OG.dll
#     Data/F4SE/Plugins/commonwealth_online_gns_bridge.dll
#     Data/F4SE/Plugins/CommonwealthOnlineClient.json
#     Data/Scripts/...  Data/Materials/...  Data/Meshes/...  Data/Textures/...
#   CommonwealthOnlineUI-<version>.zip         ->  Data/F4SE/Plugins/CommonwealthOnlineUI.dll + Data/PrismaUI_F4/...
#   CommonwealthOnline.Host-linux-x64-<version>.tar.gz  ->  package contents at the archive root
#   CommonwealthOnline.Host-win-x64-<version>.tar.gz
#
# Inputs (environment):
#   CO_VERSION              required, e.g. 0.0.0.4; used only in asset filenames
#   CO_OUT                  output directory (default: <repo>/release-out)
#   CO_BUILD_UI            1 to also build and package the UI archive (default 0)
#   CO_BUILD_HOST_PACKAGES 1 to also build the host archives (default 1)
#   XWIN_ROOT               xwin CRT/SDK tree (default /opt/xwin)
#   PRISMA_WIN_PACKAGES     staged Windows xmake packages (default /opt/prisma-toolchain/win-packages)
#
# Requires: Linux host, clang-cl/lld-link/llvm-*/cmake/ninja/protoc/xmake on PATH,
# python3, and the CO-BETA/plugin/lib/commonlibf4 + CO-BETA/ThirdParty/
# GameNetworkingSockets-src submodules checked out. dotnet 8 is additionally
# required when CO_BUILD_HOST_PACKAGES=1. Prints each produced asset path on stdout.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CO_BETA_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
REPO_DIR="$(cd -- "$CO_BETA_DIR/.." && pwd)"

VERSION="${CO_VERSION:-}"
OUT_DIR="${CO_OUT:-$REPO_DIR/release-out}"
BUILD_UI="${CO_BUILD_UI:-0}"
BUILD_HOST_PACKAGES="${CO_BUILD_HOST_PACKAGES:-1}"
XWIN_ROOT="${XWIN_ROOT:-/opt/xwin}"
PRISMA_WIN_PACKAGES="${PRISMA_WIN_PACKAGES:-/opt/prisma-toolchain/win-packages}"

PLUGIN_DIR="$CO_BETA_DIR/plugin"
XPACKAGE_DIR="$REPO_DIR/CommonwealthOnlineUI"
XPACKAGE_WIN_PACKAGES="${PRISMA_WIN_PACKAGES}"
BRIDGE_DLL="$PLUGIN_DIR/build/gns-bridge/bin/commonwealth_online_gns_bridge.dll"
RUNTIME_RELEASE_DIR="$PLUGIN_DIR/build/windows/x64/release"
UI_DLL=""

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/co-release.XXXXXX")"
cleanup() { rm -rf "$WORK_DIR"; }
trap cleanup EXIT

log() { printf '[release] %s\n' "$*" >&2; }
die() { printf '[release] error: %s\n' "$*" >&2; exit 1; }

require_file() {
    [ -f "$1" ] || die "required file missing: $1"
}

require_dir() {
    [ -d "$1" ] || die "required directory missing: $1"
}

require_tool() {
    command -v "$1" >/dev/null 2>&1 || die "required tool missing from PATH: $1"
}

# Zips a directory so the archive root is the directory's own contents,
# e.g. zip_tree <dir-containing-Data> <out.zip> produces entries under Data/.
zip_tree() {
    local source_dir="$1"
    local out_zip="$2"
    require_dir "$source_dir"
    python3 - "$source_dir" "$out_zip" <<'PY'
import os
import sys
import zipfile

source, out = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as archive:
    for root, dirs, files in os.walk(source):
        dirs.sort()
        rel_root = os.path.relpath(root, source)
        if rel_root == ".":
            rel_root = ""
        # Record directory entries so empty directories survive a round trip,
        # matching what the published archives contain.
        if rel_root:
            archive.writestr(rel_root.replace(os.sep, "/") + "/", "")
        for name in sorted(files):
            path = os.path.join(root, name)
            arcname = os.path.join(rel_root, name).replace(os.sep, "/")
            archive.write(path, arcname)
PY
}

build_windows_bridge() {
    log "cross-building the Windows GNS bridge"
    local protobuf_tag="v21.12"
    local tc_hash pb_install
    tc_hash="$(sha256sum "$CO_BETA_DIR/ci/toolchains/xwin-clang-cl.cmake" | cut -c1-16)"
    pb_install="$HOME/.cache/co-release/protobuf-${protobuf_tag}-${tc_hash}"
    if [ ! -f "$pb_install/lib/libprotobuf.lib" ]; then
        log "protobuf cache miss; cross-building $protobuf_tag"
        local work="$WORK_DIR/protobuf"
        git clone --depth 1 --branch "$protobuf_tag" \
            https://github.com/protocolbuffers/protobuf.git "$work/src"
        cmake -S "$work/src" -B "$work/build" -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE="$CO_BETA_DIR/ci/toolchains/xwin-clang-cl.cmake" \
            -DCMAKE_BUILD_TYPE=Release \
            -Dprotobuf_BUILD_TESTS=OFF \
            -Dprotobuf_BUILD_PROTOC_BINARIES=OFF \
            -Dprotobuf_BUILD_SHARED_LIBS=OFF \
            -Dprotobuf_MSVC_STATIC_RUNTIME=OFF \
            -Dprotobuf_WITH_ZLIB=OFF
        cmake --build "$work/build" --parallel 4
        rm -rf "$pb_install"
        cmake --install "$work/build" --prefix "$pb_install"
    fi
    require_file "$pb_install/lib/libprotobuf.lib"

    rm -rf "$PLUGIN_DIR/build/gns-bridge"
    cmake -S "$PLUGIN_DIR/gns-bridge" -B "$PLUGIN_DIR/build/gns-bridge" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$CO_BETA_DIR/ci/toolchains/xwin-clang-cl.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DGNS_SOURCE_DIR="$CO_BETA_DIR/ThirdParty/GameNetworkingSockets-src" \
        -DCO_GNS_BUILD_TESTS=OFF \
        -DProtobuf_INCLUDE_DIR="$pb_install/include" \
        -DProtobuf_LIBRARY="$pb_install/lib/libprotobuf.lib" \
        -DProtobuf_LIBRARY_RELEASE="$pb_install/lib/libprotobuf.lib" \
        -DProtobuf_LITE_LIBRARY="$pb_install/lib/libprotobuf-lite.lib" \
        -DProtobuf_PROTOC_EXECUTABLE=/usr/bin/protoc
    cmake --build "$PLUGIN_DIR/build/gns-bridge" --parallel 4
    require_file "$BRIDGE_DLL"
}

build_plugin_dlls() {
    log "cross-building the AE, NG and OG F4SE plugin DLLs"
    (
        cd "$PLUGIN_DIR"
        rm -rf build-xwin .xmake
        xmake config --file=xmake-xwin.lua \
            --plat=windows --arch=x64 --mode=release --builddir=build-xwin \
            --yes
        xmake build --file=xmake-xwin.lua --jobs=2 CommonwealthOnline
        xmake build --file=xmake-xwin.lua --jobs=2 CommonwealthOnlineNG
        xmake build --file=xmake-xwin.lua --jobs=2 CommonwealthOnlineOG
    )

    mkdir -p "$RUNTIME_RELEASE_DIR"
    local name source
    for name in CommonwealthOnline.dll CommonwealthOnline-NG.dll CommonwealthOnline-OG.dll; do
        source="$(find "$PLUGIN_DIR/build-xwin" -type f -name "$name" -print -quit)"
        [ -n "$source" ] || die "$name was not produced by the xwin build"
        cp "$source" "$RUNTIME_RELEASE_DIR/$name"
    done
}

build_ui_package() {
    log "cross-building the UI plugin"
    require_file "$XPACKAGE_DIR/include/PrismaUI_F4_API.h"
    require_dir "$XPACKAGE_DIR/ThirdParty/CommonLibF4"
    require_dir "$XPACKAGE_DIR/ui/views"
    (
        cd "$XPACKAGE_DIR"
        rm -rf build-xwin .xmake
        xmake config --file=xmake-xwin.lua \
            --plat=windows --arch=x64 --mode=release --builddir=build-xwin \
            --yes
        xmake build --file=xmake-xwin.lua --jobs=2 CommonwealthOnlineUI
    )
    UI_DLL="$(find "$XPACKAGE_DIR/build-xwin" "$XPACKAGE_DIR/package" -type f -name CommonwealthOnlineUI.dll -print -quit 2>/dev/null || true)"
    [ -n "$UI_DLL" ] || die "the UI build produced no CommonwealthOnlineUI.dll"
}

# Stages the repo content exactly as CO-BETA/ci/stage-content.sh does (the same
# set the content-manifest fingerprint in Beta CI covers) plus the runtime
# binaries, then emits and verifies CommonwealthOnlineClient.json.
stage_runtime_data() {
    local data_dir="$1"
    log "staging runtime Data tree at $data_dir"

    local content_stage
    content_stage="$(bash "$CO_BETA_DIR/ci/stage-content.sh")"
    mkdir -p "$data_dir"
    cp -R "$content_stage"/. "$data_dir"/
    rm -rf "$content_stage"

    mkdir -p "$data_dir/F4SE/Plugins"
    cp "$RUNTIME_RELEASE_DIR/CommonwealthOnline.dll" "$data_dir/F4SE/Plugins/CommonwealthOnline.dll"
    cp "$BRIDGE_DLL" "$data_dir/F4SE/Plugins/commonwealth_online_gns_bridge.dll"

    (
        cd "$REPO_DIR/co-core"
        xmake f -y -m debug >/dev/null
        xmake build -y CommonwealthOnlineCoreContentManifestTool >/dev/null
    )

    local manifest="$data_dir/F4SE/Plugins/CommonwealthOnlineClient.json"
    local fingerprint
    fingerprint="$(cd "$REPO_DIR/co-core" && xmake run CommonwealthOnlineCoreContentManifestTool \
        --root "$data_dir" \
        --exclude-name CommonwealthOnline.dll \
        --exclude-name CommonwealthOnline-NG.dll \
        --exclude-name CommonwealthOnline-OG.dll \
        --exclude-name commonwealth_online_gns_bridge.dll \
        --emit-client-json "$manifest" | tr -d '\r' | grep -E '^[0-9a-f]{64}$' | tail -n 1)"
    [ -n "$fingerprint" ] || die "content-manifest tool returned no fingerprint"

    local verified
    verified="$(cd "$REPO_DIR/co-core" && xmake run CommonwealthOnlineCoreContentManifestTool \
        --root "$data_dir" \
        --exclude-name CommonwealthOnline.dll \
        --exclude-name CommonwealthOnline-NG.dll \
        --exclude-name CommonwealthOnline-OG.dll \
        --exclude-name commonwealth_online_gns_bridge.dll | tr -d '\r' | grep -E '^[0-9a-f]{64}$' | tail -n 1)"
    [ "$fingerprint" = "$verified" ] || die "staged content fingerprint changed after the client manifest was emitted"

    require_file "$manifest"
    log "content manifest fingerprint $fingerprint"
}

# Mirrors CO-BETA/stage-mod.bat's :ValidateReleasePayload for the shipped layout.
validate_payload() {
    local data_dir="$1"
    log "validating the staged payload"
    require_file "$data_dir/CommonwealthOnline.esp"
    require_file "$data_dir/F4SE/Plugins/CommonwealthOnline.dll"
    require_file "$data_dir/F4SE/Plugins/commonwealth_online_gns_bridge.dll"
    require_file "$data_dir/F4SE/Plugins/CommonwealthOnlineClient.json"
    require_file "$data_dir/Scripts/COCharacterSetupQuestScript.pex"
    require_file "$data_dir/Scripts/COQuestSyncScript.pex"
    require_file "$data_dir/Scripts/CommonwealthOnlineDebug.pex"
    require_file "$data_dir/Scripts/CommonwealthOnlineQuest.pex"
    require_file "$data_dir/Scripts/Source/User/COCharacterSetupQuestScript.psc"
    require_file "$data_dir/Scripts/Source/User/COQuestSyncScript.psc"
    require_file "$data_dir/Scripts/Source/User/CommonwealthOnlineDebug.psc"
    require_file "$data_dir/Scripts/Source/User/CommonwealthOnlineQuest.psc"
    require_dir "$data_dir/Materials"
    require_dir "$data_dir/Meshes"
    require_dir "$data_dir/Textures"
    require_file "$RUNTIME_RELEASE_DIR/CommonwealthOnline-NG.dll"
    require_file "$RUNTIME_RELEASE_DIR/CommonwealthOnline-OG.dll"

    local ui_dll="$data_dir/F4SE/Plugins/CommonwealthOnlineUI.dll"
    if [ -e "$ui_dll" ]; then
        die "the runtime archives must not contain CommonwealthOnlineUI.dll; the UI ships as its own archive"
    fi

    if [ -e "$data_dir/F4SE/Plugins/CommonwealthOnlineProbe.dll" ]; then
        die "the release payload must not contain CommonwealthOnlineProbe.dll; the probe is a non-shipping tooling target"
    fi
}

assemble_runtime_archives() {
    local data_dir="$1"
    log "assembling the AE, NG and OG archives"
    local runtime out
    for runtime in AE NG OG; do
        out="$WORK_DIR/root-$runtime"
        rm -rf "$out"
        mkdir -p "$out"
        cp -R "$data_dir" "$out/Data"

        # Exactly one runtime DLL per archive, named for that runtime.
        rm -f "$out/Data/F4SE/Plugins/CommonwealthOnline.dll"
        rm -f "$out/Data/F4SE/Plugins/CommonwealthOnline-NG.dll"
        rm -f "$out/Data/F4SE/Plugins/CommonwealthOnline-OG.dll"
        cp "$RUNTIME_RELEASE_DIR/CommonwealthOnline$([ "$runtime" = "AE" ] || printf -- '-%s' "$runtime").dll" \
            "$out/Data/F4SE/Plugins/"

        zip_tree "$out" "$OUT_DIR/CommonwealthOnline-$runtime-$VERSION.zip"
        log "wrote CommonwealthOnline-$runtime-$VERSION.zip"
    done
}

# Mirrors Write-UiProvenance in CO-BETA/tools/stage-client-manifest.ps1: same field
# names and the same views digest (sorted "relpath<TAB>sha256" lines, hashed as one text).
write_ui_provenance() {
    local data_dir="$1"
    python3 - "$REPO_DIR" "$data_dir" <<'PY'
import hashlib
import json
import os
import subprocess
import sys

repo, data = sys.argv[1], sys.argv[2]


def git(*args):
    return subprocess.run(["git", "-C", repo, *args], check=True, capture_output=True, text=True).stdout


def sha256(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


views = os.path.join(data, "PrismaUI_F4", "views")
lines = []
for root, _, files in os.walk(views):
    for name in files:
        path = os.path.join(root, name)
        relative = os.path.relpath(path, views).replace(os.sep, "/")
        lines.append(f"{relative}\t{sha256(path)}\n")
lines.sort()

provenance = {
    "sourceCommit": git("rev-parse", "HEAD").strip(),
    "uiSourceDirty": git("status", "--porcelain", "--", "CommonwealthOnlineUI").strip() != "",
    "uiDllSha256": sha256(os.path.join(data, "F4SE", "Plugins", "CommonwealthOnlineUI.dll")),
    "uiViewsFileCount": len(lines),
    "uiViewsSha256": hashlib.sha256("".join(lines).encode("utf-8")).hexdigest(),
}
with open(os.path.join(data, "F4SE", "Plugins", "CommonwealthOnlineUI.provenance.json"), "w", encoding="utf-8") as handle:
    json.dump(provenance, handle, indent=2)
    handle.write("\n")
PY
}

# Mirrors CommonwealthOnlineUI/scripts/stage-package.ps1, which is what the UI's
# own stage-mod.bat runs; the xmake build alone only drops the DLL in package/.
assemble_ui_archive() {
    log "assembling the UI archive"
    local out="$WORK_DIR/root-ui"
    local data="$out/Data"
    rm -rf "$out"
    mkdir -p "$data/F4SE/Plugins" "$data/PrismaUI_F4/views" "$data/PrismaUI_F4/misc"

    cp "$UI_DLL" "$data/F4SE/Plugins/CommonwealthOnlineUI.dll"

    cp -R "$XPACKAGE_DIR/ui/views"/. "$data/PrismaUI_F4/views"/
    if [ -d "$XPACKAGE_DIR/ui/MapMarkers" ]; then
        mkdir -p "$data/PrismaUI_F4/views/CommonwealthOnline/MapMarkers"
        cp -R "$XPACKAGE_DIR/ui/MapMarkers"/. "$data/PrismaUI_F4/views/CommonwealthOnline/MapMarkers"/
    fi
    if [ -d "$XPACKAGE_DIR/ui/Icons" ]; then
        mkdir -p "$data/PrismaUI_F4/views/CommonwealthOnline/shared/icons"
        cp -R "$XPACKAGE_DIR/ui/Icons"/. "$data/PrismaUI_F4/views/CommonwealthOnline/shared/icons"/
    fi
    local cursor="$XPACKAGE_DIR/ui/views/CommonwealthOnline/browser/assets/F4MouseCursor.png"
    if [ -f "$cursor" ]; then
        cp "$cursor" "$data/PrismaUI_F4/misc/cursor.png"
    else
        log "warning: F4MouseCursor.png not found; the UI archive will carry no custom cursor"
    fi
    if [ -d "$XPACKAGE_DIR/ui/shell-overrides" ]; then
        mkdir -p "$data/PrismaUI_F4/shell"
        cp -R "$XPACKAGE_DIR/ui/shell-overrides"/. "$data/PrismaUI_F4/shell"/
    fi

    require_file "$data/F4SE/Plugins/CommonwealthOnlineUI.dll"
    require_dir "$data/PrismaUI_F4/views"
    write_ui_provenance "$data"
    zip_tree "$out" "$OUT_DIR/CommonwealthOnlineUI-$VERSION.zip"
    log "wrote CommonwealthOnlineUI-$VERSION.zip"
}

assemble_host_archives() {
    local platform out_dir
    for platform in linux-x64 win-x64; do
        out_dir="$WORK_DIR/host-$platform"
        rm -rf "$out_dir"
        log "building the $platform host package"
        bash "$REPO_DIR/CO-SERVER/server-cpp/scripts/build-native-package-ci.sh" "$platform" "$out_dir" >/dev/null
        require_dir "$out_dir"
        tar -czf "$OUT_DIR/CommonwealthOnline.Host-$platform-$VERSION.tar.gz" -C "$out_dir" .
        log "wrote CommonwealthOnline.Host-$platform-$VERSION.tar.gz"
    done
}

main() {
    [ -n "$VERSION" ] || die "CO_VERSION is required, e.g. CO_VERSION=0.0.0.4 $0"
    case "$VERSION" in
    *[!0-9.]*) die "CO_VERSION must contain only digits and dots" ;;
    esac

    require_tool xmake
    require_tool cmake
    require_tool ninja
    require_tool protoc
    require_tool clang-cl
    require_tool lld-link
    require_tool python3
    require_dir "$CO_BETA_DIR/ThirdParty/GameNetworkingSockets-src"
    require_dir "$PLUGIN_DIR/lib/commonlibf4"
    require_dir "$XWIN_ROOT/crt"
    require_dir "$XWIN_ROOT/sdk"
    if [ "$BUILD_UI" = "1" ]; then
        require_dir "$XPACKAGE_WIN_PACKAGES"
    fi

    rm -rf "$OUT_DIR"
    mkdir -p "$OUT_DIR"

    build_windows_bridge
    build_plugin_dlls
    if [ "$BUILD_UI" = "1" ]; then
        build_ui_package
    else
        log "CO_BUILD_UI=0; skipping the UI archive"
    fi

    local data_dir="$WORK_DIR/Data"
    stage_runtime_data "$data_dir"
    validate_payload "$data_dir"
    assemble_runtime_archives "$data_dir"
    if [ "$BUILD_UI" = "1" ]; then
        assemble_ui_archive
    fi
    if [ "$BUILD_HOST_PACKAGES" = "1" ]; then
        assemble_host_archives
    else
        log "CO_BUILD_HOST_PACKAGES=0; skipping the host archives"
    fi

    printf '\nrelease assets in %s:\n' "$OUT_DIR"
    find "$OUT_DIR" -maxdepth 1 -type f -printf '  %f\n' | sort
}

main "$@"
