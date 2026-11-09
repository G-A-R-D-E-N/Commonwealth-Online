#!/usr/bin/env bash

co_debug_enabled() {
    local value="${COMMONWEALTH_ONLINE_DEBUG:-on}"
    value="${value#"${value%%[![:space:]]*}"}"
    value="${value%"${value##*[![:space:]]}"}"
    value="${value,,}"
    case "$value" in
        0|false|no|off|release|disabled)
            return 1
            ;;
    esac
    return 0
}

co_debug() {
    local component="${1:-Script}"
    shift || true
    if co_debug_enabled; then
        printf '[CO DEBUG][%s] %s\n' "$component" "$*"
    fi
}
