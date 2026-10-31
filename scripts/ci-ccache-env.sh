#!/usr/bin/env bash
set -euo pipefail

if ! command -v ccache >/dev/null 2>&1; then
    exit 0
fi

mkdir -p "$HOME/.ccache"
{
    echo "CCACHE_DIR=$HOME/.ccache"
    echo "CCACHE_BASEDIR=$GITHUB_WORKSPACE"
    echo "CCACHE_NOHASHDIR=1"
    echo "CCACHE_MAXSIZE=20G"
    echo "CO_XMAKE_CCACHE=--ccache=y"
} >> "$GITHUB_ENV"
