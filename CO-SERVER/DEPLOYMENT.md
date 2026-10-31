# Deployment

Releases ship a native package: the C++ authoritative server, the GNS bridge
beside it, the Avalonia Host, a config file, and a `CommonwealthOnline.Server.native`
marker. Nothing in the package needs a .NET runtime to run the server itself.

## Building a package

```bash
bash server-cpp/scripts/build-native-package-ci.sh linux-x64 /path/to/output
bash server-cpp/scripts/build-native-package-ci.sh win-x64   /path/to/output
```

Each build composes the package and then runs `verify-native-package.sh`, which
rejects an incomplete layout, a mixed managed/native layout, and unexpected
loose runtime dependencies. `NATIVE_RUNTIME.txt` in the package records what
the build expects from the operating system.

## Running a Linux server

```bash
./CommonwealthOnline.Server --config commonwealth-server.json
```

The server resolves `libcommonwealth_online_gns_bridge.so` from its own
directory through `$ORIGIN`, so the package is relocatable. OpenSSL's
`libcrypto` is a system dependency.

A systemd example ships as `commonwealth-online.service.example` inside the
Linux package.

## Running a Windows server

```bat
CommonwealthOnline.Server.exe --config commonwealth-server.json
```

`commonwealth_online_gns_bridge.dll` ships beside the executable. The build is
statically linked against the MSVC runtime and uses BCrypt rather than OpenSSL.

## Host

The Host launches the native server from its own directory. Point it at a
package directory; it will refuse a directory that has no native server
executable in it.

## Firewall

| Purpose | Default | Protocol | Exposure |
| --- | --- | --- | --- |
| Gameplay (GNS) | 7777 | UDP | public |
| LAN discovery | 7778 | UDP | LAN only, optional |
| Admin control | 7779 | TCP | localhost only |

Disable discovery with `--no-lan-discovery`, or move it with
`--lan-discovery-port`, when a host runs more than one server. Never expose the
admin port beyond localhost.

## Identity and persistence

Protocol V2 identity is mandatory: `require_v2_identity` defaults to `true` and
validation rejects `false`. `--enable-persistence` therefore always runs with
validated identities. Resume credentials live for
`resume_token_lifetime_hours` (default 720); `resume_token_lifetime_seconds`
overrides it for testing. See [`../docs/server/IDENTITY.md`](../docs/server/IDENTITY.md)
and [`../docs/server/PERSISTENCE.md`](../docs/server/PERSISTENCE.md).
