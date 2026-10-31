#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ $# -ne 6 ]]; then
    echo "usage: $0 <package-dir> <identity-smoke> <persistence-smoke> <admin-smoke> <profile-ban-smoke> <smoke-bridge-dir>" >&2
    exit 2
fi

PACKAGE_DIR="$(realpath "$1")"
IDENTITY_SMOKE="$(realpath "$2")"
PERSISTENCE_SMOKE="$(realpath "$3")"
ADMIN_SMOKE="$(realpath "$4")"
PROFILE_BAN_SMOKE="$(realpath "$5")"
SMOKE_BRIDGE_DIR="$(realpath "$6")"
SERVER="$PACKAGE_DIR/CommonwealthOnline.Server"

for required in "$SERVER" "$PACKAGE_DIR/libcommonwealth_online_gns_bridge.so" \
                "$IDENTITY_SMOKE" "$PERSISTENCE_SMOKE" "$ADMIN_SMOKE" "$PROFILE_BAN_SMOKE"; do
    test -s "$required" || { echo "ERROR: release smoke input is missing: $required" >&2; exit 1; }
done

free_udp_port() {
    bash "$SCRIPT_DIR/free-port.sh" udp
}

free_tcp_port() {
    bash "$SCRIPT_DIR/free-port.sh" tcp
}

admin_port_for() {
    local game_port="$1"
    local port
    port="$(free_tcp_port)"
    while [[ "$port" == "$game_port" ]]; do port="$(free_tcp_port)"; done
    printf '%s\n' "$port"
}

discovery_port_for() {
    local game_port="$1"
    local port
    port="$(free_udp_port)"
    while [[ "$port" == "$game_port" ]]; do port="$(free_udp_port)"; done
    printf '%s\n' "$port"
}

scratch="$(mktemp -d "${RUNNER_TEMP:-/tmp}/co-native-release-smoke.XXXXXX")"
SERVER_PID=""
SERVER_LOG=""

stop_server() {
    if [[ -n "$SERVER_PID" ]]; then
        kill -TERM "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=""
    fi
}

cleanup() {
    stop_server
    if [[ -n "$SERVER_LOG" && -f "$SERVER_LOG" ]]; then
        cat "$SERVER_LOG" || true
    fi
    rm -rf "$scratch"
}
trap cleanup EXIT

start_server() {
    local config="$1"
    local game_port="$2"
    local log="$3"
    stop_server
    SERVER_LOG="$log"
    : > "$SERVER_LOG"
    "$SERVER" --config "$config" >"$SERVER_LOG" 2>&1 &
    SERVER_PID=$!

    local ready=0
    for _ in $(seq 1 120); do
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            cat "$SERVER_LOG" || true
            echo "ERROR: packaged native server exited before ready" >&2
            exit 1
        fi
        if grep -q "listening on 127.0.0.1:$game_port (GNS)" "$SERVER_LOG"; then
            ready=1
            break
        fi
        sleep 0.05
    done
    [[ "$ready" == 1 ]] || { cat "$SERVER_LOG" || true; echo "ERROR: packaged native server did not become ready" >&2; exit 1; }
}

run_client() {
    LD_LIBRARY_PATH="$SMOKE_BRIDGE_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$@"
}

# Mandatory identity/resume behavior on a clean native package.
identity_game="$(free_udp_port)"
identity_admin="$(admin_port_for "$identity_game")"
identity_discovery="$(discovery_port_for "$identity_game")"
cat > "$scratch/identity.json" <<JSON
{
  "host": "127.0.0.1",
  "port": $identity_game,
  "admin_port": $identity_admin,
  "lan_discovery_port": $identity_discovery,
  "server_name": "Native Release Identity",
  "max_players": 8,
  "require_v2_identity": true,
  "enable_persistence": false,
  "enable_chat": true,
  "enable_factions": true
}
JSON
start_server "$scratch/identity.json" "$identity_game" "$scratch/identity.log"
run_client "$IDENTITY_SMOKE" 127.0.0.1 "$identity_game"
stop_server

# Durable state and resume credentials must survive an actual process restart.
persistence_game="$(free_udp_port)"
persistence_admin="$(admin_port_for "$persistence_game")"
persistence_discovery="$(discovery_port_for "$persistence_game")"
cat > "$scratch/persistence.json" <<JSON
{
  "host": "127.0.0.1",
  "port": $persistence_game,
  "admin_port": $persistence_admin,
  "lan_discovery_port": $persistence_discovery,
  "server_name": "Native Release Persistence",
  "max_players": 8,
  "require_v2_identity": true,
  "enable_persistence": true,
  "enable_chat": true,
  "enable_factions": true,
  "durable_state_path": "durable-state.json"
}
JSON
resume_token="$scratch/persistence-resume-token.txt"
start_server "$scratch/persistence.json" "$persistence_game" "$scratch/persistence-seed.log"
run_client "$PERSISTENCE_SMOKE" seed 127.0.0.1 "$persistence_game" "$resume_token" "$scratch/durable-state.json"
stop_server

test -s "$scratch/durable-state.json" || { echo "ERROR: durable state was not written before restart" >&2; exit 1; }
start_server "$scratch/persistence.json" "$persistence_game" "$scratch/persistence-resume.log"
run_client "$PERSISTENCE_SMOKE" resume 127.0.0.1 "$persistence_game" "$resume_token"
stop_server

# The packaged native runtime must expose the authenticated admin contract used
# by the Avalonia Host.
admin_game="$(free_udp_port)"
admin_port="$(admin_port_for "$admin_game")"
admin_discovery="$(discovery_port_for "$admin_game")"
cat > "$scratch/admin.json" <<JSON
{
  "host": "127.0.0.1",
  "port": $admin_game,
  "admin_port": $admin_port,
  "lan_discovery_port": $admin_discovery,
  "server_name": "Native Release Admin",
  "max_players": 8,
  "require_v2_identity": true,
  "enable_persistence": false,
  "enable_chat": true,
  "enable_factions": true
}
JSON
start_server "$scratch/admin.json" "$admin_game" "$scratch/admin.log"
for _ in $(seq 1 80); do
    [[ -s "$scratch/.admin-token" ]] && break
    sleep 0.05
done
test -s "$scratch/.admin-token" || { echo "ERROR: packaged native server did not create admin token" >&2; exit 1; }
run_client "$ADMIN_SMOKE" 127.0.0.1 "$admin_game" "$admin_port" "$scratch/.admin-token"
stop_server

rm -f "$scratch/.admin-token"
start_server "$scratch/admin.json" "$admin_game" "$scratch/profile-ban.log"
for _ in $(seq 1 80); do
    [[ -s "$scratch/.admin-token" ]] && break
    sleep 0.05
done
test -s "$scratch/.admin-token" || { echo "ERROR: packaged native server did not recreate an admin token" >&2; exit 1; }
run_client "$PROFILE_BAN_SMOKE" 127.0.0.1 "$admin_game" "$admin_port" "$scratch/.admin-token"
stop_server

printf '%s\n' "native Linux release acceptance smoke passed"
