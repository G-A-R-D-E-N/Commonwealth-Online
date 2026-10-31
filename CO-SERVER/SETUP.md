# Setup

## Native server development

Required:

- a C++23 toolchain (clang on Linux, clang-cl + xwin for Windows cross-builds)
- [xmake](https://xmake.io) 3.x
- cmake, ninja, protoc, git — the GNS bridge build needs them
- OpenSSL development headers on Linux
- .NET 8 SDK, for the Host and its tests

Build the bridge, then the server:

```bash
bash server/scripts/build-gns-bridge.sh
cd server-cpp
xmake f -y -m debug
xmake build -y CommonwealthOnlineServerCpp
xmake test -y
```

Run from a source build:

```bash
./build/linux/x86_64/debug/CommonwealthOnlineServerCpp \
  --bind 127.0.0.1 --port 7777 --admin-port 7779
```

`--help` lists every flag. Anything set on the command line can also live in a
config file passed with `--config`.

## Host development

```bash
dotnet build host/CommonwealthOnline.Host.csproj -c Release
dotnet run --project host-tests/CommonwealthOnline.Host.Tests.csproj -c Release
```

The Host only launches native server packages. A directory without
`CommonwealthOnline.Server` in it is rejected rather than started.

## GameNetworkingSockets

Gameplay transport is GNS/UDP end to end. The bridge sources live in
`server/native_transport`; `server/scripts/build-gns-bridge.sh` builds the
Linux `.so` and `build-gns-bridge-windows-cross.sh` cross-builds the Windows
DLL. A packaged server resolves the bridge from its own directory, so no path
configuration is involved.

## Ports

- Gameplay: UDP 7777 by default
- LAN discovery: UDP 7778 by default, configurable or disablable
- Admin control: TCP 127.0.0.1:7779 by default

The admin port must remain localhost-only.
