#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ $# -ne 3 ]]; then
    echo "usage: $0 <package-directory> <live-gns-smoke-client> <smoke-bridge-directory>" >&2
    exit 2
fi

PACKAGE_DIR="$(realpath "$1")"
SMOKE_CLIENT="$(realpath "$2")"
SMOKE_BRIDGE_DIR="$(realpath "$3")"
SERVER="$PACKAGE_DIR/CommonwealthOnline.Server"

for required in "$SERVER" "$PACKAGE_DIR/libcommonwealth_online_gns_bridge.so" "$SMOKE_CLIENT"; do
    test -s "$required" || { echo "ERROR: runtime smoke input is missing: $required" >&2; exit 1; }
done

free_udp_port() {
    bash "$SCRIPT_DIR/free-port.sh" udp
}

free_tcp_port() {
    bash "$SCRIPT_DIR/free-port.sh" tcp
}

game_port="$(free_udp_port)"
admin_port="$(free_tcp_port)"
while [[ "$admin_port" == "$game_port" ]]; do admin_port="$(free_tcp_port)"; done
discovery_port="$(free_udp_port)"
while [[ "$discovery_port" == "$game_port" ]]; do discovery_port="$(free_udp_port)"; done

scratch="$(mktemp -d "${RUNNER_TEMP:-/tmp}/co-native-package-smoke.XXXXXX")"
config="$scratch/package-smoke.json"
log="$scratch/package-smoke.log"
cat > "$config" <<JSON
{
  "host": "127.0.0.1",
  "port": $game_port,
  "admin_port": $admin_port,
  "lan_discovery_port": $discovery_port,
  "server_name": "Native Package Smoke",
  "max_players": 4
}
JSON
: > "$log"

"$SERVER" --config "$config" >"$log" 2>&1 &
server_pid=$!

cleanup() {
    kill -TERM "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
    cat "$log" || true
    rm -rf "$scratch"
}
trap cleanup EXIT

ready=0
for _ in $(seq 1 100); do
    if ! kill -0 "$server_pid" 2>/dev/null; then
        echo "ERROR: packaged native server exited before ready" >&2
        exit 1
    fi
    if grep -q "listening on 127.0.0.1:$game_port (GNS)" "$log"; then
        ready=1
        break
    fi
    sleep 0.05
done
[[ "$ready" == 1 ]] || { echo "ERROR: packaged native server did not become ready" >&2; exit 1; }

LD_LIBRARY_PATH="$SMOKE_BRIDGE_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$SMOKE_CLIENT" 127.0.0.1 "$game_port"

printf '%s\n' "native Linux package runtime smoke passed"
