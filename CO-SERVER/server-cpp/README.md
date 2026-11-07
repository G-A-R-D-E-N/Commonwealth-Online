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
  "max_players": 100,
  "require_v2_identity": true,
  "enable_persistence": true,
  "enable_chat": true,
  "enable_factions": true,
  "friendly_fire": true,
  "faction_friendly_fire": false,
  "durable_state_path": "durable-state.json",
  "required_client_version": "2.1.0",
  "required_fallout_runtime_version": null,
  "required_content_manifest_fingerprint": null,
  "required_capabilities": [],
  "admin_profiles": [],
  "resume_token_lifetime_hours": 720
}
```

The inbound budget limits what one client connection may send, counted per second. `inbound_packets_per_second` (10-512, default `120`) caps gameplay packets; `inbound_bytes_per_second` (65536-67108864, default `262144`) caps their decoded bytes after decompression; `inbound_violation_windows` (1-60, default `3`) is how many over-budget seconds a connection may have before strikes start. Each strike is logged with the bytes by packet type for that second. The first `inbound_budget` strike also sends the player a `Server` chat line, and the second kicks. Voice datagrams have their own budget. A normal client stays well inside the defaults; raise them only when the logs show a legitimate client over budget.

`max_players` must be 1-100: the supported session size is up to 100 players (`F4T::RemotePlayerCapacity::MaximumSessionPlayers`). `enable_persistence` (default `true`) requires Protocol V2 identity; the packaged file ships it on. Relative durable-state paths resolve beside the configuration file. `enable_chat` (default `true`) turns on global text chat; set it to `false` to reject chat packets like unknown types. The packaged file ships it on.

`inventory_gain_mode` (`off`, `warn` or `enforce`; default `warn`) limits how fast a player's inventory snapshot can add items. `enforce` refuses an over-budget snapshot as `gainLimited`, `warn` logs it and accepts it, and `off` skips the check. Loot and quest grants are not credited to the budget, so `enforce` can refuse legitimate gains. Known limit: in `enforce` a single unexplained gain above the burst (for example a veteran's first full snapshot after offline play) is rejected for as long as the diff exceeds the burst, so keep `warn` until measured.

`required_client_version` (default: the current client version, `2.1.0`) is the exact client version the server accepts. Leave it out to use the current version. The Host always enforces the current version while its field is blank. `null` or an empty string turns the check off only in a hand-written native JSON file; the Host drops them on save.

`enable_factions` (default `true`) turns on player factions and advertises the `factions-v1` capability. The packaged file ships it on. Factions are stored under the `factions` key of the durable state, so they persist only when `enable_persistence` is also true; with persistence off the server logs a warning and factions reset on restart.

`enable_object_state` (default `true`) turns on shared world objects and advertises the `object-state-v1` and `object-state-v2` capabilities (v2 gates terminal lock state); set it to `false` to reject object-state packets.

`friendly_fire` (default `true`) allows players to damage each other. When it is `false` the server drops every player-versus-player `combatHit` instead of routing it to the victim, so no damage is applied. NPC damage, NPC combat, and environmental damage are unaffected. The current value is sent to every client in the `welcome` packet as `friendlyFire` and is reported by the admin `status` command as `friendly_fire`.

`faction_friendly_fire` (default `false`) controls player-versus-player damage between members of the same faction. When it is `false` the server drops a `combatHit` whose attacker and victim are in the same faction, so no damage is applied; players in different factions or in no faction are unaffected. It has no effect while `friendly_fire` is `false`, which already drops every player-versus-player hit. It only applies when `enable_factions` is on. NPC damage, NPC combat, and environmental damage are unaffected. The value is sent to every client in the `welcome` packet as `factionFriendlyFire` and is reported by the admin `status` command as `faction_friendly_fire`.

`server_password` (optional, unset by default) requires every joining client to send the same password. Use 4-64 printable ASCII characters. The server keeps only a SHA-256 digest of it, and advertises `passworded` in the public listing and LAN discovery.

`factions` (optional object) sets the faction limits. Every key is optional and defaults to the value shown: `max_factions` 512 (1-4096), `max_members` 16 (1-64), `invite_lifetime_seconds` 60 (1-3600), `invite_window_seconds` 60 (1-3600), `invites_per_window` 8 (1-1000), `pending_invites` 256 (1-65536), `pending_invites_per_target` 16 (1-1024), `roster_request_interval_seconds` 1, `list_request_interval_seconds` 1, `name_roster_interval_seconds` 1 (each 0-60), `mutation_capacity` 4 (1-1000), `mutation_refill_per_second` 1 (above 0, at most 1000). Out-of-range values fail startup.

`downed` (optional object) sets the downed and revive policy: `seconds` 60 (the top-level `downed_seconds` key is still accepted when this is unset), `revive_channel_seconds` 3 (top-level `revive_channel_seconds` also accepted), `revive_health_fraction` 0.5 (above 0, at most 1), `revive_range_meters` 3 (above 0, at most 50), `respawn_grace_seconds` 8 (0-600), `stimpaks_per_revive` 1 (0-99).

`questStoryOrderPath` (optional, unset by default) names a quest story order JSON file, resolved beside the configuration file. When set it replaces the built-in order and must pass strict loading (no duplicate quests, no cycles, no dangling requires); an unreadable or invalid file stops startup. `quest-story-order.sample.json` ships next to this file and matches the built-in order.

`steam_friends_only` (default `true`) applies when a player hosts this server through Steam: only the host's Steam friends can connect. Set it to `false` together with `server_password` to also let non-friends join with the password. Without `server_password` it stays friends-only. Wrong passwords lock that Steam ID out for 5 minutes after 5 tries. See `docs/server/STEAM_IDENTITY.md` (Steam hosting admission).
`modpack_name` (optional, unset by default; also read as `modpackName`) is the display name of the modlist or Nexus collection players need, up to 80 bytes of printable ASCII, no < or >. It requires a manifest or an MO2 profile; the server refuses to start with a name and no modpack. When a client is blocked for a modpack mismatch it is shown "This server requires the <name> modlist.". The server sends no mod files unless `allow_modpack_downloads` is on.

Set `required_mo2_instance` to a Mod Organizer 2 instance folder and `required_mo2_profile` to its profile name to build a required manifest at server startup. The instance, its enabled mods, and Fallout 4's Data folder must be readable by the server. Relative instance paths are resolved beside the configuration file. The launcher can fill both fields from its selected active MO2 profile. Use either this pair or `required_modpack_path`, not both. Restart the server after changing the profile. The manifest checks enabled plugins and their order, plus winning archives, F4SE plugins, INI files, and other loose files from enabled mods and overwrite. MO2's default and supported custom virtual filesystem exclusions are applied. Files already present only in the physical Data folder are not inventoried except for enabled plugins. Extra loose files on a client are not detected. Profiles exceeding 32,768 entries or the 8 MiB manifest limit fail rather than producing a partial manifest. Players need a client that supports the MO2 manifest capability.

The server excludes top-level `Root` and `server` folders, which can contain game-root files and machine-specific server state. It validates and hashes MO2 content at startup, not when settings are opened or saved. A missing or invalid profile prevents startup. MO2-backed servers require a writable configuration directory and only one MO2-backed server can use it at a time, including while the profile is being read; leave `.commonwealth-server.lock` in place while the server is running. The lock file remains after shutdown and does not itself mean a server is running. The launcher appends to `dedicated-console.log` across starts, so archive or clear it when the server is stopped. Keep the profile and its files unchanged while the server reads them. The server checks file sizes and modification times for ordinary concurrent edits, but a same-size rewrite with an unchanged timestamp can evade that check.

MO2 filename comparisons use a table generated from `CharLowerBuffW` on Windows 11 build 26200 with the en-US locale; `scripts/generate-windows-case-table.py` reproduces it on that setup. Windows checks the table against its installed mapping and rejects names that differ. Linux uses the pinned table, which may differ from the Windows installation that produced the profile.

`allow_modpack_downloads` (default `false`; also read as `allowModpackDownloads`) lets a blocked player download the plugins and plugin archives they are missing from this server, after they agree in game. It requires a manifest or an MO2 profile, `modpack_download_dir` and `modpack_downloadable`. Only plugins (`.esp`, `.esm`, `.esl`) and the archives Fallout 4 loads next to them (`<plugin> - Main.ba2`, `- Textures.ba2`, `- Voices_<language>.ba2`, `- Geometry.psg`, `- Geometry.csg`, `<plugin>.cdx`) can be offered. Official, Creation Club and Commonwealth Online files, F4SE plugins, inis and anything outside `Data` are never sent.
`modpack_download_dir` is the folder the files are served from, relative to the configuration file. A file is served only if it is listed in the modpack manifest with a SHA-256 and its bytes match that hash; the server hashes the folder once at start and logs every file it will not serve.
`modpack_downloadable` lists the file names you have permission to redistribute, or `["*"]` for every matching file in the folder. Most mods' licences do not allow redistribution, so list only files you may share.

`nexus_sources` maps required installed file names to their Nexus mod and file IDs. For example:

```json
"nexus_sources": {
  "version": 1,
  "files": [
    { "name": "Example.esp", "modId": 123, "fileId": 456 },
    { "name": "Example - Main.ba2", "modId": 123, "fileId": 456 }
  ]
}
```

Each name must occur in the required modpack manifest with a SHA-256 hash. Players can open the identified Nexus file page and choose Mod Manager Download for MO2 or Vortex. These mappings do not change the modpack fingerprint and do not grant redistribution permission.

For a Vortex collection, configure `"nexus_collection": { "slug": "abc123", "revision": 3 }` alongside a required manifest with SHA-256 hashes for every entry. Players can open that pinned collection revision and install and deploy it with Vortex; the manifest still determines whether their installed files match the server.
`modpack_download_concurrency` (default 4, 1-16) caps simultaneous downloads, and `modpack_download_bytes_per_hour_per_address` (default and maximum 42949672960, which is 40 GiB) caps what one address can download per hour.

`admin_profiles` (default empty) lists the profile ids that are admins. Admins may open the Fallout 4 console while connected; every other player is blocked from the console for the whole session. The server tells each client its own answer in the `sessionReady` packet, so a client cannot grant itself the console. Each entry is one profile id with no `:` form, no duplicates and at most 64 entries. With the list empty nobody gets the console in multiplayer.

The entry grants the console to whoever connects under that profile id, and the identity gate is what keeps it to that player: after a profile's first join the gate requires the profile's server-issued resume credential in the `hello`, so a second client cannot present an already-used profile id. The very first join of a profile id that has never connected is trust-on-first-use, the same as the rest of the identity model; revoke it with the admin `identity_reset` command for that profile id.

Admins can also be added while the server runs, with the `admin_grant` admin command or the Make admin button in the Launcher and the in-game admin menu. Granted admins are saved in `admins.json` next to the other server data and survive restarts; `admin_revoke` removes them. Entries from `admin_profiles` can only be changed in the config file. The host of a Steam P2P session is an admin automatically for that session and is not saved.

`require_known_profile` (default `false`; also read as `requireKnownProfile`) refuses any profile that has never joined this server. Leave it off unless you need that: with it on, every first-time player is refused, and there is no administrator allow-list yet, so only profiles that have already connected get in. A player whose first session never finished gets in by presenting the token the server issued; one with no token stays refused until the flag is off.

`require_steam_identity` (default `false`; also read as `requireSteamIdentity`) refuses any player without a verified Steam account, with the `steam_required` code. Steam P2P connections pass at once; other players must send a Steam ticket that the Steam Web API confirms, and a player whose check is still running is held for up to 30 seconds. The server exits with code 2 at startup when this is on and no Steam Web API key that Steam accepts is configured.

A role is only ever applied to a session the identity gate bound a profile to. Protocol V2 identity is mandatory and `require_v2_identity` can no longer be set to `false`, and a `hello` without a profile id is rejected outright, so every connected player has a profile id for the list to match against.

The admin token is stored in `.admin-token` beside the configuration file. On a drive without ACL support (FAT32, exFAT), the secret files (the admin token, the save attestation key, the Steam host link token and the Steam Web API key) are written without the owner-only DACL, so any local user can read them, and the server prints a warning. If the token cannot be loaded or created, the server keeps running with admin control off. Bans and other server-owned state use the same configuration directory as their persistence root.

## Starting inventory recovery

Starting inventory remains blocked after an interrupted or uncertain delivery. A game save and an F4SE cosave are not treated as an atomic receipt. The player should disconnect, preserve the relevant saves, and contact the server operator. Do not retry the grant or approve a save until the actual inventory and server ledger have been reconciled; a partial delivery needs manual correction before approval.

With the player disconnected, send newline-delimited JSON to the configured loopback admin port, adding the `adminToken` from `.admin-token` as described above. Use `{"cmd":"starting-inventory-inspect","profileId":"PROFILE"}` to obtain the server instance, grant record, and current inventory and revision. Compare those items and quantities with the intended character's inventory in the chosen FOS. Investigate missing forms and partial grants; keep recovery blocked if the inventories cannot be reconciled. If delivery has not started, manually reconcile the starting items before resolving the record. Preserve copies of the state and saves before manual corrections.

Once the actual character inventory matches the server inventory, create a stable FOS for that character under a new filename and calculate its SHA-256. A reused filename with different contents can be rejected by save attestation before recovery reaches the client. The `saveFile` value is its filename without `.fos`. Send `{"cmd":"starting-inventory-resolve","profileId":"PROFILE","characterId":"CHARACTER","saveFile":"SAVE_NAME","sha256":"LOWERCASE_SHA256","note":"what was checked and corrected","legacy":false,"confirm":"inventory-verified"}` with the admin token. Use `legacy:true` only when inspection shows a grant with no `recovery` record. The server records the approval against the current inventory revision and consumes it durably on first verification. Every later connection requires a fresh operator reconciliation, even if inventory did not change. The player must reconnect with the approved save loaded. The client will not automatically switch saves. If it is unavailable, modified, older, or the inventory revision has changed, repeat the comparison and reconciliation; never assume an ACK or cosave proves the FOS contents.

For an interrupted first join that never bound a save, and whose character never began its starting grant (grant `unstarted`, inventory revision 0), the player's client asks the server to discard that character automatically on its next join. If that refuses (the character has starting inventory or progress recorded by a grant that began), reconcile it with `starting-inventory-inspect`. If the automatic path cannot reach the character, send `{"cmd":"first-join-abandon","profileId":"ACCOUNT#SLOT","confirm":"discard-unbegun-first-join"}` with the admin token. The command refuses unless the profile is disconnected, its grant never began and its inventory revision is 0. Progress, appearance and equipped items on that character are discarded with it, so confirm with the player first. When the character already has starting inventory or inventory changes but the player never got a save for it, send the same command with `"confirm":"discard-first-join-without-save"`. That form discards the character whatever its grant and inventory state, and refuses when the starting grant names a character that has a recorded save on the account, when it is the active character, or when the profile is connected. Save records are kept per account, so the server can link a slot to a save only through its starting grant. Confirm with the player that they have no save for the character first.

For a legacy save-less hold with no slot, other characters on the account can prevent automatic recovery after the discard. First verify with the player which slot belongs to the interrupted character; do not guess or discard other characters. If it has not already been discarded, run `first-join-abandon` for that slot and confirm it succeeded. Then have the player close Fallout 4 and back up `Documents\My Games\Fallout4\CommonwealthOnline\server-profiles.json` without sharing it, since it contains private profile information. In the original file, locate the affected server under `servers` and add `"slot": N` to its existing `firstJoinHold` object, replacing `N` with the verified numeric slot. Leave all other fields intact. The player can then start the game and reconnect: the server checks that specific slot, and the client clears the hold only after the server confirms the slot is empty. If the slot cannot be verified, keep the hold and saves for manual investigation.
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
