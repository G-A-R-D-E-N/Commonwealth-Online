#!/usr/bin/env bash
# usage: audit-windows-native-server.sh <bridge.dll> <server.exe>
set -euo pipefail
export LC_ALL=C # comm needs sort's order; locale-aware sort breaks it on uutils

BRIDGE="${1:?usage: $0 <bridge.dll> <server.exe>}"
SERVER_EXE="${2:?usage: $0 <bridge.dll> <server.exe>}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
HEADER="$CO_ROOT/server/native_transport/co_gns_server_bridge.h"

test -s "$BRIDGE" || { echo "::error::bridge DLL is missing or empty: $BRIDGE"; exit 1; }
test -s "$SERVER_EXE" || { echo "::error::native server is missing or empty: $SERVER_EXE"; exit 1; }
test -f "$HEADER" || { echo "::error::bridge header not found: $HEADER"; exit 1; }

status=0

echo "==> PE shape"
file "$BRIDGE"
headers="$(llvm-readobj --file-headers "$BRIDGE")"
grep -q "IMAGE_FILE_MACHINE_AMD64" <<<"$headers" || {
    echo "::error::bridge is not an AMD64 image"; status=1; }
grep -q "Magic: 0x20B" <<<"$headers" || {
    echo "::error::bridge is not PE32+"; status=1; }

echo "==> imports"
imports="$(llvm-readobj --coff-imports "$BRIDGE" | awk '/Name:/ {print tolower($2)}' | sort -u)"
printf '%s\n' "$imports"

check_absent() {
    if grep -qE "$1" <<<"$imports"; then
        echo "::error::$2"
        status=1
    fi
}

check_absent "gamenetworkingsockets" \
    "bridge depends on a loose GameNetworkingSockets DLL"
check_absent "^libprotobuf" \
    "bridge depends on a loose protobuf DLL"
check_absent "^(libcrypto|libssl)" \
    "bridge depends on a loose OpenSSL DLL"
check_absent "^(vcruntime|msvcp)[0-9]+\\.dll" \
    "bridge imports the Visual C++ redistributable; static CRT linkage was expected"

grep -q "^bcrypt\.dll$" <<<"$imports" || {
    echo "::error::bridge does not import bcrypt.dll; Windows crypto backend is not BCrypt"
    status=1; }

echo "==> exports"
declared="$(grep "CO_GNS_API" "$HEADER" \
    | grep -v "define\|ifdef\|else\|endif" \
    | sed 's/.*CO_GNS_API[^a-z_]*//' | awk '{print $2}' | sed 's/(.*//' | sort -u | grep -v '^co_gns_test_')"
exported="$(llvm-readobj --coff-exports "$BRIDGE" | awk '/Name:/ {print $2}' | sort -u)"

if grep -q '^co_gns_test_' <<<"$exported"; then
    echo "::error::release bridge exports test-network symbols"
    status=1
fi

missing="$(comm -23 <(printf '%s\n' "$declared") <(printf '%s\n' "$exported"))"
if [[ -n "$missing" ]]; then
    echo "::error::bridge is missing declared CO_GNS_API exports:"
    printf '  %s\n' $missing
    status=1
else
    echo "all $(wc -l <<<"$declared") declared CO_GNS_API exports present"
fi

echo "==> native server PE shape"
file "$SERVER_EXE"
server_headers="$(llvm-readobj --file-headers "$SERVER_EXE")"
grep -q "IMAGE_FILE_MACHINE_AMD64" <<<"$server_headers" || {
    echo "::error::native server is not an AMD64 image"; status=1; }
grep -q "Magic: 0x20B" <<<"$server_headers" || {
    echo "::error::native server is not PE32+"; status=1; }

echo "==> native server imports"
server_imports="$(llvm-readobj --coff-imports "$SERVER_EXE" | awk '/Name:/ {print tolower($2)}' | sort -u)"
printf '%s\n' "$server_imports"
for required in commonwealth_online_gns_bridge.dll bcrypt.dll ws2_32.dll; do
    grep -Fxq "$required" <<<"$server_imports" || {
        echo "::error::native server is missing required import $required"
        status=1
    }
done
if grep -qE '^(libcrypto|libssl).*\.dll$' <<<"$server_imports"; then
    echo "::error::native Windows server unexpectedly depends on OpenSSL"
    status=1
fi
if grep -q 'gamenetworkingsockets' <<<"$server_imports"; then
    echo "::error::native server depends directly on a loose GameNetworkingSockets DLL"
    status=1
fi

exit "$status"
