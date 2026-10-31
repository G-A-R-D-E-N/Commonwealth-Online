#!/usr/bin/env bash
# Cross-build the Windows x64 GNS bridge from Linux using clang-cl + xwin.
#
# This replaces the Windows-runner/vcpkg path for CI. It targets the Windows
# MSVC ABI (x86_64-pc-windows-msvc) with the static release CRT, so the
# packaged bridge does not require a Visual C++ redistributable. Wine is not
# used to compile or link; it is only used, when available, to execute the
# generated PE test binaries.
#
# usage: build-gns-bridge-windows-cross.sh <deps-dir> <build-dir>
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <deps-dir> <build-dir>" >&2
    exit 2
fi

DEPS_DIR="$1"
BUILD_DIR="$2"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

GNS_SHA="f4525e39ee10f6b45181fd92d01fee75f7d71756"
PROTOBUF_TAG="v21.12"
XWIN_ROOT="${XWIN_ROOT:-/opt/xwin}"
TOOLCHAIN="$ROOT_DIR/server/native_transport/cmake/xwin-clang-cl-static.cmake"
PB_STATIC_RUNTIME=ON
CI_CACHE_ROOT="${CO_CI_CACHE_DIR:-${HOME:-/nonexistent}/.cache/co-ci}"

GNS_DIR="$DEPS_DIR/GameNetworkingSockets-src"
PB_SRC="$DEPS_DIR/protobuf-src"
PB_BUILD="$DEPS_DIR/protobuf-build"
PB_INSTALL="$DEPS_DIR/protobuf-install"

echo "==> verifying cross toolchain"
missing=0
for tool in git cmake ninja clang-cl lld-link llvm-lib llvm-rc llvm-mt llvm-readobj protoc; do
    command -v "$tool" >/dev/null 2>&1 || { echo "::error::missing cross-build tool: $tool"; missing=1; }
done
for path in \
    "$XWIN_ROOT/crt/lib/x86_64/libcmt.lib" \
    "$XWIN_ROOT/crt/lib/x86_64/libcpmt.lib" \
    "$XWIN_ROOT/crt/lib/x86_64/libvcruntime.lib" \
    "$XWIN_ROOT/sdk/lib/ucrt/x86_64/libucrt.lib"; do
    test -f "$path" || { echo "::error::missing static CRT component: $path"; missing=1; }
done
test -f "$TOOLCHAIN" || { echo "::error::missing toolchain file: $TOOLCHAIN"; missing=1; }
test "$missing" -eq 0

mkdir -p "$DEPS_DIR"

echo "==> fetching pinned GameNetworkingSockets $GNS_SHA"
if [[ ! -d "$GNS_DIR/.git" ]]; then
    rm -rf "$GNS_DIR"
    git init -q "$GNS_DIR"
    git -C "$GNS_DIR" remote add origin https://github.com/ValveSoftware/GameNetworkingSockets.git
fi
git -C "$GNS_DIR" fetch -q --depth 1 origin "$GNS_SHA"
git -C "$GNS_DIR" checkout -q --detach FETCH_HEAD
actual_gns="$(git -C "$GNS_DIR" rev-parse HEAD)"
if [[ "$actual_gns" != "$GNS_SHA" ]]; then
    echo "::error::pinned GNS revision mismatch: $actual_gns" >&2
    exit 1
fi

echo "==> building Windows protobuf runtime ($PROTOBUF_TAG, static CRT)"
build_protobuf() {
    local prefix="$1"
    rm -rf "$PB_SRC" "$PB_BUILD" "$prefix"
    git clone -q --depth 1 --branch "$PROTOBUF_TAG" \
        https://github.com/protocolbuffers/protobuf.git "$PB_SRC"
    actual_pb="$(git -C "$PB_SRC" describe --tags --exact-match)"
    if [[ "$actual_pb" != "$PROTOBUF_TAG" ]]; then
        echo "::error::pinned protobuf tag mismatch: $actual_pb" >&2
        exit 1
    fi

    # protobuf_MSVC_STATIC_RUNTIME=ON is required here and deliberately differs
    # from the CO-BETA /MD proof: CO-SERVER links the static CRT, and mixing a
    # /MD protobuf into a /MT bridge produces duplicate-runtime link errors.
    cmake -S "$PB_SRC" -B "$PB_BUILD" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DCMAKE_BUILD_TYPE=Release \
        -Dprotobuf_BUILD_TESTS=OFF \
        -Dprotobuf_BUILD_PROTOC_BINARIES=OFF \
        -Dprotobuf_BUILD_SHARED_LIBS=OFF \
        -Dprotobuf_MSVC_STATIC_RUNTIME="$PB_STATIC_RUNTIME" \
        -Dprotobuf_WITH_ZLIB=OFF
    cmake --build "$PB_BUILD" --parallel "$(nproc)"
    cmake --install "$PB_BUILD" --prefix "$prefix"
}

cache_ok=0
if mkdir -p "$CI_CACHE_ROOT" 2>/dev/null && [[ -w "$CI_CACHE_ROOT" ]] && command -v flock >/dev/null 2>&1; then
    cache_ok=1
fi
if [[ "$cache_ok" -eq 1 ]]; then
    tc_hash="$({ sha256sum "$TOOLCHAIN"; declare -f build_protobuf; clang-cl --version 2>/dev/null || true; } | sha256sum | cut -c1-16)"
    PB_INSTALL="$CI_CACHE_ROOT/protobuf-$PROTOBUF_TAG-$tc_hash-static-runtime-$PB_STATIC_RUNTIME"
    (
        flock 9
        if [[ ! -f "$PB_INSTALL/lib/libprotobuf.lib" ]]; then
            rm -rf "$PB_INSTALL".tmp-*
            build_protobuf "$PB_INSTALL.tmp-$$"
            rm -rf "$PB_INSTALL"
            mv "$PB_INSTALL.tmp-$$" "$PB_INSTALL"
        fi
    ) 9>"$PB_INSTALL.lock"
elif [[ ! -f "$PB_INSTALL/lib/libprotobuf.lib" ]]; then
    build_protobuf "$PB_INSTALL"
fi
test -f "$PB_INSTALL/lib/libprotobuf.lib"

echo "==> configuring bridge cross-build"
emulator_args=()
if command -v wine >/dev/null 2>&1; then
    emulator_args+=("-DCMAKE_CROSSCOMPILING_EMULATOR=$(command -v wine)")
fi

rm -rf "$BUILD_DIR"
cmake -S "$ROOT_DIR/server/native_transport" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_BUILD_TYPE=Release \
    -DGNS_SOURCE_DIR="$GNS_DIR" \
    -DCO_GNS_BUILD_TESTS="${CO_GNS_BUILD_TESTS:-ON}" \
    -DCO_GNS_ENABLE_TEST_NETWORK="${CO_GNS_ENABLE_TEST_NETWORK:-ON}" \
    "${emulator_args[@]}" \
    -DProtobuf_INCLUDE_DIR="$PB_INSTALL/include" \
    -DProtobuf_LIBRARY="$PB_INSTALL/lib/libprotobuf.lib" \
    -DProtobuf_LIBRARY_RELEASE="$PB_INSTALL/lib/libprotobuf.lib" \
    -DProtobuf_LITE_LIBRARY="$PB_INSTALL/lib/libprotobuf-lite.lib" \
    -DProtobuf_PROTOC_EXECUTABLE="$(command -v protoc)"

echo "==> building bridge"
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

BRIDGE="$BUILD_DIR/commonwealth_online_gns_bridge.dll"
test -s "$BRIDGE" || { echo "::error::bridge DLL was not produced: $BRIDGE" >&2; exit 1; }
echo "==> built $BRIDGE"
