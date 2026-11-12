# Deployment

Releases ship a native package: the C++ authoritative server, the GNS bridge
beside it, the Avalonia Host, a config file, and a `CommonwealthOnline.Server.native`
marker. Nothing in the package needs a .NET runtime to run the server itself.

Before upgrading a server to durable-state schema 6, stop it and take a private
backup of its durable state and configuration. Older server versions refuse
schema 6. Restoring a pre-upgrade backup permits rollback but discards progress
made since that backup. Test the backup before upgrading.

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
Linux package. The example uses `UMask=0077` so files newly created by the
service are private to its user by default. This does not change existing file
permissions or protect files copied with broader permissions. Durable state and
its `.bak` and temporary files can contain profile secrets and resume credential
hashes; restrict access to the state directory and every backup before starting
the server. On Windows, use a private directory with an appropriate inherited
ACL. Containers do not use the systemd setting: restrict their state mounts and
backup destinations separately.

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

Death bags can be looted by other characters only when `allow_death_bag_loot_by_others` is `true`; the default is `false`.

Death bag placement recovery: every new bag is placement-requested at deposit (`CreateAndDepositDeathBag`). If the client crashes after the server has deposited a bag, its next login reports the bag as unplaced (`deathBagUnplaced`) and the server refunds the bag's items once; a repeated report finds the bag gone and refunds nothing. If no placement result arrives within 24 hours (`PlaceResultBoundSeconds`, fixed, not configurable), the bag is archived as `placeFailedUnverified` for administrator recovery (`death_bag_archive_list`, then `death_bag_archive_resolve`). It is not refunded automatically; this 24-hour hold is the backstop. The 120-second crash refund (`CrashRefundSeconds`, measured from departure or creation) applies only to a deposited bag that was never placement-requested, which is the case for legacy or migrated bags.

Death bag despawn: a bag is removed as soon as its last item is withdrawn, and the empty container is never kept. The character's death generation then carries a `bagSpent` marker, so a replayed deposit cannot recreate the bag and a replayed last withdraw still returns its original grant. A placed bag that is already empty (legacy) is retired by the next expiry sweep. The marker clears when the generation is retired (revive or respawn), and the next death creates a new bag with a new death id.
