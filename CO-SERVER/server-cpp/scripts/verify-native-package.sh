#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <linux-x64|win-x64> <package-directory>" >&2
    exit 2
fi

RID="$1"
PACKAGE_DIR="$(realpath "$2")"

case "$RID" in
    linux-x64)
        HOST_NAME="CommonwealthOnline.Host"
        SERVER_NAME="CommonwealthOnline.Server"
        BRIDGE_NAME="libcommonwealth_online_gns_bridge.so"
        ;;
    win-x64)
        HOST_NAME="CommonwealthOnline.Host.exe"
        SERVER_NAME="CommonwealthOnline.Server.exe"
        BRIDGE_NAME="commonwealth_online_gns_bridge.dll"
        ;;
    *)
        echo "ERROR: unsupported runtime identifier: $RID" >&2
        exit 2
        ;;
esac

for required in "$HOST_NAME" "$SERVER_NAME" "$BRIDGE_NAME" commonwealth-server.json workshop-zones.json CommonwealthOnline.Server.native NATIVE_RUNTIME.txt; do
    test -s "$PACKAGE_DIR/$required" || {
        echo "ERROR: native package is missing $required" >&2
        exit 1
    }
done

marker="$(tr -d '\r\n' < "$PACKAGE_DIR/CommonwealthOnline.Server.native")"
[[ "$marker" == "native-cpp" ]] || {
    echo "ERROR: invalid native package marker: '$marker'" >&2
    exit 1
}

for managed in CommonwealthOnline.Server.dll CommonwealthOnline.Server.deps.json CommonwealthOnline.Server.runtimeconfig.json; do
    if [[ -e "$PACKAGE_DIR/$managed" ]]; then
        echo "ERROR: native package contains managed server artifact $managed" >&2
        exit 1
    fi
done

if grep -Eq '"(enable_gns_transport|gns_bridge_path|log_verbosity)"' "$PACKAGE_DIR/commonwealth-server.json"; then
    echo "ERROR: native package config contains managed-only server fields" >&2
    exit 1
fi

if [[ "$RID" == "linux-x64" ]]; then
    for tool in readelf ldd; do
        command -v "$tool" >/dev/null || { echo "ERROR: $tool was not found on PATH." >&2; exit 1; }
    done
    grep -q 'OpenSSL libcrypto' "$PACKAGE_DIR/NATIVE_RUNTIME.txt" || {
        echo "ERROR: native runtime note does not document the Linux libcrypto dependency" >&2
        exit 1
    }
    test -x "$PACKAGE_DIR/$HOST_NAME" || {
        echo "ERROR: native Linux Host is not executable" >&2
        exit 1
    }
    test -x "$PACKAGE_DIR/$SERVER_NAME" || {
        echo "ERROR: native Linux server is not executable" >&2
        exit 1
    }
    test -s "$PACKAGE_DIR/commonwealth-online.service.example" || {
        echo "ERROR: native Linux package is missing the systemd example" >&2
        exit 1
    }
    grep -q '^ExecStart=/opt/commonwealth-online/CommonwealthOnline.Server --config ' \
        "$PACKAGE_DIR/commonwealth-online.service.example" || {
        echo "ERROR: systemd example does not use the native CLI contract" >&2
        exit 1
    }
    if grep -q 'CommonwealthOnline.Server serve ' "$PACKAGE_DIR/commonwealth-online.service.example"; then
        echo "ERROR: native systemd example still uses the managed serve verb" >&2
        exit 1
    fi
    readelf -d "$PACKAGE_DIR/$SERVER_NAME" | grep -Fq '$ORIGIN' || {
        echo "ERROR: native Linux server does not use a relocatable \$ORIGIN runtime path" >&2
        exit 1
    }

    server_ldd="$(ldd "$PACKAGE_DIR/$SERVER_NAME")"
    bridge_ldd="$(ldd "$PACKAGE_DIR/$BRIDGE_NAME")"
    if grep -q 'not found' <<<"$server_ldd"; then
        echo "ERROR: native Linux server has an unresolved runtime dependency" >&2
        printf '%s\n' "$server_ldd" >&2
        exit 1
    fi
    if grep -q 'not found' <<<"$bridge_ldd"; then
        echo "ERROR: native Linux bridge has an unresolved runtime dependency" >&2
        printf '%s\n' "$bridge_ldd" >&2
        exit 1
    fi
    grep -Fq "$PACKAGE_DIR/$BRIDGE_NAME" <<<"$server_ldd" || {
        echo "ERROR: native Linux server does not resolve the packaged GNS bridge" >&2
        printf '%s\n' "$server_ldd" >&2
        exit 1
    }
    if grep -q 'GameNetworkingSockets' <<<"$bridge_ldd"; then
        echo "ERROR: packaged Linux bridge depends on a loose GameNetworkingSockets library" >&2
        exit 1
    fi
else
    command -v llvm-readobj >/dev/null || { echo "ERROR: llvm-readobj was not found on PATH." >&2; exit 1; }
    grep -q 'static MSVC C/C++ runtime' "$PACKAGE_DIR/NATIVE_RUNTIME.txt" || {
        echo "ERROR: native runtime note does not document the Windows static-runtime contract" >&2
        exit 1
    }

    require_pe64() {
        local file="$1"
        local label="$2"
        local headers
        headers="$(llvm-readobj --file-headers "$file")"
        grep -q 'IMAGE_FILE_MACHINE_AMD64' <<<"$headers" || {
            echo "ERROR: $label is not AMD64" >&2
            exit 1
        }
        grep -q 'Magic: 0x20B' <<<"$headers" || {
            echo "ERROR: $label is not PE32+" >&2
            exit 1
        }
    }

    require_pe64 "$PACKAGE_DIR/$HOST_NAME" "native package Host"
    require_pe64 "$PACKAGE_DIR/$SERVER_NAME" "native package server"
    require_pe64 "$PACKAGE_DIR/$BRIDGE_NAME" "native package GNS bridge"

    server_imports="$(llvm-readobj --coff-imports "$PACKAGE_DIR/$SERVER_NAME" | awk '/Name:/ {print tolower($2)}' | sort -u)"
    for required in commonwealth_online_gns_bridge.dll bcrypt.dll ws2_32.dll; do
        grep -Fxq "$required" <<<"$server_imports" || {
            echo "ERROR: native Windows server is missing required import $required" >&2
            exit 1
        }
    done
    if grep -qE 'gamenetworkingsockets|^(libcrypto|libssl)|^(vcruntime|msvcp)[0-9]+\.dll$' <<<"$server_imports"; then
        echo "ERROR: native Windows server has a loose runtime dependency" >&2
        printf '%s\n' "$server_imports" >&2
        exit 1
    fi

    bridge_imports="$(llvm-readobj --coff-imports "$PACKAGE_DIR/$BRIDGE_NAME" | awk '/Name:/ {print tolower($2)}' | sort -u)"
    grep -Fxq 'bcrypt.dll' <<<"$bridge_imports" || {
        echo "ERROR: native Windows bridge does not use the BCrypt backend" >&2
        exit 1
    }
    if grep -qE 'gamenetworkingsockets|^libprotobuf|^(libcrypto|libssl)|^(vcruntime|msvcp)[0-9]+\.dll$' <<<"$bridge_imports"; then
        echo "ERROR: native Windows bridge has a loose runtime dependency" >&2
        printf '%s\n' "$bridge_imports" >&2
        exit 1
    fi
fi

printf 'native package verified: %s (%s)\n' "$PACKAGE_DIR" "$RID"
