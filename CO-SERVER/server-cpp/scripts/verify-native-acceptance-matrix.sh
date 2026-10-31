#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
CPP_DIR="$ROOT_DIR/CO-SERVER/server-cpp"
XMAKE="$CPP_DIR/xmake.lua"
WORKFLOW="$ROOT_DIR/.github/workflows/co-server-acceptance.yml"

unit_targets=(
    CommonwealthOnlineServerProtocolTests
    CommonwealthOnlineServerInterestTests
    CommonwealthOnlineServerReplayTests
    CommonwealthOnlineServerWorldCombatTests
    CommonwealthOnlineServerSharedCoreParityTests
    CommonwealthOnlineServerLoadTests
    CommonwealthOnlineServerIdentityGateTests
    CommonwealthOnlineServerBanStoreTests
    CommonwealthOnlineServerAdminCommandDispatcherTests
    CommonwealthOnlineServerPersistenceTests
    CommonwealthOnlineServerPersistenceGateTests
    CommonwealthOnlineServerInventoryTransferTests
)

live_targets=(
    CommonwealthOnlineServerLiveNamedGnsSmoke
    CommonwealthOnlineServerLiveNetworkFaultGnsSmoke
    CommonwealthOnlineServerLiveProtocolFaultGnsSmoke
    CommonwealthOnlineServerLiveAuthorityChurnGnsSmoke
    CommonwealthOnlineServerLiveAdmissionGnsSmoke
    CommonwealthOnlineServerLiveReconnectChurnGnsSmoke
    CommonwealthOnlineServerLiveProfileBanGnsSmoke
)

for target_name in "${unit_targets[@]}"; do
    grep -Eq "(target|server_test)\(\"$target_name\"" "$XMAKE" || {
        echo "ERROR: native regression target is missing: $target_name" >&2
        exit 1
    }
done

for target_name in "${live_targets[@]}"; do
    grep -Eq "(target|server_test)\(\"$target_name\"" "$XMAKE" || {
        echo "ERROR: live native acceptance target is missing: $target_name" >&2
        exit 1
    }
    grep -Fq "xmake build -y $target_name" "$WORKFLOW" || {
        echo "ERROR: canonical acceptance does not build $target_name" >&2
        exit 1
    }
    grep -Fq "xmake run $target_name" "$WORKFLOW" || {
        echo "ERROR: canonical acceptance does not execute $target_name" >&2
        exit 1
    }
done

for source in \
    LiveNamedGnsSmoke.cpp \
    LiveNetworkFaultGnsSmoke.cpp \
    LiveProtocolFaultGnsSmoke.cpp \
    LiveAuthorityChurnGnsSmoke.cpp \
    LiveAdmissionGnsSmoke.cpp \
    LiveReconnectChurnGnsSmoke.cpp \
    LiveProfileBanGnsSmoke.cpp \
    ServerSharedCoreParityTests.cpp \
    ServerLoadTests.cpp; do
    test -s "$CPP_DIR/tests/$source" || {
        echo "ERROR: native acceptance source is missing: $source" >&2
        exit 1
    }
done

# Named/replay behavior, network impairment, protocol faults/rate limits,
# authority churn, reconnect churn, capacity admission, parity and load are
# required before the managed acceptance tree can be retired.
for evidence in \
    'npc replay: cached nearby NPC state' \
    '100% fake packet loss' \
    'rate_limited' \
    'authority churn: deterministic handoff' \
    'reconnect churn: interest scope follows the reconnected session' \
    'server_full' \
    'profile_banned'; do
    if ! grep -R -Fq "$evidence" "$CPP_DIR/tests"; then
        echo "ERROR: native acceptance evidence marker is missing: $evidence" >&2
        exit 1
    fi
done

printf '%s\n' "native acceptance matrix verified: regression, live GNS, parity, and load evidence are wired"
