#!/usr/bin/env bash
# Builds the CommonwealthOnline release assets for one version tag.
#
# This reproduces the layout of the published 0.0.0.x releases, which is NOT
# CO-BETA/stage-mod.bat's: the runtime archives root at Data/ and carry the
# runtime plugin DLL, the GNS bridge, the packaged client manifest,
# the repo content, PrismaUI runtime, and Commonwealth Online UI views.
#
#     Data/CommonwealthOnline.esp
#     Data/F4SE/Plugins/commonwealth_online_gns_bridge.dll
#     Data/F4SE/Plugins/CommonwealthOnlineClient.json
#     Data/F4SE/Plugins/Addictol.dll  AddictolCrashLogger.dll  Addictol*.toml  Addictol_*.ini  AddictolCrashLogger.toml
#     Data/Scripts/Addictol.pex  Data/Scripts/XCELL.pex
#     Data/Scripts/...  Data/Materials/...  Data/Meshes/...  Data/Textures/...
#     Data/CommonwealthOnline/Licenses/Addictol-LICENSE.txt  Addictol-EXCEPTIONS.txt  AddictolCrashLogger-*.txt  Addictol.md
#   CommonwealthOnline.Host-linux-x64-<version>.tar.gz  ->  package contents at the archive root
#   CommonwealthOnline.Host-win-x64-<version>.tar.gz
#
# Inputs (environment):
#   CO_VERSION              required, e.g. 0.0.0.4; used only in asset filenames
#   CO_OUT                  output directory (default: <repo>/release-out)
#   CO_BUILD_HOST_PACKAGES 1 to also build the host archives (default 1)
#   CO_PRISMA_RUNTIME      required: pinned PrismaUI runtime ZIP
#   CO_WINDOWS_DLLS         required: directory with the Windows DLLs (scripts/windows-compile-ci.py release,
#                           scripts/windows-addictol-ci.py)
#   XWIN_ROOT               xwin CRT/SDK tree (default /opt/xwin)
#
# Requires: Linux host, clang-cl/lld-link/llvm-*/cmake/ninja/protoc/xmake on PATH,
# python3. dotnet 8 is additionally required when CO_BUILD_HOST_PACKAGES=1.
# Prints each produced asset path on stdout.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CO_BETA_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
REPO_DIR="$(cd -- "$CO_BETA_DIR/.." && pwd)"

VERSION="${CO_VERSION:-}"
OUT_DIR="${CO_OUT:-$REPO_DIR/release-out}"
BUILD_HOST_PACKAGES="${CO_BUILD_HOST_PACKAGES:-1}"
XWIN_ROOT="${XWIN_ROOT:-/opt/xwin}"
WINDOWS_DLLS="${CO_WINDOWS_DLLS:-}"

XPACKAGE_DIR="$REPO_DIR/CommonwealthOnlineUI"
BRIDGE_DLL="$WINDOWS_DLLS/commonwealth_online_gns_bridge.dll"
PLUGIN_DLL="$WINDOWS_DLLS/CommonwealthOnline.dll"

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

# Stages the repo content, runtime binaries and UI, then emits and verifies
# CommonwealthOnlineClient.json.
stage_runtime_data() {
    local data_dir="$1"
    log "staging runtime Data tree at $data_dir"

    local content_stage
    content_stage="$(bash "$CO_BETA_DIR/ci/stage-content.sh")"
    mkdir -p "$data_dir"
    cp -R "$content_stage"/. "$data_dir"/
    rm -rf "$content_stage"

    mkdir -p "$data_dir/F4SE/Plugins"
    cp "$WINDOWS_DLLS/CommonwealthOnline.dll" "$data_dir/F4SE/Plugins/CommonwealthOnline.dll"
    cp "$BRIDGE_DLL" "$data_dir/F4SE/Plugins/commonwealth_online_gns_bridge.dll"
    stage_ui_data "$data_dir"

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
        --exclude-name commonwealth_online_gns_bridge.dll \
        --exclude-name CommonwealthOnlineUI.provenance.json \
        --emit-client-json "$manifest" | tr -d '\r' | grep -E '^[0-9a-f]{64}$' | tail -n 1)"
    [ -n "$fingerprint" ] || die "content-manifest tool returned no fingerprint"

    local verified
    verified="$(cd "$REPO_DIR/co-core" && xmake run CommonwealthOnlineCoreContentManifestTool \
        --root "$data_dir" \
        --exclude-name CommonwealthOnline.dll \
        --exclude-name commonwealth_online_gns_bridge.dll \
        --exclude-name CommonwealthOnlineUI.provenance.json | tr -d '\r' | grep -E '^[0-9a-f]{64}$' | tail -n 1)"
    [ "$fingerprint" = "$verified" ] || die "staged content fingerprint changed after the client manifest was emitted"

    require_file "$manifest"
    log "content manifest fingerprint $fingerprint"
}

stage_addictol_data() {
    local data_dir="$1"
    local plugins="$data_dir/F4SE/Plugins"
    local addictol="$CO_BETA_DIR/ThirdParty/Addictol"
    local crash_logger="$CO_BETA_DIR/ThirdParty/AddictolCrashLogger"
    log "staging Addictol and AddictolCrashLogger"

    mkdir -p "$plugins" "$data_dir/Scripts"
    cp "$WINDOWS_DLLS/Addictol.dll" "$plugins/Addictol.dll"
    cp "$WINDOWS_DLLS/AddictolCrashLogger.dll" "$plugins/AddictolCrashLogger.dll"

    local file
    for file in "$addictol"/data/F4SE/Plugins/Addictol*.toml "$addictol"/data/F4SE/Plugins/Addictol_*.ini \
        "$crash_logger"/data/F4SE/Plugins/AddictolCrashLogger*.toml; do
        require_file "$file"
        cp "$file" "$plugins/"
    done
    for file in "$addictol/data/Scripts/Addictol.pex" "$addictol/data/Scripts/XCELL.pex"; do
        require_file "$file"
        cp "$file" "$data_dir/Scripts/"
    done
}

stage_addictol_licenses() {
    local data_dir="$1"
    local out="$data_dir/CommonwealthOnline/Licenses"
    local addictol="$CO_BETA_DIR/ThirdParty/Addictol"
    local crash_logger="$CO_BETA_DIR/ThirdParty/AddictolCrashLogger"
    rm -rf "$out"
    mkdir -p "$out"

    require_file "$addictol/LICENSE"
    require_file "$addictol/EXCEPTIONS"
    require_file "$crash_logger/LICENSE"
    require_file "$crash_logger/EXCEPTIONS"
    require_file "$CO_BETA_DIR/Licenses/Addictol.md"
    cp "$addictol/LICENSE" "$out/Addictol-LICENSE.txt"
    cp "$addictol/EXCEPTIONS" "$out/Addictol-EXCEPTIONS.txt"
    cp "$crash_logger/LICENSE" "$out/AddictolCrashLogger-LICENSE.txt"
    cp "$crash_logger/EXCEPTIONS" "$out/AddictolCrashLogger-EXCEPTIONS.txt"
    cp "$CO_BETA_DIR/Licenses/Addictol.md" "$out/Addictol.md"
}

# Mirrors CO-BETA/stage-mod.bat's :ValidateReleasePayload for the shipped layout.
validate_payload() {
    local data_dir="$1"
    log "validating the staged payload"
    require_file "$data_dir/CommonwealthOnline.esp"
    require_file "$data_dir/F4SE/Plugins/CommonwealthOnline.dll"
    require_file "$data_dir/F4SE/Plugins/commonwealth_online_gns_bridge.dll"
    require_file "$data_dir/F4SE/Plugins/CommonwealthOnlineClient.json"
    require_file "$data_dir/F4SE/Plugins/Addictol.dll"
    require_file "$data_dir/F4SE/Plugins/AddictolCrashLogger.dll"
    require_file "$data_dir/F4SE/Plugins/Addictol.toml"
    require_file "$data_dir/F4SE/Plugins/AddictolCrashLogger.toml"
    require_file "$data_dir/CommonwealthOnline/Licenses/Addictol-LICENSE.txt"
    require_file "$data_dir/CommonwealthOnline/Licenses/Addictol-EXCEPTIONS.txt"
    require_file "$data_dir/CommonwealthOnline/Licenses/AddictolCrashLogger-LICENSE.txt"
    require_file "$data_dir/CommonwealthOnline/Licenses/AddictolCrashLogger-EXCEPTIONS.txt"
    require_file "$data_dir/CommonwealthOnline/Licenses/Addictol.md"
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

    local ui_dll="$data_dir/F4SE/Plugins/CommonwealthOnlineUI.dll"
    if [ -e "$ui_dll" ]; then
        die "the runtime archives must not contain CommonwealthOnlineUI.dll; the UI code is inside CommonwealthOnline.dll"
    fi

    if [ -e "$data_dir/F4SE/Plugins/CommonwealthOnlineProbe.dll" ]; then
        die "the release payload must not contain CommonwealthOnlineProbe.dll; the probe is a non-shipping tooling target"
    fi
}

assemble_runtime_archives() {
    local data_dir="$1"
    local out="$WORK_DIR/root-unified"
    rm -rf "$out"
    mkdir -p "$out"
    cp -R "$data_dir" "$out/Data"
    zip_tree "$out" "$OUT_DIR/CommonwealthOnline-$VERSION.zip"
    log "wrote CommonwealthOnline-$VERSION.zip"
}

# Mirrors Write-UiProvenance in CO-BETA/tools/stage-client-manifest.ps1: same field
# names and the same views digest (sorted "relpath<TAB>sha256" lines, hashed as one text).
write_ui_provenance() {
    local data_dir="$1"
    python3 - "$REPO_DIR" "$data_dir" "$2" <<'PY'
import hashlib
import json
import os
import subprocess
import sys

repo, data, plugin_dll = sys.argv[1], sys.argv[2], sys.argv[3]


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
    "uiDllSha256": sha256(plugin_dll),
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
prepare_prisma_runtime() {
    local out="$WORK_DIR/prisma-runtime"
    [ -n "${CO_PRISMA_RUNTIME:-}" ] || die "CO_PRISMA_RUNTIME is required for runtime archives"
    require_file "$CO_PRISMA_RUNTIME"
    python3 - "$CO_PRISMA_RUNTIME" "$out" <<'PY_RUNTIME' || die "invalid PrismaUI runtime archive"
import hashlib
import sys
import zipfile

path, out = sys.argv[1:]
with open(path, "rb") as source:
    digest = hashlib.sha256(source.read()).hexdigest()
if digest != "4ca83f40384a76c02dcc94e16cde68cb615c121be19a390e15a096ee13ca7189":
    raise SystemExit("PrismaUI runtime archive hash mismatch")
with zipfile.ZipFile(path) as archive:
    for item in archive.infolist():
        item.filename = item.filename.replace("Data/F4SE/plugins/", "Data/F4SE/Plugins/", 1)
        archive.extract(item, out)
PY_RUNTIME
}

stage_ui_data() {
    local data="$1"
    mkdir -p "$data"
    cp -R "$WORK_DIR/prisma-runtime/Data"/. "$data"/
    mkdir -p "$data/F4SE/Plugins" "$data/PrismaUI_F4/views" "$data/PrismaUI_F4/misc"

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

    require_dir "$data/PrismaUI_F4/views"
    write_ui_provenance "$data" "$PLUGIN_DLL"
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
    require_dir "$XWIN_ROOT/crt"
    require_dir "$XWIN_ROOT/sdk"
    [ -n "$WINDOWS_DLLS" ] || die "CO_WINDOWS_DLLS is required: the directory with the Windows DLLs"
    local dll
    for dll in CommonwealthOnline.dll commonwealth_online_gns_bridge.dll Addictol.dll AddictolCrashLogger.dll; do
        require_file "$WINDOWS_DLLS/$dll"
    done

    prepare_prisma_runtime
    rm -rf "$OUT_DIR"
    mkdir -p "$OUT_DIR"

    local data_dir="$WORK_DIR/Data"
    stage_runtime_data "$data_dir"
    stage_addictol_data "$data_dir"
    stage_addictol_licenses "$data_dir"
    validate_payload "$data_dir"
    assemble_runtime_archives "$data_dir"
    if [ "$BUILD_HOST_PACKAGES" = "1" ]; then
        assemble_host_archives
    else
        log "CO_BUILD_HOST_PACKAGES=0; skipping the host archives"
    fi

    printf '\nrelease assets in %s:\n' "$OUT_DIR"
    find "$OUT_DIR" -maxdepth 1 -type f -printf '  %f\n' | sort
}

main "$@"
