#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <linux-x64|win-x64> <output-directory>" >&2
    exit 2
fi

RID="$1"
OUT_DIR="$(mkdir -p "$2" && realpath "$2")"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CO_SERVER_DIR="$(cd "$CPP_DIR/.." && pwd)"
RUN_KEY="${GITHUB_RUN_ID:-local}-${GITHUB_RUN_ATTEMPT:-1}-$$"

bash "$SCRIPT_DIR/verify-native-release-path.sh"

case "$RID" in
    linux-x64)
        DEPS_DIR="${CO_GNS_DEPS_DIR:-${RUNNER_TEMP:-/tmp}/co-native-package-gns-deps-$RUN_KEY}"
        BUILD_DIR="${CO_GNS_BUILD_DIR:-${RUNNER_TEMP:-/tmp}/co-native-package-gns-build-$RUN_KEY}"
        export CO_GNS_DEPS_DIR="$DEPS_DIR"
        export CO_GNS_BUILD_DIR="$BUILD_DIR"

        # The shipped bridge carries no test instrumentation: global fake packet
        # loss and reorder must not be reachable in a released server.
        export CO_GNS_BUILD_TESTS=OFF
        export CO_GNS_ENABLE_TEST_NETWORK=OFF
        bash "$CO_SERVER_DIR/server/scripts/build-gns-bridge.sh"
        bridge="$BUILD_DIR/libcommonwealth_online_gns_bridge.so"
        test -s "$bridge" || { echo "ERROR: Linux GNS bridge was not produced" >&2; exit 1; }

        build_dir="$CPP_DIR/build-package"
        rm -rf "$build_dir" "$CPP_DIR/.xmake"
        (
            cd "$CPP_DIR"
            CO_GNS_PACKAGE_BRIDGE_DIR="$BUILD_DIR" xmake config --file=xmake-package.lua \
                --plat=linux --arch=x86_64 --mode=release --builddir=build-package --yes
            for target in \
                CommonwealthOnlineServerCpp \
                CommonwealthOnlineServerLiveGnsSmoke \
                CommonwealthOnlineServerLiveIdentityGnsSmoke \
                CommonwealthOnlineServerLivePersistenceGnsSmoke \
                CommonwealthOnlineServerLiveAdminGnsSmoke \
                CommonwealthOnlineServerLiveProfileBanGnsSmoke; do
                CO_GNS_PACKAGE_BRIDGE_DIR="$BUILD_DIR" xmake build --file=xmake-package.lua \
                    --jobs=2 -vD "$target"
            done
        )

        server="$(find "$build_dir" -type f -name 'CommonwealthOnlineServerCpp' -print -quit)"
        smoke="$(find "$build_dir" -type f -name 'CommonwealthOnlineServerLiveGnsSmoke' -print -quit)"
        identity_smoke="$(find "$build_dir" -type f -name 'CommonwealthOnlineServerLiveIdentityGnsSmoke' -print -quit)"
        persistence_smoke="$(find "$build_dir" -type f -name 'CommonwealthOnlineServerLivePersistenceGnsSmoke' -print -quit)"
        admin_smoke="$(find "$build_dir" -type f -name 'CommonwealthOnlineServerLiveAdminGnsSmoke' -print -quit)"
        profile_ban_smoke="$(find "$build_dir" -type f -name 'CommonwealthOnlineServerLiveProfileBanGnsSmoke' -print -quit)"
        for required in "$server" "$smoke" "$identity_smoke" "$persistence_smoke" "$admin_smoke" "$profile_ban_smoke"; do
            test -s "$required" || { echo "ERROR: native Linux release binary was not produced: $required" >&2; exit 1; }
        done

        bash "$SCRIPT_DIR/package-native-release.sh" linux-x64 "$bridge" "$server" "$OUT_DIR"
        printf '%s\n' "CO_NATIVE_PACKAGE_DIR=$OUT_DIR"
        printf '%s\n' "CO_NATIVE_SMOKE=$smoke"
        printf '%s\n' "CO_NATIVE_IDENTITY_SMOKE=$identity_smoke"
        printf '%s\n' "CO_NATIVE_PERSISTENCE_SMOKE=$persistence_smoke"
        printf '%s\n' "CO_NATIVE_ADMIN_SMOKE=$admin_smoke"
        printf '%s\n' "CO_NATIVE_PROFILE_BAN_SMOKE=$profile_ban_smoke"
        printf '%s\n' "CO_NATIVE_BRIDGE_DIR=$BUILD_DIR"
        ;;

    win-x64)
        DEPS_DIR="${CO_WIN_DEPS_DIR:-${RUNNER_TEMP:-/tmp}/co-native-package-win-deps-$RUN_KEY}"
        BUILD_DIR="${CO_WIN_BUILD_DIR:-${RUNNER_TEMP:-/tmp}/co-native-package-win-build-$RUN_KEY}"
        export CO_WIN_DEPS_DIR="$DEPS_DIR"
        export CO_WIN_BUILD_DIR="$BUILD_DIR"

        # The shipped bridge carries no test instrumentation: global fake packet
        # loss and reorder must not be reachable in a released server.
        export CO_GNS_BUILD_TESTS=OFF
        export CO_GNS_ENABLE_TEST_NETWORK=OFF
        bash "$CO_SERVER_DIR/server/scripts/build-gns-bridge-windows-cross.sh" "$DEPS_DIR" "$BUILD_DIR"
        bridge="$BUILD_DIR/commonwealth_online_gns_bridge.dll"
        test -s "$bridge" || { echo "ERROR: Windows GNS bridge was not produced" >&2; exit 1; }

        build_dir="$CPP_DIR/build-xwin-package"
        rm -rf "$build_dir" "$CPP_DIR/.xmake"
        (
            cd "$CPP_DIR"
            CO_GNS_BRIDGE_DIR="$BUILD_DIR" xmake config --file=xmake-xwin-package.lua \
                --plat=windows --arch=x64 --mode=release --builddir=build-xwin-package --yes
            for target in \
                CommonwealthOnlineServerCpp \
                CommonwealthOnlineServerLiveGnsSmoke \
                CommonwealthOnlineServerLiveIdentityGnsSmoke \
                CommonwealthOnlineServerLivePersistenceGnsSmoke \
                CommonwealthOnlineServerLiveAdminGnsSmoke \
                CommonwealthOnlineServerLiveProfileBanGnsSmoke; do
                CO_GNS_BRIDGE_DIR="$BUILD_DIR" xmake build --file=xmake-xwin-package.lua \
                    --jobs=2 -vD "$target"
            done
        )

        server="$(find "$build_dir" -type f -iname 'CommonwealthOnlineServerCpp.exe' -print -quit)"
        test -s "$server" || { echo "ERROR: Windows native server was not produced" >&2; exit 1; }

        # The Windows runtime smoke runs on a machine with no toolchain, so the
        # gameplay clients are cross-built here and shipped beside the package.
        smoke_dir="$OUT_DIR-smoke"
        rm -rf "$smoke_dir"
        mkdir -p "$smoke_dir"
        for smoke in \
            CommonwealthOnlineServerLiveGnsSmoke \
            CommonwealthOnlineServerLiveIdentityGnsSmoke \
            CommonwealthOnlineServerLivePersistenceGnsSmoke \
            CommonwealthOnlineServerLiveAdminGnsSmoke \
            CommonwealthOnlineServerLiveProfileBanGnsSmoke; do
            found="$(find "$build_dir" -type f -iname "$smoke.exe" -print -quit)"
            test -s "$found" || { echo "ERROR: Windows smoke client was not produced: $smoke" >&2; exit 1; }
            cp "$found" "$smoke_dir/"
        done
        cp "$bridge" "$smoke_dir/"

        bash "$SCRIPT_DIR/package-native-release.sh" win-x64 "$bridge" "$server" "$OUT_DIR"
        printf '%s\n' "CO_NATIVE_PACKAGE_DIR=$OUT_DIR"
        printf '%s\n' "CO_NATIVE_WIN_SMOKE_DIR=$smoke_dir"
        ;;

    *)
        echo "ERROR: unsupported runtime identifier: $RID" >&2
        exit 2
        ;;
esac
