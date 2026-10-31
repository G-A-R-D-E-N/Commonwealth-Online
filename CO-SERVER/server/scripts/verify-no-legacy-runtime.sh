#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd -- "${ROOT}"

SELF="server/scripts/verify-no-legacy-runtime.sh"
FAILED=0

echo "==> Checking tracked paths for prohibited legacy runtime artifacts"
while IFS= read -r -d '' path; do
  lower="${path,,}"
  if [[ "${lower}" =~ (^|/)(requirements[^/]*\.txt|pipfile(\.lock)?|pyproject\.toml|poetry\.lock|setup\.py|setup\.cfg|tox\.ini|\.python-version)$ ]] \
      || [[ "${lower}" =~ (^|/)(\.venv|venv|__pycache__|\.pytest_cache)(/|$) ]] \
      || [[ "${lower}" =~ \.(py|pyw|pyi|pyc|pyo|whl|egg)$ ]]; then
    echo "ERROR: prohibited tracked path: ${path}" >&2
    FAILED=1
  fi
done < <(git ls-files -z)

# Keep the retired Python runtime and its dependency surface out of source,
# scripts, workflows, configs, packaging and documentation. This policy file is
# the sole intentional source of the forbidden tokens below.
#
# The interpreter itself is banned again: CI picks free ports with
# server-cpp/scripts/free-port.sh, so nothing in this tree needs Python.
FORBIDDEN='(^|[^[:alnum:]_])(python([0-9]+([.][0-9]+)*)?|py[.]exe|pip([0-9]+)?|pytest|pyside6|virtualenv|venv)([^[:alnum:]_]|$)|[.]venv|consumer_server_cli[.]py|admin_server[.]py|server_core[.]py|requirements-(server|host-gui)[.]txt'

echo "==> Checking tracked text for prohibited legacy runtime references"
if git grep -nI -i -E "${FORBIDDEN}" -- . ":(exclude)${SELF}"; then
  echo "ERROR: prohibited legacy runtime reference found in tracked content." >&2
  FAILED=1
fi

if [[ ${FAILED} -ne 0 ]]; then
  exit 1
fi

echo "Legacy runtime guard passed."
