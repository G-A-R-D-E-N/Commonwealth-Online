# Commonwealth Online — Server & Host

The dedicated server and its host GUI for Commonwealth Online (Fallout 4
multiplayer).

## Components

- **[`server-cpp/`](server-cpp/README.md)** — the authoritative dedicated
  server. C++23, sharing engine-independent behaviour with the F4SE client
  through [`co-core`](../co-core), speaking GameNetworkingSockets over UDP.
- **[`host/`](host/README.md)** — the Avalonia cross-platform host GUI:
  start/stop the server, edit config, watch the log, kick and ban players.
- **`server/native_transport/`** — the C bridge the server and client use to
  reach GameNetworkingSockets.
- **`host-tests/`** — the Host launch contract and native config tests.

The managed C# authoritative server was retired; see
[`../MIGRATION.md`](../MIGRATION.md).

## Quick start — packaged server

```bash
./CommonwealthOnline.Server --config commonwealth-server.json
```

A `CommonwealthOnline.Server.native` marker sits beside the executable in a
native package, and `NATIVE_RUNTIME.txt` records what the package expects from
the operating system.

## Quick start — host GUI

```bash
dotnet run --project host/CommonwealthOnline.Host.csproj
```

The Host launches the native server from the package directory beside it. It
does not build or run a server from source.

## Build

```bash
bash server/scripts/build-gns-bridge.sh
cd server-cpp && xmake f -y -m release && xmake build -y CommonwealthOnlineServerCpp
dotnet build host/CommonwealthOnline.Host.csproj -c Release
```

The bridge must be built first: the server links against it.

## Ports

| Purpose | Default | Protocol |
| --- | --- | --- |
| Gameplay (GNS) | 7777 | UDP |
| LAN discovery | 7778 | UDP, `--lan-discovery-port`, `--no-lan-discovery` |
| Admin control | 7779 | TCP, localhost only |

`0.0.0.0` is a bind address, not the address players join. The admin port must
remain localhost-only.
