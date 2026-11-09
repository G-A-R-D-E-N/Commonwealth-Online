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
    echo "CCACHE_MAXSIZE=150M"
    echo "CO_XMAKE_CCACHE=--ccache=y"
    echo "CMAKE_C_COMPILER_LAUNCHER=ccache"
    echo "CMAKE_CXX_COMPILER_LAUNCHER=ccache"
} >> "$GITHUB_ENV"
