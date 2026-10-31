# Commonwealth Online comparison changelog

## Unreleased

- Runtime contract: OG `1.10.163`, NG `1.10.984`, and AE `1.11.240` only. Each DLL refuses to load on the other runtimes.
- Sessions support 16 players (15 visible remote players per client). The tested cap is pending in-game acceptance.
- Ranged hits are reported only when exactly one recent attack matches; ambiguous overlapping shots are dropped. The exact per-projectile match is not active in this release.
- Tested runtimes and executables are listed in `docs/release/acceptance-matrix.md`.
- The GNS bridge is loaded from explicit candidate paths (module directory, `Data/F4SE/Plugins`, current directory) before a bare-name fallback.
- The native C++ server replaces the Python relay.

## TL;DR

Matrix replaces the older monolithic Fallout 4 project with shared contracts, a native C++ dedicated server, GNS transport, isolated AE/NG/OG packages, centralized UI ownership, and stronger validation and CI. PvP is enabled and hardened; NPC synchronization remains disabled. Campaign co-op and full world synchronization are still out of scope.

This document compares the older public Commonwealth Online repository with the
current Commonwealth Online Matrix repository. It is a comparison changelog,
not a replacement for the detailed client history in
[`CO-BETA/changelog.md`](CO-BETA/changelog.md).

## Comparison basis

| Baseline | Repository and revision |
| --- | --- |
| Older public version | [`Commonwealth-Online-Public`](https://git.zambazosmedia.group/Commonwealth-Online/Commonwealth-Online-Public), `main` at `06c77b96b218c0d47682102e4ae23e24c00985f9` (2026-07-26) |
| Current version | [`CommonwealthOnline-Matrix`](https://github.com/G-A-R-D-E-N/CommonwealthOnline-Matrix), current `main` (verify the exact SHA before relying on this table) |

The old snapshot is compared as it existed at its recorded commit. Its README,
changelog, and implementation are not always synchronized, so this document
distinguishes advertised status from behavior and structure confirmed in the
current tree. Vendored libraries, generated binaries, and raw art assets are
summarized as repository changes rather than listed individually.

## Executive summary

The older repository was a single Fallout 4 project organized around one F4SE
plugin, a Python relay server, an in-repository UI, and one primary runtime.
The current repository is a Matrix monorepo with four explicit ownership areas:

- `co-core/` contains engine-independent protocol, identity, validation,
  snapshot, persistence, authority, and compatibility logic.
- `CO-SERVER/` contains the native C++ dedicated server, GNS bridge, native
  acceptance clients, persistence, admission, admin control, and Avalonia host.
- `CO-BETA/` contains the Fallout 4 plugin, Creation Kit content, Papyrus,
  client integration, and runtime-specific packaging.
- `CommonwealthOnlineUI/` is the source of truth for the UI plugin and Prisma
  HTML/CSS/JavaScript views.

The project is still a dedicated-server presence foundation rather than a full
campaign co-op product. The current tree adds substantially stronger shared
contracts, runtime isolation, native transport, server authority, and CI
proof, while keeping NPC synchronization disabled in all shipping plugin
targets.

## Added in Matrix

### Shared core and authoritative contracts

The current tree adds `co-core/`, which is built without Fallout or F4SE. It
centralizes the logic that must agree between clients, servers, and tests:

- Protocol V2 identity and client-compatibility metadata.
- GNS protocol adaptation and snapshot envelope handling.
- Transform, appearance, action-event, network, player-state, and authoritative
  packet validation.
- Connection policy, packet rate limiting, interest scope, peer entity sets,
  snapshot interpolation/delta handling, and server world state.
- Durable profile/resume state, profile bans, authority election, and F4SE
  capability policy.
- Engine-independent regression tests for the shared behavior.

This removes the older pattern in which client, Python server, and tests could
each carry a slightly different copy of the same validation rule.

### Native dedicated server and transport

The current server product is native C++ in `CO-SERVER/server-cpp/`, with a
native GameNetworkingSockets bridge in `CO-SERVER/server/native_transport/`.
The current runtime path is GNS over UDP on port `7777`, with UDP LAN discovery
on `7778` and localhost-only admin control on `7779`.

The server now includes explicit components for:

- Admission limits and validated gameplay authority.
- Identity recovery, durable state, server profiles, and profile bans.
- LAN discovery, packet routing, snapshot replay, and world-state authority.
- Admin commands and a separate Avalonia host GUI.
- Native live acceptance and network-fault test clients.

### Runtime-specific client and package support

The current client build produces isolated packages for exactly these targets:

- AE: Fallout 4 `1.11.240` — `CommonwealthOnline.dll`.
- NG: Fallout 4 `1.10.984` — `CommonwealthOnline-NG.dll`.
- OG: Fallout 4 `1.10.163` — `CommonwealthOnline-OG.dll`.

Each target selects its runtime at build time and declines to load on the wrong
runtime. Package staging, plugin entry points, content manifests, and clean-copy
smoke tests are now checked independently for the three versions.

### Acceptance, audit, and build infrastructure

The current repository adds root-level CI and source contracts covering:

- Shared-core C++ tests, native Linux tests, sanitizer lanes, and Windows
  cross-builds.
- Native GNS bridge and live protocol acceptance.
- Plugin DLL packaging and F4SE entry-point checks.
- Strict AE/NG/OG package isolation and content-manifest fingerprints.
- Repository audit regressions, source contracts, shell parsing, and UI checks.
- Reproducible build, staging, deployment, and submodule validation.

### Operational hardening

Compared with the older baseline, the current implementation adds or formalizes
bounded and fail-closed handling for malformed traffic, connection admission,
packet sizes, numeric ranges, action events, discovery metadata, persistence
writes, log rotation, admin control, and profile/resume recovery.

## Changed

### Repository ownership and layout

| Older public tree | Current Matrix tree |
| --- | --- |
| `plugin/` | `CO-BETA/plugin/` |
| `server/` Python relay and GUI | `CO-SERVER/server-cpp/`, `CO-SERVER/server/native_transport/`, and `CO-SERVER/host/` |
| `protocol/` | `co-core/`, `docs/server/`, and `CO-BETA/docs/protocol/` |
| `creation-kit/` | `CO-BETA/creation-kit/` |
| `ui/` and plugin-owned UI sources | `CommonwealthOnlineUI/` plus the `CO-BETA/ThirdParty/CommonwealthOnlineUI` release dependency |
| root-level build/deploy scripts | `CO-BETA/` build/deploy scripts and shared CI workflows under `.github/workflows/` |

The current root README now defines which tree owns each type of change. This
prevents a second implementation of a subsystem from silently appearing in a
different component.

### Transport and server lifecycle

The older README described TCP JSON on `7777` through `server/server.py` and
Python LAN discovery. The current canonical production path is native C++ with
GNS/UDP, while LAN discovery remains UDP-based. The old Python relay is absent
from the current tracked Matrix tree and is not the canonical production server
path.

The managed C# server path is likewise retired from the canonical server
product. The Host launches the packaged native server rather than building a
managed server from source.

### UI ownership and build flow

The older tree stored Prisma HTML/CSS/JavaScript views and several UI bridge
sources in the main repository. The current tree moves UI ownership to
`CommonwealthOnlineUI/`. The client package consumes the built UI dependency;
the Matrix client tree owns the integration boundary, not a second copy of the
views.

### Identity, persistence, and bans

The current system treats `profileId` and resume credentials as durable
continuity while `playerId` remains session-scoped. Profile bindings follow the
character and newest associated save rather than depending only on a save
filename. Durable replacement writes, recoverable backups, bounded resume
cache handling, profile bans, live teardown, and explicit identity/resume reset
controls are now part of the server contract.

### Player combat validation

The older public README described combat as out of scope, while its historical
changelog recorded experimental combat work. The current Matrix tree preserves
PvP as an enabled path and hardens its validated `combatHit` transport:

- Sender, target, self-hit, zero-identifier, and routing checks are shared with
  native behavioral tests.
- NaN, infinity, non-positive, and over-limit damage is rejected.
- Duplicate, stale, and wraparound sequence handling is shared with shipping
  queue state.
- The pending-hit queue is capped at 32 entries.
- Disconnect removes the sender's sequence state and pending hits.

This is protocol and runtime hardening; it is not a claim that complete Fallout
combat, quests, inventory, or campaign co-op synchronization is finished.

### NPC synchronization scope

The current tree contains engine-independent NPC authority contracts and keeps
the client integration behind an explicit build boundary. All three shipping
plugin targets define `F4T_ENABLE_NPC_SYNC=0`; disabled builds expose no-op NPC
sync behavior. NPC synchronization is therefore not enabled by this comparison
or by the current shipping package.

### Onboarding and Creation Kit content

The older snapshot carried a separate `CommonwealthOnline_Vault109.esp`,
`CO_Vault109StartQuestScript`, and extra `CoSync*.psc` sources. The current
content is consolidated around `CommonwealthOnline.esp`,
`COCharacterSetupQuestScript`, `CommonwealthOnlineDebug`, and the current
Vault 111 onboarding flow. Papyrus staging is restricted to the active scripts,
and stale `CoSync` compiled files are removed rather than packaged.

## Removed or retired

- The Python relay is no longer the canonical production server.
- The managed C# authoritative server is no longer the canonical production
  path.
- In-repository `ui/`, plugin-owned Prisma view sources, and the old UI build
  wrappers were removed from the Matrix ownership boundary.
- The separate Vault 109 ESP/start-quest package and obsolete character-setup
  path were removed in favor of the consolidated current content flow.
- Dead `CoSync.psc`, `CoSyncPlayer.psc`, `CoSyncQuest.psc`, and the stray
  compiled `test.pex` were removed from active content staging.
- Direct ownership of UI-only framework dependencies was removed from the
  client tree; those dependencies now belong to the UI component.

## Current status and limits

The current Matrix repository is materially more testable and operationally
controlled than the older public snapshot, but it remains pre-release. The
working product is dedicated-server multiplayer presence: remote proxy actors,
movement, appearance, apparel, partial weapon visuals, session lifecycle, and
server infrastructure.

The following remain outside the current complete product claim: campaign
co-op, quests, settlements, inventory/loot, VATS, companions, dialogue, full
world simulation, and shipping NPC synchronization. Live Fallout testing on
the supported AE, NG, and OG runtimes remains a separate acceptance gate from
source tests, package tests, and native server CI.
