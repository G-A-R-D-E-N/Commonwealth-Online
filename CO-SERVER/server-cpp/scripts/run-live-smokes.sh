#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SERVER_BIN="$CPP_DIR/build/linux/x86_64/debug/CommonwealthOnlineServerCpp"
FREE_PORT="$CPP_DIR/scripts/free-port.sh"
RUN_TAG="${GITHUB_RUN_ID:-local}-${GITHUB_RUN_ATTEMPT:-0}"
LOG_DIR="${RUNNER_TEMP:-/tmp}"
SMOKE_CONFIG="$CPP_DIR/.run-live-smokes-config.json"

SERVER_PIDS=()
SERVER_LOGS=()
SERVER_PORT=""
SERVER_ADMIN_PORT=""

# Each smoke gets its own server: a connect-attempt budget is per-server state.
stop_servers() {
    local pid log
    for pid in "${SERVER_PIDS[@]}"; do
        kill -TERM "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    done
    SERVER_PIDS=()
    for log in "${SERVER_LOGS[@]}"; do
        cat "$log" || true
    done
    SERVER_LOGS=()
}

start_server() {
    local name="$1" client_version="$2"
    shift 2
    local log="$LOG_DIR/co-native-$name-$RUN_TAG.log"
    : > "$log"
    printf '{"required_client_version": "%s"}\n' "$client_version" > "$SMOKE_CONFIG"
    local port admin_port
    port="$(bash "$FREE_PORT" udp)"
    admin_port="$(bash "$FREE_PORT" tcp)"
    while [[ "$admin_port" == "$port" ]]; do admin_port=$((admin_port + 1)); done
    "$SERVER_BIN" --config "$SMOKE_CONFIG" --bind 127.0.0.1 --port "$port" --admin-port "$admin_port" --no-public-listing "$@" >"$log" 2>&1 &
    local pid=$!
    SERVER_PIDS+=("$pid")
    SERVER_LOGS+=("$log")
    SERVER_PORT="$port"
    SERVER_ADMIN_PORT="$admin_port"
    local ready=0 i
    for i in $(seq 1 600); do
        if ! kill -0 "$pid" 2>/dev/null; then
            echo "::error::$name native server exited before becoming ready"
            return 1
        fi
        if grep -q "listening on 127.0.0.1:$port (GNS)" "$log"; then
            ready=1
            break
        fi
        sleep 0.05
    done
    [[ "$ready" == 1 ]] || { echo "::error::$name native server did not become ready"; return 1; }
}

trap 'stop_servers; rm -f "$SMOKE_CONFIG"' EXIT
cd "$CPP_DIR"

echo "==> named native GNS acceptance"
start_server acceptance named-smoke-1 --no-lan-discovery
xmake run CommonwealthOnlineServerLiveNamedGnsSmoke 127.0.0.1 "$SERVER_PORT"
stop_servers

echo "==> network-fault native GNS acceptance"
start_server network-fault network-fault-smoke-1 --no-lan-discovery
xmake run CommonwealthOnlineServerLiveNetworkFaultGnsSmoke 127.0.0.1 "$SERVER_PORT"
stop_servers

echo "==> protocol-fault and rate-limit native GNS acceptance"
start_server protocol-fault protocol-fault-smoke-1 --no-lan-discovery
xmake run CommonwealthOnlineServerLiveProtocolFaultGnsSmoke 127.0.0.1 "$SERVER_PORT"
stop_servers

echo "==> NPC authority churn native GNS acceptance"
start_server authority-churn authority-churn-smoke-1 --no-lan-discovery
xmake run CommonwealthOnlineServerLiveAuthorityChurnGnsSmoke 127.0.0.1 "$SERVER_PORT"
stop_servers

echo "==> reconnect churn native GNS acceptance"
start_server reconnect-churn reconnect-churn-smoke-1 --no-lan-discovery
xmake run CommonwealthOnlineServerLiveReconnectChurnGnsSmoke 127.0.0.1 "$SERVER_PORT"
# Credential ageing needs its own server: the scenarios above assume a long-lived resume token.
start_server reconnect-expiry reconnect-churn-smoke-1 --resume-token-lifetime-seconds 3 --no-lan-discovery
xmake run CommonwealthOnlineServerLiveReconnectChurnGnsSmoke 127.0.0.1 "$SERVER_PORT" 3
stop_servers

echo "==> capacity admission native GNS acceptance"
start_server admission admission-smoke-1 --no-lan-discovery --max-players 2
xmake run CommonwealthOnlineServerLiveAdmissionGnsSmoke 127.0.0.1 "$SERVER_PORT"
stop_servers

echo "==> profile-ban native acceptance"
rm -f .admin-token
(umask 077; printf 'weak-token' > .admin-token)
start_server profile-ban profile-ban-smoke-1 --no-lan-discovery
for _ in $(seq 1 80); do
    [[ -s .admin-token && "$(cat .admin-token)" != "weak-token" ]] && break
    sleep 0.05
done
[[ -s .admin-token && "$(cat .admin-token)" != "weak-token" ]] || { echo "::error::profile-ban weak admin token was not replaced"; exit 1; }
xmake run CommonwealthOnlineServerLiveProfileBanGnsSmoke 127.0.0.1 "$SERVER_PORT" "$SERVER_ADMIN_PORT" "$PWD/.admin-token"
stop_servers
rm -f .admin-token

echo "==> voice flood native GNS acceptance"
start_server voice-flood voice-flood-smoke-1
xmake run CommonwealthOnlineServerLiveVoiceFloodGnsSmoke 127.0.0.1 "$SERVER_PORT"
stop_servers
