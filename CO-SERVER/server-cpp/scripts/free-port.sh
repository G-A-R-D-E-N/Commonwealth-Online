#!/usr/bin/env bash
# Prints a free loopback port. Callers bind it shortly afterwards, so this is
# advisory in the same way an ephemeral bind-and-close was: it reports a port
# nothing is listening on right now.
set -Eeuo pipefail

PROTOCOL="${1:-udp}"
case "$PROTOCOL" in
    udp) LISTEN_FLAGS=(-lun) ;;
    tcp) LISTEN_FLAGS=(-ltn) ;;
    *) echo "usage: $0 <udp|tcp>" >&2; exit 2 ;;
esac

command -v ss >/dev/null || { echo "ERROR: ss (iproute2) is required to pick a free port" >&2; exit 1; }

for _ in $(seq 1 200); do
    # Above the well-known range, below the usual ephemeral range so the kernel
    # is unlikely to hand the same port to something else mid-run.
    candidate=$(( 20000 + RANDOM % 25000 ))
    if [[ -z "$(ss -H "${LISTEN_FLAGS[@]}" "sport = :$candidate")" ]]; then
        printf '%s\n' "$candidate"
        exit 0
    fi
done

echo "ERROR: could not find a free $PROTOCOL port after 200 attempts" >&2
exit 1
