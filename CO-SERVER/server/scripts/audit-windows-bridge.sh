#!/usr/bin/env bash
# Audit a cross-built Windows GNS bridge DLL: PE shape, exports, and the
# absence of loose runtime dependencies.
#
# usage: audit-windows-bridge.sh <path-to-dll>
set -euo pipefail

BRIDGE="${1:?usage: $0 <path-to-commonwealth_online_gns_bridge.dll>}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HEADER="$(cd "$SCRIPT_DIR/../.." && pwd)/server/native_transport/co_gns_server_bridge.h"

test -s "$BRIDGE" || { echo "::error::bridge DLL is missing or empty: $BRIDGE"; exit 1; }
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

# The bridge statically links GNS, protobuf and the CRT. Any of these appearing
# as an import means the package would need a loose DLL beside it at runtime.
check_absent() {
    # $1 = extended regex, $2 = message shown when the import is present
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
    | sed 's/.*CO_GNS_API[^a-z_]*//' | awk '{print $2}' | sed 's/(.*//' | sort -u)"
exported="$(llvm-readobj --coff-exports "$BRIDGE" | awk '/Name:/ {print $2}' | sort -u)"

missing="$(comm -23 <(printf '%s\n' "$declared") <(printf '%s\n' "$exported"))"
if [[ -n "$missing" ]]; then
    echo "::error::bridge is missing declared CO_GNS_API exports:"
    printf '  %s\n' $missing
    status=1
else
    echo "all $(wc -l <<<"$declared") declared CO_GNS_API exports present"
fi

exit "$status"
