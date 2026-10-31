#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

for name in co-server-package.yml co-server-release-acceptance.yml co-server-publish-host.yml; do
    # Private keeps public CI workflows under .github/public-ci/workflows; the export moves them to .github/workflows.
    workflow="$ROOT_DIR/.github/workflows/$name"
    [[ -e "$workflow" ]] || workflow="$ROOT_DIR/.github/public-ci/workflows/$name"
    # The public CI export omits the private publisher; private-only-checks.yml sets CO_REQUIRE_PUBLISHER=1.
    if [[ ! -e "$workflow" && "$name" == co-server-publish-host.yml && "${CO_REQUIRE_PUBLISHER:-}" != 1 ]]; then
        continue
    fi
    test -s "$workflow" || { echo "ERROR: required release workflow is missing: $workflow" >&2; exit 1; }
    grep -Fq 'build-native-package-ci.sh' "$workflow" || {
        echo "ERROR: release workflow no longer uses the native package build driver: $workflow" >&2
        exit 1
    }
    if grep -Eq 'server/scripts/package-release\.sh|CommonwealthOnline\.Server\.csproj|CommonwealthOnline\.Server serve|end-to-end TCP|tcp-smoke|tcp-clean|TCP Package' "$workflow"; then
        echo "ERROR: managed/TCP production release path reappeared in $workflow" >&2
        exit 1
    fi
done

scripts=(
    "$SCRIPT_DIR/build-native-package-ci.sh"
    "$SCRIPT_DIR/package-native-release.sh"
    "$SCRIPT_DIR/verify-native-package.sh"
    "$SCRIPT_DIR/verify-native-release-path.sh"
    "$SCRIPT_DIR/smoke-native-linux-package.sh"
    "$SCRIPT_DIR/smoke-native-linux-release.sh"
)

for script in "${scripts[@]}"; do
    test -s "$script" || { echo "ERROR: native release component is missing: $script" >&2; exit 1; }
    bash -n "$script" || {
        echo "ERROR: native release script has invalid shell syntax: $script" >&2
        exit 1
    }
done

printf '%s\n' "native release path verified: package, release acceptance, and publisher use the C++ authoritative server"
