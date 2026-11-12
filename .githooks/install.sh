#!/usr/bin/env bash
# Install these guardrail hooks. Requires Node on PATH.
set -e

MODE="${1:-repo}"

if [ "$MODE" = "global" ]; then
  DEST="$HOME/.githooks"
  mkdir -p "$DEST"
  cp "$(dirname "$0")/commit-msg" "$(dirname "$0")/pre-commit" "$(dirname "$0")/pre-push" "$DEST/"
  chmod +x "$DEST"/commit-msg "$DEST"/pre-commit "$DEST"/pre-push
  git config --global core.hooksPath "$DEST"
  echo "Installed globally -> $DEST (every repo uses these)."
else
  chmod +x "$(dirname "$0")"/commit-msg "$(dirname "$0")"/pre-commit "$(dirname "$0")"/pre-push
  git config core.hooksPath .githooks
  echo "Installed for THIS repo -> .githooks (core.hooksPath set)."
fi

echo "Verify: git config --get core.hooksPath"
