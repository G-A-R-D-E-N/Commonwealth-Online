#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
GNS_SHA="f4525e39ee10f6b45181fd92d01fee75f7d71756"
DEPS_DIR="${CO_GNS_DEPS_DIR:-$ROOT_DIR/_deps}"
GNS_DIR="$DEPS_DIR/GameNetworkingSockets-src"
BUILD_DIR="${CO_GNS_BUILD_DIR:-$ROOT_DIR/build/gns-server-bridge-linux}"
BRIDGE="$BUILD_DIR/libcommonwealth_online_gns_bridge.so"
CI_CACHE_ROOT="${CO_CI_CACHE_DIR:-${HOME:-/nonexistent}/.cache/co-ci}"
GNS_BINARY_DIR="$BUILD_DIR/gns"
GNS_LOCK=""

for tool in git cmake protoc; do
    command -v "$tool" >/dev/null 2>&1 || { echo "ERROR: $tool was not found on PATH." >&2; exit 1; }
done

mkdir -p "$DEPS_DIR"
if [[ ! -d "$GNS_DIR/.git" ]]; then
    rm -rf "$GNS_DIR"
    git init "$GNS_DIR" >/dev/null
    git -C "$GNS_DIR" remote add origin https://github.com/ValveSoftware/GameNetworkingSockets.git
fi

git -C "$GNS_DIR" fetch --depth 1 origin "$GNS_SHA"
git -C "$GNS_DIR" checkout --detach --force FETCH_HEAD >/dev/null
actual="$(git -C "$GNS_DIR" rev-parse HEAD)"
if [[ "$actual" != "$GNS_SHA" ]]; then
    echo "ERROR: GameNetworkingSockets is at $actual but $GNS_SHA is required." >&2
    exit 1
fi

if mkdir -p "$CI_CACHE_ROOT" 2>/dev/null && [[ -w "$CI_CACHE_ROOT" ]] && command -v flock >/dev/null 2>&1; then
    compiler_id="$({ "${CC:-cc}" --version || true; "${CXX:-c++}" --version || true; protoc --version; cmake --version; } 2>/dev/null | sha256sum | cut -c1-16)"
    cmake_hash="$(sha256sum "$ROOT_DIR/server/native_transport/CMakeLists.txt" | cut -c1-16)"
    gns_key="$(printf '%s\n' "$GNS_SHA" "$GNS_DIR" "$compiler_id" Release "$cmake_hash" | sha256sum | cut -c1-16)"
    GNS_BINARY_DIR="$CI_CACHE_ROOT/gns-$gns_key"
    GNS_LOCK="$CI_CACHE_ROOT/gns-$gns_key.lock"
fi

configure_and_build() {
    cmake -S "$ROOT_DIR/server/native_transport" -B "$BUILD_DIR" \
        -DGNS_SOURCE_DIR="$GNS_DIR" \
        -DCO_GNS_BINARY_DIR="$GNS_BINARY_DIR" \
        -DCO_GNS_BUILD_TESTS="${CO_GNS_BUILD_TESTS:-ON}" \
        -DCO_GNS_ENABLE_TEST_NETWORK="${CO_GNS_ENABLE_TEST_NETWORK:-ON}" \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" --parallel 2
}
if [[ -n "$GNS_LOCK" ]]; then
    (
        flock 9
        if [[ -e "$GNS_BINARY_DIR.building" ]]; then rm -rf "$GNS_BINARY_DIR"; fi
        touch "$GNS_BINARY_DIR.building"
        configure_and_build
        rm -f "$GNS_BINARY_DIR.building"
    ) 9>"$GNS_LOCK"
else
    configure_and_build
fi
ctest --test-dir "$BUILD_DIR" --output-on-failure

if [[ ! -s "$BRIDGE" ]]; then
    echo "ERROR: native GNS bridge was not produced at $BRIDGE" >&2
    exit 1
fi

if ldd "$BRIDGE" | grep -q 'GameNetworkingSockets'; then
    echo "ERROR: packaged bridge still depends on a shared GameNetworkingSockets library." >&2
    exit 1
fi

printf '%s\n' "$BRIDGE"
