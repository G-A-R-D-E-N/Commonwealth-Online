#!/usr/bin/env bash
# Install CommonwealthOnline-Matrix self-hosted runners on this build host.
# Requires: sudo, gh auth with repo admin on G-A-R-D-E-N/CommonwealthOnline-Matrix
set -euo pipefail

GHRUNNER_DIR="${GHRUNNER_DIR:-}"
if [[ -z "$GHRUNNER_DIR" ]]; then
  for cand in \
    "$HOME/repos/GHRUNNER" \
    /run/media/user/Games-Storage/Projects/GHRUNNER
  do
    if [[ -f "$cand/scripts/install-runner.sh" && -f "$cand/configs/fleet.json" ]]; then
      GHRUNNER_DIR="$cand"
      break
    fi
  done
fi

if [[ -z "${GHRUNNER_DIR:-}" ]]; then
  echo "GHRUNNER checkout not found. Set GHRUNNER_DIR=/path/to/GHRUNNER" >&2
  exit 1
fi

VER=2.336.0
SHA=04cf0be1aff4c3ec3554466c39124ca250e3effd8873bb7e8d68535aa9505d5d
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
TARBALL="${RUNNER_TARBALL:-$ROOT/.tmp/actions-runner-linux-x64-${VER}.tar.gz}"

mkdir -p "$(dirname "$TARBALL")"
if [[ ! -f "$TARBALL" ]]; then
  echo "Downloading actions-runner ${VER}..."
  curl -fsSL -o "$TARBALL" \
    "https://github.com/actions/runner/releases/download/v${VER}/actions-runner-linux-x64-${VER}.tar.gz"
fi
echo "${SHA}  ${TARBALL}" | sha256sum -c -

cd "$GHRUNNER_DIR"
git fetch origin
git checkout main
git pull --ff-only origin main

for key in matrix-ci matrix-sync; do
  if [[ -d "/opt/actions-runner-${key}" ]]; then
    echo "already installed: /opt/actions-runner-${key} (skipping $key)"
    continue
  fi
  echo "Installing $key..."
  token="$(gh api -X POST repos/G-A-R-D-E-N/CommonwealthOnline-Matrix/actions/runners/registration-token --jq .token)"
  printf '%s\n' "$token" | ./scripts/install-runner.sh "$key" "$TARBALL"
  unset token
done

echo
echo "Registered Matrix runners:"
gh api repos/G-A-R-D-E-N/CommonwealthOnline-Matrix/actions/runners \
  --jq '.runners[] | "\(.name)\t\(.status)\t\([.labels[].name] | join(","))"'
echo
echo "Done. Queued PR #21 jobs should start picking up matrix-ci."
