# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

For testing notes, see [`docs/testing.md`](docs/testing.md). Milestone summaries, known issues and next steps are historical, in [`docs/archive/dev-log.md`](docs/archive/dev-log.md). Do not include testing content in this file.


## [Unreleased]

### Added
- Server-scoped profile bans backed by durable `banned_profiles` records, pre-reserve identity rejection, live-session teardown, `identity_reset`/`resume_reset` ban controls, `profile_unban`, and player-facing `profile_banned` alerts.

### Removed
- Dead `CoSync.psc`, `CoSyncPlayer.psc`, and `CoSyncQuest.psc` Papyrus sources that were excluded from compile and deploy.
- The decompiled vanilla interface reference tree (patched-SWF sources, exported ActionScript and translation dumps). It was read-only reference material and never a build input; the docs that pointed at it now say to extract those files locally from the game archives.

### Changed
- `compile-papyrus.bat` still deletes leftover `CoSync*.pex` from the compile output folder.
- `stage-mod.bat` stages only `COCharacterSetupQuestScript` and `CommonwealthOnlineDebug` Papyrus files, so stale `CoSync*.pex` cannot re-enter the package. Leftover `CoSync*.pex` on a game install is still removed by `deploy-staged-data.ps1`.
- Rebuilt `COCharacterSetupQuestScript.pex` and `CommonwealthOnlineDebug.pex` with the Creation Kit Papyrus compiler.
- Protocol V2 handshake rejections now surface actionable alerts: Retry uses the UI-owned last-join replay through the separate V2 interface, Get Update appears for identity/content/capability mismatches, and Direct Connect exposes Retry when a prior join exists. Structured rejection and normal join completion are serialized through one game-thread task while the V1 callback ABI remains unchanged.

## [2026.08.16] — Next-Gen + Old-Gen

First stable release cut from `main`. Ships both runtimes; each DLL declines to load on the wrong one, so keep both installed and the matching build activates.

### Highlights
- **Old-Gen (Fallout 4 1.10.163) support.** A dedicated `CommonwealthOnline-OG.dll` runs alongside the Next-Gen `CommonwealthOnline.dll`; install both and the game loads whichever matches your version.
- **Direct Connect no longer locks up** after a failed or cancelled join — the address/port field and the "Connecting…" overlay always clear now. Adds an on-screen **Cancel** button and a 15-second watchdog (Esc / controller B also cancel).
- **Server hardening** against malformed and abusive network traffic: out-of-range value rejection, connection-admission limits, combat-packet validation at ingress, and rate-limited LAN discovery.
- **Save profiles follow your character** rather than the save's filename, so renaming or reorganizing saves no longer loses your online profile.
- Verbose debug logging is on by default in this build (`CommonwealthOnline.log`) to aid troubleshooting.

### Added
- Beta builds now share `COMMONWEALTH_ONLINE_DEBUG` across the F4SE plugin, active Papyrus, Python server, Host GUI, and build/deployment scripts, with beta debug logging enabled by default and consistent `[CO DEBUG][Component]` output.
- Server beta debug logging can be inspected or changed at runtime with `commonwealth debug status`, `commonwealth debug on`, and `commonwealth debug off`, while Host GUI/server config persists the `debug_logging` startup setting.
- Added focused debug tracing for connection/session lifecycle, save association, world/NPC synchronization, proxy/animation behavior, combat, LAN discovery, persistence, admin/process management, character setup, and packaging/deployment.
- Compiled Papyrus is now committed alongside the sources: `CommonwealthOnlineDebug.pex` ships the beta debug script and `COCharacterSetupQuestScript.pex` is rebuilt against it, so deployment no longer needs a local Creation Kit to produce them.
- Continuous integration builds `CommonwealthOnline.dll` with real MSVC under wine and cross-builds `CommonwealthOnlineHost.exe` with clang-cl and the xwin SDK against Qt6 for MSVC, and runs the Python, C++, and shell/switch-parsing suites on every `beta` push.
- Commonwealth Online now ships a separate `CommonwealthOnline-OG.dll` for Fallout 4 1.10.163 alongside the existing `CommonwealthOnline.dll` for 1.11.240. Install only the build matching the game version; each declines to load on the other. Both are built from the same sources, with the engine addresses each runtime needs selected at build time rather than detected while the game runs, so the Next-Gen build is unchanged. Any engine address without a confirmed Old-Gen counterpart fails loudly and names itself in the log the first time it is used, instead of silently resolving to unrelated code.
- Engine knowledge that previously lived in source comments now has a home in `docs/wiki/`, covering animation sync, proxy lifecycle and movement, appearance sync, combat, networking, saves, and the Fallout 4 and CommonLibF4 behaviours that shaped each. Crash history and the constraints behind past fixes are recorded with their consequences rather than sitting next to the code they explain.

### Fixed
- Proxy pathing no longer reads results from the wrong engine functions. The vendored CommonLibF4 carried incorrect Address Library ids for `Actor::IsPathValid` and `Actor::IsPathing`, both of which resolved to unrelated code on the current game target, so the two proxy pathing checks that consume them were acting on meaningless values. The correct ids now come from the ReverseEng CommonLib pin (PR #199), so no local patch is needed (source:RE-Work/subsystems/commonwealth-online-re/commonlib-pin-pr199/README.md).
- Runtime character tint entries are now built through CommonLibF4's own `REX::EMPLACE_VTABLE`/`MEM_WRITE_ZERO` helpers with the allocation size and vtable taken from a single template argument, so the two can no longer drift apart, and the resolved vtable address is checked to land inside the game module before it is stamped into the object. Previously each entry type paired a hand-written `sizeof` with a separately resolved vtable pointer written directly to offset zero.
- The proxy puppet `Actor::Update` hook now verifies the target vtable slot before writing it and confirms the write afterwards, refusing to install (and saying why) instead of overwriting an unrelated virtual. Previously the only check was that the replaced slot was non-null, which a wrong slot index passes. The two motion-feedback virtuals are no longer reached through hand-written slot indices at all — they are called through their CommonLibF4 declarations, so the compiler supplies the indices.
- Beta debug logging now fully honors the global switch on high-frequency client paths, requires both the global switch and `log_verbosity=debug` for relay packet spam, preserves real Papyrus warnings, throttles hostile combat logging, rotates persistent Host GUI/server logs, bounds untrusted UI/network text, and keeps packaged server scripts working without repository-only debug helpers.
- Persistent server logging now reuses one file handle, closes it cleanly, continues appending when Windows temporarily blocks rotation, reports the rotation problem to live listeners, and retries later instead of silently losing future beta evidence.
- Server log rotation now moves the live log into a `.rotating` staging file before touching the backup chain, so a Windows lock on the live log leaves `.1` and `.2` intact; previously the cascade ran first and a blocked rename destroyed both retained generations. A staging file left behind by a crash is recovered on the next open, and a log directory deleted while the server runs is recreated instead of failing every subsequent write.
- Relay debug logging bounds and escapes JSON field names before recording them, so an oversized or newline-bearing key in a hostile packet cannot pad or forge a log record.
- `.gitmodules` no longer begins with a UTF-8 BOM, which prevented `git config -f .gitmodules` from reading the first submodule entry.
- Removed the orphaned `creation-kit/scripts/compiled/test.pex`, which was not produced by any source in the repository.
- Host GUI bind/admin/player-limit settings saved while a server is running remain pending until restart so live labels continue representing the active process, while beta debug logging is applied asynchronously to both the GUI and running relay without blocking the settings window on admin timeouts.
- Public relay connections now enforce `max_players`, bound inbound packet size, expire inactive pre-gameplay sockets, serialize per-client writes, and grant world/NPC host authority only after validated gameplay traffic.
- Relay transform/NPC packets now validate numeric ranges, form IDs, and allowlisted action events before forwarding; NPC snapshots include `sourceFormId` so clients match one exact local actor instead of suppressing every actor sharing a base.
- F4SE TCP sends now complete partial nonblocking writes safely, diagnostic state is synchronized across threads, and periodic keepalives preserve established sessions without granting authority.
- World-state host changes and disconnect resets now cross to the game thread through pending state instead of mutating Fallout state from the receive thread.
- `serverWorldState` no longer carries executable console text; validated time and weather values are applied through game state APIs, and network NPC state no longer feeds `player.placeatme` commands.
- Incoming PvP damage now uses a normally constructed `RE::HitData` object instead of treating zeroed raw storage as live C++ objects.
- Detailed per-player face/body writes to shared `COPlayerProxy` base forms are disabled to prevent same-sex remote players from overwriting each other's shared NPC appearance; sex-specific proxy bases remain active.
- Server profiles, bans, and server config writes now use crash-resistant replacement with recoverable backups; Host GUI config saves use `QSaveFile` and report write/commit failures.
- Host GUI stats polling now runs outside the Qt UI thread, pending settings no longer switch the live admin port before restart, and finished `QProcess` instances are released between restarts.
- LAN discovery validates untrusted metadata and uses a short bounded discovery window instead of a multi-second subnet TCP scan; discovery probe logging is debug-only.
- `fix-port.bat` / `fix-port.sh` now update only the configured game port without discarding other server settings, and full deployment tracks owned files so removed package files are cleaned on future deploys while legacy CoSync PEX files are removed on first managed deploy.
- Server save profiles now follow the newest save for their associated character across quicksave, autosave, manual-save, and exit-save rotation, and successful saves while connected refresh the profile without allowing another character's save to be claimed.
- Newest-save selection now ranks candidates by the timestamp embedded in the save filename instead of trusting the engine save list's ordering, so reconnecting can no longer bind a server profile to that character's oldest save (for example the `Vault111Cryo` opening save).
- Late-joining clients no longer apply synced proxy appearance/facegen in the same frames as the first long-range restore teleport (and no longer force a second restore after head refresh), which crashed when approaching a player already in the exterior world.
- Per-frame proxy smooth movement now null-checks the local player's parent cell before resolving slot actors.

## [1.0.0 - 1.0.2]

### Added
- Pause menu CURRENT SERVER panel shows live session info: server name, players online, max players, and ping (CommonwealthOnlineUI).
- CLI `commonwealth users` (and updated `clients`) prints an on-demand table of connected players with positions and movement state.
- CLI `commonwealth help` lists available server management commands.
- Interactive `commonwealth>` shell via `serve --interactive` (used by `start.bat` / `start.sh`) for help/users/kick/ban and related commands in the same window.
- Admin `clients` / `users` responses now include each client's last transform snapshot.

### Changed
- `start.bat` / `start.sh` launch the server with `--interactive` so operators type commands directly instead of separate `python ...` invocations.
- Interactive `users` / `clients` live-refresh the player table every second (Enter to stop); `users once` prints a single snapshot.
- Server `log_verbosity` is enforced: default `info` no longer prints per-packet transform/position/world/npc broadcast spam (use `debug` for that).
- Server treats TCP connect probes that never send a packet as silent short-lived sessions (no disconnect broadcast; connect log waits for first gameplay packet).
- Server browser Browse tab loads public listings from `https://commonwealth-online.com/servers/server.json` instead of mock data (CommonwealthOnlineUI).
- Browse refresh now TCP-probes each listed host for connect ping (ms).
- Server browser join: second click on a selected row joins (dblclick is unreliable under PrismaUI after list re-renders).
- Recent servers persist via C++ to `Documents/My Games/Fallout4/CommonwealthOnline/recent-servers.json` (fixes joins never reaching JS after the menu hides, and PrismaUI localStorage not surviving game restarts).
- `README.md` / `docs/testing.md` / `docs/dev-log.md` updated for the public directory Browse flow.

### Fixed
- Direct Connect and server-browser joins now allow up to three seconds for the initial TCP connection using a valid `timeval`, preventing high-latency clients—including Fallout 4 under Wine—from failing before reaching remote public servers.
- Server save profiles now wait for the first-time quicksave to complete before binding, and rotating exit-save lookup stays within the associated character, preventing local and public servers from exchanging character saves.
- Pause menu CURRENT SERVER panel no longer TCP-probes the game relay while already connected (was creating ghost players and disconnect spam) (CommonwealthOnlineUI).
- Host GUI forces a light Fusion palette and dark label colors so status-strip stats and dialog text stay readable under Windows dark mode (no more white-on-light text).
- Host GUI `deploy.bat` no longer overwrites Qt 6.11 DLLs with older 6.10.2 copies (fixes `QTimer::toDuration` entry-point errors on launch).
- Host GUI live client list and Kick/Ban controls now poll the localhost admin port and update the status strip; Kick/Ban/Bans actions sit in the clients header so they stay visible.
- Server browser keyboard shortcuts (Q/E tabs, R refresh, F favorite) no longer intercept typing in Direct Connect or search text fields, so domains like `servers.commonwealth-online.com` can be entered.
- Host GUI no longer fails with "Server directory not found" after a local build: CMake post-build and `deploy.bat` stage `server/` next to the exe, and `ServerProcess` walks parent folders to locate `consumer_server_cli.py`.

### Added
- Persistent IP ban list (`server/bans.json`) with kick/ban/unban APIs on the relay server.
- Localhost-only admin control port (`admin_port`, default `7779`) for live `kick` / `ban` / `unban` / `bans` / `clients` / `stats` JSON-lines commands.
- CLI commands `commonwealth kick`, `ban`, `unban`, and `bans` (plus live `status` / `clients` via the admin port).
- Directed `sessionEnded` protocol packet (`code` / optional `reason`) sent to kicked or banned clients before TCP close.
- Client ban/kick notification explaining that the user was disconnected; local single-player gameplay remains available.
- Host GUI Kick and Ban IP buttons on the connected clients table, wired through the admin port.
- Linux/macOS server launch helpers `server/start.sh` and `server/fix-port.sh` alongside the existing Windows `.bat` scripts.
- Host GUI **Settings** dialog to edit `server_name`, `server_description`, bind address, port, max players, and log level, saving to `commonwealth-server.json`.
- Host-configurable `server_description` in server config JSON (optional, max 256 characters).

### Changed
- Host GUI restyled as a light admin utility: toolbar Start/Stop/Settings, dense status strip, and a resizable clients/log splitter (replacing the Fallout amber game theme and emoji chrome).
- Host GUI CMake post-build and `deploy.bat` preserve an existing staged `commonwealth-server.json` so Settings dialog edits survive rebuilds/redeploys.
- Optional `description` field on LAN `discoverResponse` packets.
- Optional `serverName` / `serverDescription` fields on TCP `welcome` packets.
- Server browser details pane shows server description for Local and listed servers.
- Female remote-player proxies via `COPlayerProxyFemale` (`0091E5`), selected from optional appearance field `isFemale`.
- Appearance payload version 4 optional boolean `isFemale` so receivers spawn/respawn the matching male or female custom proxy base.
- Character setup now prompts Male/Female before Looks/SPECIAL; when the choice differs from the current sex, Papyrus sets `CO_SexChangePending` and the plugin runs `player.SexChange`.
- `COCharacterSetupQuestScript.psc` for quest-owned player placement, Looks menu, SPECIAL menu, and stage-100 setup completion in `Vault111Cryo`.
- Pause-menu controller map navigation: left-stick pan, right-stick zoom, LB/RB location cycle (centers selection), A select/fast-travel, B back; analog stick axes forwarded from `F4TMenuInput` into the pause Prisma view.
- Pause-menu pointer-mode switching: gamepad activity restores controller prompts/hides the PC ESC back control; mouse mode hides controller prompt bars.

### Changed
- LAN discovery now reads `server_name`, `server_description`, and `max_players` from the running server instead of hardcoded defaults.
- Local server list rows prefer the configured server name when present instead of always showing `host:port`.
- Remote-player state now preserves the last known `appearance` and `equippedItems` snapshots when a later transform omits them.
- NPC sync / proxy identity checks recognize both `COPlayerProxy` and `COPlayerProxyFemale` bases.
- Direct Connect resolves DNS hostnames (and optional `host:port` address strings) via `getaddrinfo`, so domains like `server.example.com` work in addition to IPv4 literals.
- Direct Connect UI copy notes that server address accepts an IP or domain.
- `docs/protocol.md` documents DNS hostname / `host:port` client addressing.

### Fixed
- Character setup no longer opens the Looks menu on the default over-the-shoulder camera, which happened because the quest never played the vanilla chargen camera idles.
- The chargen camera idles now resolve through `Game.GetFormFromFile` by form ID, so they no longer depend on `Idle` quest properties that cannot be assigned without crashing the Creation Kit.
- After SPECIAL finishes, character setup forces first person so the face/body camera idle does not leave third-person stuck until the player switches views.
- A stalled character setup now calls `AbortSetup` on timeout so player controls, chargen HUD, and saving are restored.
- `COCharacterSetupQuestScript.psc` now keeps `Game.FadeOutGame` calls on one line so the Papyrus compiler can parse their argument lists.
- Fast-travel replacement proxies now reapply their per-reference character name, appearance, and equipment instead of inheriting stale slot tracking and showing the base NPC name `Player`.
- The main plugin now vendors an ABI-compatible `CommonwealthOnlineUI_API.h`, so `CommonwealthOnline.dll` builds without the private UI submodule and registers the remote-map callback without calling the wrong UI function pointer at game-data-ready.
- Fast travel no longer drives remote-player proxies into distant unloaded exterior cells: cross-cell representation is limited to nearby players, held proxies remain below the local player in the loaded play space, and unresolved promoted slots reset for controlled respawn.
- NPC synchronization now excludes every actor using the `COPlayerProxy` base, including stale proxies whose runtime slot handle is temporarily unavailable.
- Unarmed punches against remote-player proxies now enter the targeted `combatHit` damage pipeline instead of being discarded by the ranged-only weapon filter.
- `launch-two-fallout4.bat` no longer exposes the entire Fallout 4 installation through Sandboxie's `OpenFilePath`; the sandboxed client now receives a private writable Prisma CEF profile instead of aborting on the regular client's shared `cef_cache` lock.
- `launch-two-fallout4.bat` now forces `UsePrivacyMode=n` so Privacy Mode cannot block `D:\SteamLibrary\...\f4se_loader.exe` (Error 2); Client 2 saves stay isolated under `FileRootPath` with default copy-on-write.
- Controller input no longer thrash-locks after a mouse nudge: gamepad re-entry ignores mouse jitter for 500ms, uses a larger move threshold, and native cursor suppress runs only in gamepad mode.
- Multiplayer B/back exits the embedded browser locally instead of depending on a native `closeBrowser` round-trip.
- Controller navigation no longer races through rows: stick/browser repeats use a 500ms initial delay and 180ms interval, with 160ms digital-button debounce.
- Hidden mouse hover no longer makes Quit appear selected while navigating Settings with a controller.
- Main-menu Settings/Quit selection now uses live DOM positions instead of sparse legacy `data-index` values, preventing Settings from selecting Quit and blocking Quit clicks.
- Main-menu B/back directly closes embedded Multiplayer or Settings; Settings discards unapplied game-setting edits on controller back.
- D-pad up/down from the Settings tab strip now enters the settings rows so vertical navigation works immediately.

### Changed
- Character setup chooses gender with a Message prompt before Looks, then opens `ShowRaceMenu` mode 1 so the unused SEX button is not shown.
- `COCharacterSetupQuestScript` now reproduces the vanilla Vault 111 respec camera setup: chargen skeleton, neutral face archetype, `ElevatorFaceCamera`/`ElevatorBodyCamera` idles replayed on `OnLooksMenuEvent`, an `InputEnableLayer` that disables camera switching, chargen HUD/save state, and `ShowRaceMenu` with no menu target.
- First-time session onboarding now enters `Vault111Cryo`, invokes `CO_CharacterSetupQuest`, waits for quest stage 100, and leaves Looks/SPECIAL sequencing to Papyrus.
- `Vault111Cryo` replaces `COVault109` as the client-side solo character-setup cell.
- `launch-two-fallout4.bat` prompts on first run for the Sandboxie file location (drive or full path), saves it in `.sandbox-file-root`, and maps drive-only answers such as `D:\` to `<drive>\sandboxes\Fallout4Client2`.
- Quit/travel confirm dialogs also accept D-pad left/right for option movement.
- Settings LB/RB switch tabs; Y alone cycles between tabs, content, and actions.

### Added
- Optional transform field `characterName` so remote player proxies show the character's name on the vanilla interact prompt instead of the shared `COPlayerProxy` base name ("Player").
- `ThirdParty/CommonwealthOnlineUI` submodule and `F4TUIClient` bridge that loads `CommonwealthOnlineUI.dll` via `RequestPluginAPI` and registers session callbacks (`JoinSession`, `Disconnect`, `ConnectLocal`, `IsConnected`, `ScanLocalServers`).
- Host-authoritative enemy NPC synchronization through capped `npcState` batches, server relay/cache support, host hostile scanning, non-host PlaceAtMe puppets, duplicate-local suppression, transform/death lifecycle handling, host handoff cleanup, fake-client support, protocol tests, and documentation.
- Shooter-authoritative ranged PvP using `gunState` fire edges for cosmetic `fireSingle` proxy actions and local `TESHitEvent` collisions for targeted damage.
- Additive `combatHit` packets with authoritative sender IDs, victim-only server routing, sender/sequence deduplication, and game-thread health damage.
- Read-only active-proxy FormID lookup in `F4TProxyActorController` for mapping local projectile collisions to remote player IDs.
- Ranged-fire and targeted-hit helpers in `server/fake_client.py` and `server/fake_player.py`, plus focused combat protocol relay tests.

### Added
- Incoming `combatHit` now drives Fallout's native `DoHitMe` hit pipeline plus `StartCombat` against the shooter's proxy, so the health meter, hit feedback, and combat state become visible (melee already did this via real attack frames).

### Changed
- Prisma/HTML UI layer moved out of this repo into CommonwealthOnlineUI (`CommonwealthOnlineUI.dll`); main plugin no longer owns Prisma views, menu inject/input, Scaleform UI openers, or `ui/` assets.
- Build/stage/deploy scripts consume a prebuilt CommonwealthOnlineUI package (including `PrismaUI_F4.dll` and views) instead of building Prisma/UI from this repo; `build-prismaui.bat` / `build-ui.bat` wrappers removed from Commonwealth-Online.
- `stage-mod.bat` always stages the sync plugin/ESP/assets/scripts even when the UI package is missing (warns instead of aborting); `build.bat` stages into `build/` after a successful compile.
- CommonwealthOnlineUI build entrypoint is a single `build.bat` (PrismaUI + UI plugin + stage); docs/scripts no longer reference separate `build-prismaui` / `build-ui` steps.
- `F4TSessionLaunch::LaunchValidatedSession` now takes a main-owned `JoinRequest` POD instead of UI `ServerEntry` types.
- NPC synchronization now captures all nearby loaded NPCs instead of waiting for hostility, while explicitly excluding remote-player proxies and NPC-sync puppets from capture and local duplicate suppression.

### Removed
- Separate `CommonwealthOnline_Vault109.esp`, its start-quest script, C++ Looks/SPECIAL camera orchestration, and the Vault 109 exit hook that started MQ102.
- Direct `ThirdParty/framework-F4-Conversion` and `ThirdParty/Ultralight-SDK` submodules (owned by CommonwealthOnlineUI).
- In-repo `ui/` HTML/CSS/JS assets and plugin UI sources (`F4TPrismaUI`, server-browser bridge/data, main-menu inject/bridges, menu input, Scaleform UI).
- Root `build-prismaui.bat` / `build-ui.bat` wrappers and `tools/scripts/setup-ultralight-link.ps1` (UI/Prisma builds run only in CommonwealthOnlineUI).

### Fixed
- Enemy NPC sync now includes actors that were already dead in the host's save before first capture, allowing their live counterparts on non-host clients to be replaced by dead host-authoritative puppets.
- Incoming `combatHit` handling no longer drops hits when the sender is briefly missing from the remote-player table, forces health via `SetBaseActorValue` first, and logs full receive/apply diagnostics for one-way Steam Deck/LAN failures.
- Server `combatHit` routing now coerces integer-valued JSON floats for `sequence`/`targetPlayerId`.
- Incoming ranged `combatHit` damage now attributes `HandleHealthDamage` to the shooter's proxy (matching how melee native hits work) and falls back to `SetActorValue`/`SetBaseActorValue` when HP would otherwise stay unchanged.
- Proxy ranged fire no longer runs `StartCombat`/`StopCombat`/`ClearAttackStates`, which was briefly holstering and redrawing the synced weapon.
- Incoming `combatHit` damage now uses `HandleHealthDamage` with a `DamageActorValue` console fallback and logs health before/after, because `ModActorValue(kDamage)` was not visibly reducing HP.
- Main menu gamepad input now reaches the HTML UI: `MainMenuBridge::ForwardControllerInput` invokes `DispatchMainMenuControllerInput` instead of only logging.
- Proxy animation transitions now remain edge-triggered when optional graph-variable writes fail or an idle proxy is restored, preventing repeated idle/holster events and allowing walk/run state to persist.
- Proxy movement stop debounce now preserves the last locomotion speed across brief zero-speed samples instead of collapsing the graph to idle.
- Armed ranged proxies now sustain NPC alert state at 10 Hz and pin `iSyncGunDown=0` per frame, preventing Fallout 4 package evaluation from automatically returning the weapon to its holster.
- Weapon animation classification now compares the scalar `WEAPON_TYPE` value directly; bitmask-style checks had classified guns such as form `0001DACF` as two-handed melee and bypassed ranged alert/ready-state maintenance.
- Melee proxies now sustain NPC alert state while drawn, and weapon attachment/reparent APIs run only on draw/holster edges instead of repeating on every locomotion-speed update.
- Weapon switches now wait 300 ms for Fallout 4's asynchronous behavior-graph reload before applying the new draw state, preventing guns switched from melee weapons from inheriting the stale melee graph/pose.

### Added
- Settings panel gamepad navigation (`handleControllerInput`): D-pad move/change, A select, B back, Y/LB/RB switch focus between tabs and content.

### Changed
- **F9** now toggles the custom main menu overlay (show/hide) for testing, instead of the server browser.
- `launch-two-fallout4.bat` stores the single Sandboxie box (`Fallout4Client2`) under `F:\Sandbox\%USERNAME%\Fallout4Client2` instead of the default `C:\Sandbox\...` path; instance 1 remains unsandboxed.

### Fixed
- `launch-two-fallout4.bat` sandbox creation used mistyped `%SANBOX_NAME%` (missing D), so `SbieIni` never configured the intended box.

### Added
- Basic melee and unarmed attack sync: optional `actionEvents` on transform packets with sequenced `meleeattackStart` replay on proxy actors.
- Weapon animation classification helper (`F4TWeaponClassification`) to branch ranged vs melee vs true-unarmed proxy graph handling.
- `server/fake_client.py` parsing/logging for `actionEvents`; `server/fake_player.py` `send_melee_action_test()` inject helper.

### Changed
- Proxy weapon draw/holster FSM and `iSyncGunDown` writes apply to ranged weapons only; melee uses MeleeBehavior draw/ready events.
- Remote action queue replays deduplicated action events through `TrySendProxyGraphEvent`.

### Added
- **Consumer server CLI** (`server/consumer_server_cli.py`): Production-ready CLI for hosting Commonwealth Online servers in cloud and on-premises environments using Typer and Rich.
- **Server orchestration service** (`server/server_service.py`): Reusable service facade for wrapping server lifecycle, configuration, and admin operations (stats, world-state control).
- **Configuration module** (`server/config.py`): JSON-based config loading, validation, and generation for hosted deployment (host, port, server name, max players, logging verbosity).
- **Windows launcher** (`server/start.bat`): Double-click to start server with automatic dependency installation and config generation.
- **Server README** (`server/README.md`): Quick-start guide for Windows users and CLI reference.
- CLI commands: `serve`, `status`, `clients`, `world time`, `world weather`, `config init`.
- JSON output mode for all commands (`--json` flag) suitable for monitoring and automation.
- Server startup banner with connection instructions and LAN address detection.
- Updated `server/requirements.txt` with `typer` and `rich` dependencies.

### Changed
- Repository cleanup: moved `protocol/` and `ROADMAP.md` under `docs/`, moved the decompiled `Interface/` reference assets into the docs tree (those assets were deleted on 2026-10-18), moved `setup-ultralight-link.ps1` to `tools/scripts/`, removed stale phase/milestone docs and unused `steam_interfaces.txt`, and added `docs/README.md` as a documentation index.
- Added contributor guidance for validating undocumented engine behavior against runtime evidence and decompiled reference material.
- Documentation updated: `docs/setup.md` now includes quick-start CLI hosting instructions.

### Fixed
- None currently in this release.
- Custom main menu overlay and open sub-panels (server browser, settings) now hide when a server join begins loading the game, so the overlay no longer obscures or blocks the vanilla loading screen.
- Mouse cursor no longer trampolines back to a centered box in the custom main menu (previously only worked with a gamepad connected). In mouse mode the title-screen engine repositions/clips the OS cursor to a small centered box every frame; with a gamepad active that path never runs, so the pointer was free. Fixed by intercepting `SetCursorPos` and `ClipCursor` in the Fallout4.exe import table (IAT) and making them no-ops while the Commonwealth Online overlay owns the pointer, so the game can no longer move or box in the Windows cursor. Only the game module's imports are patched, leaving PrismaUI's own cursor rendering untouched. Also hooks the `MainMenu` `OnCursorMoveEvent`/`OnMouseMoveEvent` vtable slots and keeps the `MenuCursor` release (`forceOSCursorPos = false`, cleared parallax constraints, widened bounds) as defense-in-depth.

### Added
- **Custom main menu overlay**: PrismaUI full-screen overlay of Commonwealth Online main menu (MULTIPLAYER, CREATIONS, SETTINGS, HELP, QUIT) auto-shows on Fallout 4 title screen, replacing the vanilla list menu.
- **Main menu input capture**: Keyboard (↑↓ / ENTER / ESC) and gamepad (dpad / A / B) navigation of custom main menu overlay; vanilla menu input is blocked so the underlying CONTINUE/NEW/LOAD/SETTINGS rows remain unreachable.
- **Main menu vanilla settings gateway**: SETTINGS → GAME SETTINGS → OPEN FALLOUT 4 SETTINGS calls `openGameSettings` stub (full vanilla settings sub-flow is planned for v1.1).
- `F4TMainMenuBridge.cpp` and `F4TMainMenuBridge.h` for main menu↔C++ event routing (controller input logging; full JS dispatch pending).
- `F4TMainMenuVanillaSettings.cpp` and `F4TMainMenuVanillaSettings.h` discovery stubs for vanilla settings panel access (ready for GFx work).
- `IsMainMenuVisible()`, `ShowMainMenu()`, `HideMainMenu()`, `IsCustomMenuInputCaptured()` public API in F4TPrismaUI.
- `SetMainMenuActive(bool)` in F4TMenuInput to route gamepad input to main menu or server browser (active target determines recipient).
- Unified pointer mode tracking and mouse/gamepad mode switching for both main menu and server browser overlays (single active menu at a time).

### Changed
- Server browser extracted into reusable `CO_Browser.create()` module (`browser/browser.js`, `browser/browser-modal.css`) matching the settings modal pattern; standalone `browser/index.html` still works for isolated UI testing via thin `app.js` bootstrap.
- Main menu MULTIPLAYER now opens the server browser as an embedded modal (left nav stays visible, background dims) instead of swapping to a separate PrismaUI view.
- `F4TPrismaUI` routes `ServerBrowserBridge`, `coAction`, `closeBrowser`, and hover sync through the main menu view when the embedded browser is active; all JS hooks (`dispatchCoEvent`, `onBrowserShown`, `closeBrowser`, `setConnectionStatus`, `CO_App`) are unchanged.
- `ServerBrowserBridge::SetView()` added so the C++ bridge can target the active PrismaUI view.
- **Main menu→multiplayer bridge**: MULTIPLAYER opens the server browser as a modal overlay on the main menu (main menu stays visible); closing the browser returns to the main menu list.

### Changed
- F4TPrismaUI renamed internal globals for clarity: `g_visible` → `g_browserVisible`, `g_domReady` → `g_browserDomReady`, `g_browserMouseCursorVisible` → `g_menuMouseCursorVisible`, `g_browserPreferGamepad` → `g_menuPreferGamepad` (shared across both views).
- Multiplayer row injection is now gated behind `constexpr bool kInjectMultiplayerRow = false` in F4TMainMenuInject.cpp. The custom main menu overlay supersedes the injected row; existing code is preserved for v1.1 opt-in.
- Controller input dispatch (`ForwardButtonPhase`) now checks active menu (main menu vs server browser) and routes to the appropriate bridge (`MainMenuBridge::ForwardControllerInput` or `ServerBrowserBridge::ForwardControllerInput`).
- `IsCustomMenuInputCaptured()` returns true when main menu OR server browser is visible, centralizing the input block guard in MainMenu hooks.
- `main-menu/script.js` now includes `window.CO_App` object with `setPointerMode()` for C++ to invoke, and `handleControllerInput()` stub for gamepad dispatch (wired in v1.1 when dispatch is complete).

### Fixed
- Game crashed on load with `REL/Trampoline.cpp: Failed to handle allocation request` because `MenuCursor` detour hooks required trampoline memory the plugin never allocated. Replaced detours with a `MainMenu::AdvanceMovie` vtable hook (no trampoline) that re-expands cursor bounds after vanilla parallax runs.
- Mouse cursor could not move away from the center of the screen while the custom main menu overlay was shown. Root causes: (1) overlay could appear during Please Stand By / intro Binks; (2) vanilla `RE::MenuCursor` min/max bounds were re-applied each frame by title-screen parallax. Fixed by gating show on intro completion, clearing constraints each frame, releasing again after `MainMenu::AdvanceMovie` and on `UIAdvanceMenusFunctionCompleteEvent`, and using `disableFocusMenu=true` for the main menu overlay.

### Changed
- Pause menu settings panel fades and slides in subtly on open and animates out on back (opacity + 10px vertical offset, 220ms ease-out).
- Pause menu no longer opens the settings panel automatically on load; the pause screen starts with no menu selection and no sub-view open.
- Settings panel uses a fixed width/height and centered position in the pause menu; tab panes share a stacked content area so switching FALLOUT 4 / MULTIPLAYER no longer resizes the window.
- Settings controller prompt bar no longer changes labels or visibility when switching tabs (MOVE, CHANGE, SELECT, BACK, SWITCH FOCUS stay consistent).
- Pause menu settings UI extracted into reusable module at `ui/views/CommonwealthOnline/settings/` (`settings-data.js`, `settings.js`, `settings.css`). Pause menu mounts it via `CO_Settings.create()` so the same component can be embedded in a future custom main menu with `layout: "standalone"`.
- Pause menu map wheel zoom now scales smoothly toward the mouse cursor instead of snapping to the viewport center.
- Pause menu map drag-pan eases toward the pointer and coasts with inertia on release.
- Pause menu settings prototype now uses two tabs only: **GAME SETTINGS** (placeholder to open vanilla Fallout 4 settings later) and **MULTIPLAYER SETTINGS** (custom Commonwealth Online options for Profile, Voice Chat, Map & Social, and Network). Removed the full category clone (Gameplay, Display, Audio, Controls, Accessibility, etc.).
- Multiplayer settings are data-driven (`text`, `toggle`, `select`, `slider`, `keybind`) with selected-row descriptions and keyboard navigation (tabs / list focus, value cycling).
- `window.CommonwealthOnlineUI` bridge stubs now expose `openGameSettings`, `setMultiplayerSetting`, `onBack`, and `onOpenView`.
- Selected multiplayer setting rows use a solid amber bar with a thin green leading edge instead of a muddy green-to-amber fade.
- Main menu prototype menu rows trimmed to MULTIPLAYER, CREATIONS, SETTINGS, HELP, and QUIT (removed CONTINUE, NEW, and LOAD).
- Cross-view UI assets consolidated under `ui/views/CommonwealthOnline/shared/` (logo, controller icons). Pause menu, main menu, and server browser now reference `../shared/` instead of per-view icon copies.

### Fixed
- Pause menu settings panel fade-in now runs reliably (dedicated `is-open` class with reflow before transition).
- Pause menu settings: pause-nav selection bars no longer spill into the settings panel (clip bars in settings mode).
- Dedicated PC back button on the pause menu map (`ESC` keycap + BACK label, top-left) so mouse users can leave map mode without relying on the controller B prompt. Fades in after the left panel hides and fades out immediately on exit so it does not overlap the logo.
- Static HTML/CSS/JS pause menu concept prototype at `ui/views/CommonwealthOnline/pause-menu/` — Fallout 4–styled multiplayer map pause screen with left menu, server info panel, controller prompts, and keyboard navigation (MAP selected by default).
- Mouse wheel zoom and click-drag pan on the pause menu map background (`ui/views/CommonwealthOnline/pause-menu/script.js`), previewing the intended Fallout 76-style navigable map; markers move with the map via shared CSS custom properties.

### Fixed
- Pause menu player/ally map markers share the map's blur filter (via a `.map-world` wrapper) so they blur and unblur in lockstep with the map instead of fading on a separate timing.
- Pause menu controller prompts: map navigation prompts (MOVE, ZOOM, SELECT, BACK) now appear on the map mode bar instead of the pause menu bar; pause menu shows SELECT and BACK only.
- Pause menu prototype's `--co-ui-scale` formula (`ui/views/CommonwealthOnline/pause-menu/styles.css`) divided a length by a bare number (`calc(100vw / 1920)`), which is a different CSS type than the plain numbers used alongside it in `clamp()`/`min()`. That type mismatch made the whole custom property invalid at the point of use, so every `calc(Npx * var(--co-ui-scale))` silently fell back to `auto` — causing the logo, menu items, and selection bar to stretch to fill their containers instead of their intended sizes. Fixed by dividing by a length (`1920px`/`1080px`) so the result is a valid unitless ratio.
- Pause menu map background switched from a cropped `cover` fit to `contain` so the full regional map is visible (less zoomed-in) instead of a tightly cropped strip, closer to a Fallout 76-style full map overview.

### Changed
- Map mode controller prompts now use LS for MOVE and RS for ZOOM (replacing the incorrect Menu glyph for move), with A SELECT and B BACK unchanged.
- Pause menu and map mode controller prompts now use Xbox PNG glyphs from `ui/Icons/Xbox` (amber-tinted like the server browser): LS (`T_X_L_2D_Alt`) MOVE, RS Y-axis (`T_X_R_Y_Alt`) ZOOM, A SELECT, B BACK.
- Rewrote root `README.md` for current project stage: structured status (working / in progress / planned / out of scope), architecture summary, developer quick start, updated repository layout and requirements, honest scale limits (4 proxies per client vs 16+ design target), and aligned license/legal sections with `LICENSE`.

### Added
- `docs/project-comparison.md` comparing Commonwealth Online with DoxyCoSync (architecture, scale, sync scope, collaboration).
- Updated root `README.md` status to reflect functional multiplayer foundation (remote proxies, appearance, apparel, weapons).
- Experimental proxy weapon alert forcing: while a remote player reports `weaponDrawn=true`, runtime proxies now periodically execute scoped `<proxyRef>.setalert 1` before weapon animation sync, then clear alert on holster, to test whether Fallout 4's NPC alert state is what keeps weapon-ready poses alive.
- One-time-per-proxy diagnostic log (`Runtime proxy weapon graph DIAGNOSTIC`) reporting the loaded animation-graph and bound-channel counts when a proxy first has a weapon equipped, to help determine why weapon FSM events are being rejected by the graph.
- Weapon animation sync for ranged guns: proxy actors now equip remote players' right-hand weapons and play armed idle/locomotion poses when `weaponDrawn=true`.
- `rightHand` weapon slot to `equippedItems` protocol: syncs equipped `WEAP` form IDs alongside apparel items. Optional field; unresolved weapons skip with throttled warning (no crash).
- Weapon-drawn animation state tracking via `iSyncGunDown` graph variable: `0` = drawn/ready, `1` = holstered/gun-down. Fires `weaponDraw` and `readyStateEnter` events on draw, `gunDownStateEnter` on holster.
- Local player right-hand weapon capture (`GetEquippedWeaponFormId`) and network transmission in transform packets.
- `docs/weapon-animation-sync.md` reference guide documenting WeaponBehavior graph events, variables, and clip naming from F4-Animation-Research unpacked XML.
- Weapon candidate graph variables to local animation graph debug probe: `iSyncGunDown`, `iRifleDrawnStateID`, `iAttackState`, `isFiring`, etc. for manual in-game validation.
- Quest auto-start on COVault109 exit: "Out of Time" quest (MQ102) now starts automatically via console `startquest`/`setstage` commands when the player leaves COVault109 for the first time.
- Complexion texture form reference (`complexionFormId`) to appearance payload so hands, arms, and body match the synced skin tone (follows the existing hair colour pattern).
- Appearance protocol version 3 with an optional `tints` field (character tint layers: skin tone, complexion, makeup, beard shade) so proxies can mirror the sender's skin colour and facial detail.
- Capture of player/NPC `tintingData` in `CaptureLocalAppearance`, plus game-thread application of tints onto runtime proxies: existing entries are updated in place by template ID, and missing entries are reconstructed against the proxy race's tint templates (skipped when no template resolves).
- `server/fake_client.py` parsing/output and `server/fake_player.py` sample payload for the new `tints` appearance field.
- Appearance protocol version 2 with optional `morphRegions` (region morph slider values) and `facialBoneMorphs` (bone-based facial morph transforms) fields so proxies receive advanced face-shape morphs that previously did not sync.
- Capture of `TESNPC::morphRegionSliderValues` and `TESNPC::facialBoneRegionSliderValues` in `CaptureLocalAppearance`, plus game-thread application of both onto runtime proxies in `ApplyProxyAppearanceFromRemoteState`.
- `server/fake_client.py` parsing/output and `server/fake_player.py` sample payloads for the new `morphRegions` and `facialBoneMorphs` appearance fields.
- Optional `appearance` transform snapshots for best-effort runtime proxy body and face visuals, including height, body morph weight, body tint, hair colors, head-part IDs, and morph slider values while leaving gender switching out.
- Game-thread proxy appearance application for supported `appearance` fields after runtime proxy 3D is ready, with race switching deferred for future proxy-base compatibility work.
- `server/fake_client.py` parsing/output and `server/fake_player.py` sample payloads for relayed `appearance` transform data.
- `COVault109` solo-cell handling in the plugin so first-time character-creation space suppresses outgoing transform packets and remote proxy representation.
- `compile-papyrus.bat` to compile active Papyrus sources from `creation-kit\scripts\source` into `creation-kit\scripts\compiled` using the Fallout 4 Creation Kit Papyrus compiler, resolving the Fallout 4 install with the same path workflow as `deploy-all.bat`, using saved `.fallout4-path` values without re-prompting during builds, and skipping reference-only `CoSync*.psc` files.
- `build-all.bat` now compiles Papyrus scripts before staging the mod package, and still stages the repo build folder for inspection when Papyrus compilation fails.
- `deploy-all.bat` now resolves the Fallout 4 path, compiles Papyrus scripts, stages the repo build folder, and then deploys from `build\Fallout 4\Data` into the Fallout 4 `Data` folder.
- `stage-mod.bat` now stages active Papyrus scripts recursively: compiled `creation-kit\scripts\compiled\*.pex` into `Data\Scripts` and raw `creation-kit\scripts\source\*.psc` into `Data\Scripts\Source\User`, while skipping reference-only `CoSync*` files.
- Server-authoritative weather and time control via `serverWorldState` packets from the dev server GUI.
- **Weather / Time** tab in `server/dev_server_app.py` with preset buttons for `fw` weather form IDs and `set gamehour to HHmm` time values.
- `server/world_state_presets.py` with shared weather and time preset definitions.
- `protocol/server-world-state.md` documenting the new packet type.
- Event-driven world time resync via optional `timeSync` field on `worldState` packets; the current world-state host pushes time after menus, loading screens, save/load, wait/sleep, and other time freezes.
- `timeSync` tracking in `server/fake_client.py` host world-state snapshot output.
- Optional `equippedItems` transform snapshots for visible apparel slots so proxies can mirror clothing, armor, hats, and eyewear changes.
- Game-thread proxy equipment application for synced apparel, including explicit tracked-slot unequips and cleanup when runtime proxy slots are held, reused, or reassigned.
- `server/fake_client.py` parsing and output for relayed `equippedItems` transform data.

### Fixed
- **Weapon animation graph now loads on proxies.** Proxy actors now trigger `AIProcess::RequestLoadAnimationsForWeaponChange()` after weapon equip/unequip, which loads the `WeaponBehavior` animation subgraph. This allows weapon events (`weaponDraw`, `readyStateEnter`, `gunDownStateEnter`) to be accepted by the animation system and enables proper weapon-drawn state animations to persist instead of immediately reverting to idle/holstered poses. Fixes: weapon briefly appears then holsters immediately on proxy.
- Proxy weapon attachment now avoids the generic queued equipment 3D refresh after a successful manual weapon attach, preventing the freshly attached weapon model from being immediately cleared again. Weapon draw/holster transitions and subsequent drawn-weapon updates also re-run weapon reparent/draw calls as a fallback while Fallout 4's `WeaponBehavior` graph is still not accepting the standard weapon animation events on proxies. Drawn-weapon states now use targeted graph writes instead of descriptor bulk writes so unrelated weapon graph variables are not clobbered back to idle/holstered defaults.
- Runtime proxy animation state no longer resets on every camera-frustum visibility flicker; only genuine 3D teardown/reload (already handled separately) now clears applied animation state. Previously, `IsVisible()` flapping on culled proxies caused the entire animation state to be treated as "just initialized" on effectively every update tick, forcing edge-triggered events (including `weaponDraw`/`readyStateEnter`/`gunDownStateEnter`) to re-fire continuously and flooding the log.
- `HasDesiredStateChanged` no longer reports "changed" on almost every tick for idle proxies. Locomotion tier comparison is now skipped while the proxy isn't moving instead of always evaluating `!lastLocomotionTier` as changed (that field is intentionally reset to empty while idle).
- Proxy head parts (including hair) now apply even when the sender's head-part count differs from the proxy base; the head-part array is reallocated with the game allocator instead of deferring the whole list on a count mismatch.
- Appearance apply log now reports applied-vs-sent head-part counts (`headParts=applied/sent`) so deferred or partial head-part application is visible.
- New clients now receive existing players' last transform snapshots immediately after `welcome`, including `appearance`, so late joiners do not start with default proxy visuals while waiting for the next sender heartbeat.
- Runtime proxy candidate isolation now ignores actors flagged as deleted, so a freshly spawned proxy is controlled instead of the slot adopting a still-present stale proxy that was marked for deletion the same frame.
- Stale saved runtime `COPlayerProxy` actors are deleted before spawning a fresh PlaceAtMe proxy, preventing duplicate visible proxies when an old uncontrolled proxy persisted in a save.
- Runtime proxy promotion no longer waits for camera-frustum `IsVisible()` before assigning a newly spawned proxy, preventing remote players from appearing only after the local player turns toward them.
- Fixed Papyrus staging path construction so `.pex` and `.psc` files are copied into `build\Fallout 4\Data\Scripts` and `build\Fallout 4\Data\Scripts\Source\User`.
- If a server profile exists but its associated save was deleted, joining from the main menu now falls back to first-time character creation and re-binds the profile to the newly created save instead of hard-failing.
- Pending first-time profile binding now completes as soon as a save exists even if character-creation phase flags are still active, preventing repeated `coc` first-time launches on reconnect.
- Session launch now resolves equivalent server endpoints (for example `localhost`, `127.0.0.1`, and legacy host-case variants) to an existing server profile before deciding first-time join flow, preventing unintended new-character creation when reconnecting.
- Server profile persistence now uses a sanitized save-folder path with a Documents fallback and write-failure logging, preventing silent loss of server profile bindings across game restarts.
- Pending profile binding now accepts exit saves (including `Exitsave...`) so the last auto-save on game exit can be rebound and loaded on reconnect, reducing accidental progress loss.
- Fixed Windows build issues in server profile path fallback by replacing unsafe `getenv` usage and accepting `std::string_view` save-folder names in folder sanitization.
- Reconnect now refreshes stale associated `Exitsave...` names to the newest available exit save before load, preventing fallback to first-time flow when exit-save filenames rotate between sessions.
- Server GUI weather now runs `fw <8-digit-form-id>` on the current `worldStateHostPlayerId` client (not hardcoded to player 1), scheduled on the game thread.
- Weather `serverWorldState` handling now defers until world-state host assignment is known, then skips only non-host clients.
- Weather `serverWorldState` packets are broadcast to all clients so the designated host receives them even after host reassignment.
- Server restart now resets player/host assignment so the first client is `playerId` 1 again, preventing GUI weather from being ignored after restart.
- Plugin now defers server weather commands until player assignment is available, preventing initial GUI clicks from being dropped right after connect.
- Replaced invalid default GUI weather IDs with valid Fallout 4 weather form IDs so `fw <id>` no longer fails with "Invalid weather ...".
- Corrected `Clear` preset ID typo from `0002852a` to valid `0002b52a` (`CommonwealthClear`).
- Time apply now converts `timeHHmm` to Fallout game-hour float before executing `set gamehour`, fixing 2200-style values applying incorrectly.
- Corrected `CO_Vault109StartQuestScript.psc` to compile against Fallout 4 Papyrus by replacing unsupported `Actor.ShowLooksMenu`/`UI.IsMenuOpen` calls with `Game.ShowRaceMenu` and `Utility.IsInMenuMode`.
- Race customization now forces third-person camera and a short pre-menu settle delay before `ShowRaceMenu`, fixing cases where the menu opens but the player model is not visible.
- `CO_Vault109StartQuestScript` now uses configurable `RaceMenuMode` (default `0`) plus `RaceMenuFallbackMode` (default `1`) for `ShowRaceMenu`.
- Race menu open flow now retries with fallback mode if the primary mode does not enter menu mode.
- Moved first-time character customization menu control out of `CO_Vault109StartQuestScript.psc`; the quest now only handles spawn/stage while plugin C++ session-launch flow owns Looks/SPECIAL sequencing.
- First-time C++ character creation now prepares camera context with `ForceThirdPerson`, opens Looks via `ShowLooksMenu 14`, retries with `Actor.ShowLooksMenu` fallback, and times out/reset safely if Looks menu never opens.
- When `LooksMenu` is confirmed open, C++ now applies chargen camera context (`SetInChargen 1` + `ForceThirdPerson`) and restores it on close, fixing first-person camera persistence during customization.
- Looks menu camera correction now directly sets `PlayerCamera` to third-person state for several polls while `LooksMenu` is open, avoiding reliance on console-only camera commands.
- Looks menu camera correction now configures the `ThirdPersonState` for face editing by zeroing shoulder offsets, flipping camera yaw toward the player, enabling free rotation, and applying a closer zoom offset.
- Looks menu camera cleanup now restores the pre-customization third-person offsets, yaw, zoom, and free-rotation flags so movement controls return to normal after first-time customization closes.

### Changed
- Weather presets store the exact `fw` console argument (8-digit hex); edit `server/world_state_presets.py` only if a preset ID fails in your console.
- Updated protocol weather examples/default preset table to match the new valid Fallout 4 preset IDs.
- World time no longer re-applies on every `worldState` heartbeat; weather heartbeats continue at ~1 Hz while time stays in sync during normal gameplay.
- Updated Weather/Time UI and protocol wording to use world-state host terminology instead of hardcoded player-1 wording.
- Reformatted `changelog.md` to follow [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
- First-time character creation launch now uses `coc COVault109` and waits for the `COVault109` start cell instead of `SanctuaryExt`.
- First-time `COVault109` launch now starts `CO_Vault109StartQuest` once per first-time join flow so the quest can move the player to its preset spawn marker.
- `CO_Vault109StartQuest` now opens `ShowRaceMenu(player, 1)` after stage-10 spawn completion, waits for menu mode to close, then opens `ShowSPECIALMenu` for name/S.P.E.C.I.A.L. setup.
- First-time Looks menu now narrows the camera FOV while open (restored on close) to magnify the face for easier facial-feature editing, since LooksMenu uses its own fixed camera that ignores third-person zoom offsets.
- First-time Looks menu now centers the player on the fixed LooksMenu camera's optical axis for the first few polls (preserving distance and facing the camera), fixing the off-center face caused by the FOV zoom; the player's original transform is restored when the menu closes.
- Added temporary diagnostic logging to LooksMenu centering (camera position/heading, target vs actual player position per poll) to confirm whether `SetPosition` takes effect and whether the menu camera is fixed or follows the player.
- Documented optional apparel equipment sync in protocol and architecture docs.

## [0.0.1] - 2026-06-23

### Added



### Changed



### Removed

- `docs/changelog.md` — moved to project root.
