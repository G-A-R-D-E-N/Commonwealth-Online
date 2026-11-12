#!/usr/bin/env bash
set -euo pipefail
for platform in linux windows; do
  output=$(cd CO-BETA/plugin && xmake f --file=xmake.lua -p "$platform" --require=n -y 2>&1) && { echo "::error::game plugin configuration accepted Linux host"; exit 1; }
  [[ "$output" == *"The game plugin can only be built on a Windows host"* ]] || { echo "$output"; exit 1; }
done
for project in CO-BETA/ci/protocol-acceptance CommonwealthOnlineUI; do
  file=xmake.lua
  [ "$project" != CommonwealthOnlineUI ] || file=xmake-parser-ci.lua
  for platform in linux windows; do
    output=$(cd "$project" && xmake f --file="$file" -p "$platform" --require=n -y 2>&1 && xmake build --file="$file" -y 2>&1) && { echo "::error::client build accepted Linux host"; exit 1; }
    [[ "$output" == *"Game client tests can only be built on a Windows host"* ]] || { echo "$output"; exit 1; }
  done
done
for file in xmake-voice.lua xmake-quest-tests.lua; do
  for platform in linux windows; do
    output=$(cd CO-BETA/plugin && xmake f --file="$file" -p "$platform" --require=n -y 2>&1 && xmake build --file="$file" -y 2>&1) && { echo "::error::client build accepted Linux host"; exit 1; }
    [[ "$output" == *"Game client tests can only be built on a Windows host"* ]] || { echo "$output"; exit 1; }
  done
done
output=$(cmake -S CO-BETA/plugin/gns-bridge -B "$RUNNER_TEMP/client-bridge-host-guard" 2>&1) && { echo "::error::client bridge accepted Linux host"; exit 1; }
[[ "$output" == *"The game client bridge can only be built on a Windows host"* ]] || { echo "$output"; exit 1; }
