#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 3 || $# -gt 4 ]]; then
    echo "usage: $0 <linux-x64|win-x64> <native-bridge-path> <native-server-path> [output-directory]" >&2
    exit 2
fi

RID="$1"
BRIDGE_SOURCE="$(realpath "$2")"
SERVER_SOURCE="$(realpath "$3")"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_CPP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd "$SERVER_CPP_DIR/.." && pwd)"
OUT_DIR="${4:-$ROOT_DIR/out/native-$RID}"

case "$RID" in
    linux-x64)
        SERVER_NAME="CommonwealthOnline.Server"
        BRIDGE_NAME="libcommonwealth_online_gns_bridge.so"
        ;;
    win-x64)
        SERVER_NAME="CommonwealthOnline.Server.exe"
        BRIDGE_NAME="commonwealth_online_gns_bridge.dll"
        ;;
    *)
        echo "ERROR: unsupported runtime identifier: $RID" >&2
        exit 2
        ;;
esac

for input in "$BRIDGE_SOURCE" "$SERVER_SOURCE"; do
    if [[ ! -s "$input" ]]; then
        echo "ERROR: required native input not found: $input" >&2
        exit 1
    fi
done
command -v dotnet >/dev/null 2>&1 || { echo "ERROR: dotnet was not found on PATH." >&2; exit 1; }

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT
HOST_OUT="$WORK_DIR/host"

PUBLISH_ARGS=(
    -c Release
    -r "$RID"
    --self-contained true
    -p:PublishSingleFile=true
    -p:IncludeNativeLibrariesForSelfExtract=true
    -p:DebugType=none
    --nologo
)

dotnet publish "$ROOT_DIR/host/CommonwealthOnline.Host.csproj" "${PUBLISH_ARGS[@]}" -o "$HOST_OUT"

KEPT_ZONES=""
if [[ -f "$OUT_DIR/workshop-zones.json" ]]; then
    KEPT_ZONES="$(mktemp)"
    cp "$OUT_DIR/workshop-zones.json" "$KEPT_ZONES"
fi
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"
cp -a "$HOST_OUT/." "$OUT_DIR/"
cp "$SERVER_SOURCE" "$OUT_DIR/$SERVER_NAME"
cp "$BRIDGE_SOURCE" "$OUT_DIR/$BRIDGE_NAME"
cp "$SERVER_CPP_DIR/commonwealth-server-native.json" "$OUT_DIR/commonwealth-server.json"
cp "$SERVER_CPP_DIR/NATIVE_RUNTIME.txt" "$OUT_DIR/NATIVE_RUNTIME.txt"
if [[ -n "$KEPT_ZONES" ]]; then
    mv "$KEPT_ZONES" "$OUT_DIR/workshop-zones.json"
else
    cp "$SERVER_CPP_DIR/workshop-zones.sample.json" "$OUT_DIR/workshop-zones.json"
fi
printf 'native-cpp\n' > "$OUT_DIR/CommonwealthOnline.Server.native"

if [[ "$RID" == "linux-x64" ]]; then
    cp "$SERVER_CPP_DIR/commonwealth-online-native.service.example" "$OUT_DIR/commonwealth-online.service.example"
    chmod +x "$OUT_DIR/$SERVER_NAME"
    ldd "$OUT_DIR/$BRIDGE_NAME" > "$OUT_DIR/native-bridge-dependencies.txt"
    ldd "$OUT_DIR/$SERVER_NAME" > "$OUT_DIR/native-server-dependencies.txt"
fi

bash "$SCRIPT_DIR/verify-native-package.sh" "$RID" "$OUT_DIR"
printf '%s\n' "$OUT_DIR"
