# Commonwealth Online Native Dedicated Server

`server-cpp` is the production authoritative server for Commonwealth Online.

The runtime is C++23, shares engine-independent behavior with the F4SE client through `co-core`, and uses Valve GameNetworkingSockets for gameplay transport. The Avalonia Host remains a separate .NET process and launches the packaged native server directly.

## Packaged run

Linux:

```bash
./CommonwealthOnline.Server --config commonwealth-server.json
```

Windows:

```bat
CommonwealthOnline.Server.exe --config commonwealth-server.json
```

The file `CommonwealthOnline.Server.native` beside the executable marks a native package. The Host uses that marker to omit the legacy managed `serve` verb.

## Production transport

Gameplay is GNS/UDP only. The shipped F4SE client routes its socket-shaped networking implementation through `F4TGnsWinsockShim`; the native server listens through `co_gns_server_create`.

TCP gameplay is not part of the native production contract and is not being reimplemented solely to preserve the retired managed acceptance harness.

## Default ports

- UDP 7777: GNS gameplay
- UDP 7778: LAN discovery
- TCP 127.0.0.1:7779: authenticated local administration

The game and admin port numbers must differ in configuration.

## Configuration

Native packages ship this schema:

```json
{
  "host": "0.0.0.0",
  "port": 7777,
  "admin_port": 7779,
  "server_name": "Commonwealth Online Server",
  "server_description": "Commonwealth Online server",
  "max_players": 16,
  "require_v2_identity": true,
  "enable_persistence": true,
  "enable_chat": true,
  "enable_factions": true,
  "friendly_fire": true,
  "faction_friendly_fire": false,
  "durable_state_path": "durable-state.json",
  "required_client_version": null,
  "required_fallout_runtime_version": null,
  "required_content_manifest_fingerprint": null,
  "required_capabilities": [],
  "admin_profiles": [],
  "resume_token_lifetime_hours": 720
}
```

`max_players` must be 1-16: the supported session size is the local player plus 15 remote players. `enable_persistence` (default `true`) requires Protocol V2 identity; the packaged file ships it on. Relative durable-state paths resolve beside the configuration file. `enable_chat` (default `true`) turns on global text chat; set it to `false` to reject chat packets like unknown types. The packaged file ships it on.

`enable_factions` (default `true`) turns on player factions and advertises the `factions-v1` capability. The packaged file ships it on. Factions are stored under the `factions` key of the durable state, so they persist only when `enable_persistence` is also true; with persistence off the server logs a warning and factions reset on restart.

`enable_object_state` (default `true`) turns on shared world objects and advertises the `object-state-v1` capability; set it to `false` to reject object-state packets.

`friendly_fire` (default `true`) allows players to damage each other. When it is `false` the server drops every player-versus-player `combatHit` instead of routing it to the victim, so no damage is applied. NPC damage, NPC combat, and environmental damage are unaffected. The current value is sent to every client in the `welcome` packet as `friendlyFire` and is reported by the admin `status` command as `friendly_fire`.

`faction_friendly_fire` (default `false`) controls player-versus-player damage between members of the same faction. When it is `false` the server drops a `combatHit` whose attacker and victim are in the same faction, so no damage is applied; players in different factions or in no faction are unaffected. It has no effect while `friendly_fire` is `false`, which already drops every player-versus-player hit. It only applies when `enable_factions` is on. NPC damage, NPC combat, and environmental damage are unaffected. The value is sent to every client in the `welcome` packet as `factionFriendlyFire` and is reported by the admin `status` command as `faction_friendly_fire`.

`server_password` (optional, unset by default) requires every joining client to send the same password. Use 4-64 printable ASCII characters. The server keeps only a SHA-256 digest of it, and advertises `passworded` in the public listing and LAN discovery.

`factions` (optional object) sets the faction limits. Every key is optional and defaults to the value shown: `max_factions` 512 (1-4096), `max_members` 16 (1-64), `invite_lifetime_seconds` 60 (1-3600), `invite_window_seconds` 60 (1-3600), `invites_per_window` 8 (1-1000), `pending_invites` 256 (1-65536), `pending_invites_per_target` 16 (1-1024), `roster_request_interval_seconds` 1, `list_request_interval_seconds` 1, `name_roster_interval_seconds` 1 (each 0-60), `mutation_capacity` 4 (1-1000), `mutation_refill_per_second` 1 (above 0, at most 1000). Out-of-range values fail startup.

`downed` (optional object) sets the downed and revive policy: `seconds` 60 (the top-level `downed_seconds` key is still accepted when this is unset), `revive_channel_seconds` 3 (top-level `revive_channel_seconds` also accepted), `revive_health_fraction` 0.5 (above 0, at most 1), `revive_range_meters` 3 (above 0, at most 50), `respawn_grace_seconds` 8 (0-600), `stimpaks_per_revive` 1 (0-99).

`questStoryOrderPath` (optional, unset by default) names a quest story order JSON file, resolved beside the configuration file. When set it replaces the built-in order and must pass strict loading (no duplicate quests, no cycles, no dangling requires); an unreadable or invalid file stops startup. `quest-story-order.sample.json` ships next to this file and matches the built-in order.

`steam_friends_only` (default `true`) applies when a player hosts this server through Steam: only the host's Steam friends can connect. Set it to `false` together with `server_password` to also let non-friends join with the password. Without `server_password` it stays friends-only. Wrong passwords lock that Steam ID out for 5 minutes after 5 tries. See `docs/server/STEAM_IDENTITY.md` (Steam hosting admission).
`modpack_name` (optional, unset by default; also read as `modpackName`) is the display name of the modlist or Nexus collection players need, up to 80 bytes of printable ASCII, no < or >. It requires `required_modpack_path`; the server refuses to start with a name and no modpack. When a client is blocked for a modpack mismatch it is shown "This server requires the <name> modlist.". The server sends no mod files unless `allow_modpack_downloads` is on.

`allow_modpack_downloads` (default `false`; also read as `allowModpackDownloads`) lets a blocked player download the plugins and plugin archives they are missing from this server, after they agree in game. It requires `required_modpack_path`, `modpack_download_dir` and `modpack_downloadable`. Only plugins (`.esp`, `.esm`, `.esl`) and the archives Fallout 4 loads next to them (`<plugin> - Main.ba2`, `- Textures.ba2`, `- Voices_<language>.ba2`, `- Geometry.psg`, `- Geometry.csg`, `<plugin>.cdx`) can be offered. Official, Creation Club and Commonwealth Online files, F4SE plugins, inis and anything outside `Data` are never sent.
`modpack_download_dir` is the folder the files are served from, relative to the configuration file. A file is served only if it is listed in the modpack manifest with a SHA-256 and its bytes match that hash; the server hashes the folder once at start and logs every file it will not serve.
`modpack_downloadable` lists the file names you have permission to redistribute, or `["*"]` for every matching file in the folder. Most mods' licences do not allow redistribution, so list only files you may share.
`modpack_download_concurrency` (default 4, 1-16) caps simultaneous downloads, and `modpack_download_bytes_per_hour_per_address` (default and maximum 42949672960, which is 40 GiB) caps what one address can download per hour.

`admin_profiles` (default empty) lists the profile ids that are admins. Admins may open the Fallout 4 console while connected; every other player is blocked from the console for the whole session. The server tells each client its own answer in the `sessionReady` packet, so a client cannot grant itself the console. Each entry is one profile id with no `:` form, no duplicates and at most 64 entries. With the list empty nobody gets the console in multiplayer.

The entry grants the console to whoever connects under that profile id, and the identity gate is what keeps it to that player: after a profile's first join the gate requires the profile's server-issued resume credential in the `hello`, so a second client cannot present an already-used profile id. The very first join of a profile id that has never connected is trust-on-first-use, the same as the rest of the identity model; revoke it with the admin `identity_reset` command for that profile id.

A role is only ever applied to a session the identity gate bound a profile to. Protocol V2 identity is mandatory and `require_v2_identity` can no longer be set to `false`, and a `hello` without a profile id is rejected outright, so every connected player has a profile id for the list to match against.

The admin token is stored in `.admin-token` beside the configuration file. On a drive without ACL support (FAT32, exFAT), the secret files (the admin token, the save attestation key, the Steam host link token and the Steam Web API key) are written without the owner-only DACL, so any local user can read them, and the server prints a warning. If the token cannot be loaded or created, the server keeps running with admin control off. Bans and other server-owned state use the same configuration directory as their persistence root.

## Native build and tests

Linux native regression suite:

```bash
cd CO-SERVER/server-cpp
xmake f -y -m debug
xmake test -y
```

The suite covers protocol validation, interest, replay, world/combat, identity, persistence, admin, shared-core parity, and 16/32/64-session native load behavior.

The live GNS acceptance lane additionally covers:

- handshake/session activation
- named player-state behavior
- NPC filtering and late-join replay
- movement correction
- packet loss/reordering
- malformed and oversized traffic
- packet-rate enforcement
- NPC authority churn
- maximum-player admission

Windows x64 is cross-built with clang-cl/xwin. The release package uses static MSVC runtime linkage and BCrypt instead of OpenSSL.

## Packaging

Canonical package, release-acceptance, and publish workflows all use:

```bash
CO-SERVER/server-cpp/scripts/build-native-package-ci.sh
```

The package composer is:

```bash
CO-SERVER/server-cpp/scripts/package-native-release.sh
```

Every package is checked by `verify-native-package.sh`. It rejects mixed managed/native layouts, validates the package marker and native configuration, checks relocatable Linux bridge resolution, and audits Windows PE/import dependencies.

Linux runtime requirements are documented inside each package in `NATIVE_RUNTIME.txt`.

## Host contract

The Avalonia Host remains managed UI only. In a native package it:

- detects `CommonwealthOnline.Server.native`
- launches the native executable without `serve`
- passes `--config <path>`
- treats GNS as the fixed production transport
- saves the native configuration schema without reintroducing managed-only transport fields

Host launch/config behavior is covered by `CO-SERVER/host-tests` and clean-package smoke tests.

## Legacy managed server

`CO-SERVER/server/` remains temporarily as a compatibility/reference tree while the native package and acceptance branches complete runner validation. It is not the production runtime or release-package source.

After native gates are green, managed-only runtime and acceptance code can be removed without porting its TCP transport.
