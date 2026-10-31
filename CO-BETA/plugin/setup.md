# Plugin Setup

The Commonwealth Online native plugin is based on `Fallout4Together`.

## Selected Template

```text
Template: Fallout4Together
Template URL: https://github.com/libxse/Fallout4Together
Use: Copied into plugin/ as the starting plugin scaffold
Forked: No
```

## Build Target

```text
Project Name: CommonwealthOnline
Output DLL: CommonwealthOnline.dll
Expected Install Path: Data/F4SE/Plugins/CommonwealthOnline.dll
```

## Current Goal

The current goal is to build the F4SE plugin, confirm it loads through F4SE, and
verify the native GNS networking path against a packaged dedicated server.

This means the plugin should be able to:

* Build successfully
* Output `CommonwealthOnline.dll`
* Be copied into `Data/F4SE/Plugins/`
* Load when Fallout 4 is launched through F4SE
* Create or write to a `CommonwealthOnline.log` file
* Prefix multiplayer/networking log lines with `[LocalPlayerId=unassigned]` before the server welcome packet and `[LocalPlayerId=N]` after assignment
* Connect to the local server on `127.0.0.1:7777`
* Send local transform packets
* Receive `welcome`, `transform`, and `disconnect` packets
* Store remote player state internally
* Move the single placed test proxy actor from remote player state on the game
  thread without dynamically spawning actors
* Smooth normal proxy movement and snap cell changes, worldspace changes, and
  teleports safely

## Not Included Yet

The plugin does not currently include:

* Dynamic actor spawning
* Multiple proxy actors
* Remote player spawning
* Combat sync
* Quest sync

## PrismaUI HTML Server Browser

Custom UI is owned by the repository-top-level `CommonwealthOnlineUI` project
(`CommonwealthOnlineUI.dll`). PrismaUI_F4 2.1.0 is an external framework
prerequisite and uses in-process Ultralight. Commonwealth-Online talks to it through
the public `plugin/include/CommonwealthOnlineUI_API.h` consumer interface and
`F4TUIClient`, which registers session callbacks for join/connect. Building
`CommonwealthOnline.dll` does not require the UI submodule; the submodule is
only required when rebuilding `CommonwealthOnlineUI.dll` and its UI package.

Initialize nested UI deps:

```bat
git submodule update --init --recursive CommonwealthOnlineUI/ThirdParty/Prisma2.0 CommonwealthOnlineUI/ThirdParty/CommonLibF4
```

Build the UI consumer from the repository root (PrismaUI_F4 remains an external prerequisite):

```bat
cd CommonwealthOnlineUI
build.bat
```

Override Ultralight with environment variable: `ULTRALIGHT_SDK_PATH`

Build the sync plugin and stage (expects a prebuilt UI package):

```bat
build-all.bat
```

This produces a mod folder at the repo root with the full game path:

```text
build/
└─ Fallout 4/
   └─ Data/
      ├─ *.esp
      ├─ F4SE/Plugins/CommonwealthOnline.dll
      ├─ F4SE/Plugins/CommonwealthOnlineUI.dll
      ├─ Materials/
      ├─ Meshes/
      ├─ Textures/
      └─ PrismaUI_F4/...
```

Merge `build\Fallout 4` into your Fallout 4 install, or run `deploy-all.bat` to copy `Data` contents directly.

`deploy-all.bat` and `deploy-ui.bat` prompt for your Fallout 4 install folder on first use (or when the saved path is missing). The choice is stored locally in `.fallout4-path` (gitignored). You can still pass a path on the command line:

```bat
deploy-all.bat "D:\SteamLibrary\steamapps\common\Fallout 4"
deploy-ui.bat "D:\SteamLibrary\steamapps\common\Fallout 4\Data"
```

`stage-mod.bat` assembles the same layout into `build/Fallout 4/Data` without touching the game install. `deploy-all.bat` runs staging first, then copies into `Data`.

In-game controls:

* Main Menu > Multiplayer opens the server browser
* Press **F9** to toggle the custom main menu overlay (testing)
* Use the server list to browse, filter, and join servers
* **Local server (Dev)** connects to `127.0.0.1:7777` — start the native server package first

If PrismaUI_F4 is missing, the plugin still loads and networking continues to work; only the HTML UI is skipped.

## Required Tools

The plugin setup is expected to require:

* Fallout 4 on Steam
* Fallout 4 Script Extender, F4SE
* Visual Studio 2022
* MSVC C++ compiler
* Windows SDK
* XMake
* Git

## Recommended Folder Layout

```text
plugin/
├─ xmake.lua
├─ src/
├─ include/
├─ external/
├─ build/
├─ README.md
└─ SETUP.md
```

## Build Steps

From the repository root:

```bash
cd plugin
xmake
xmake build
```

If the build succeeds, look for the compiled DLL in the output folder created by XMake.

## Install Location

For manual testing, the compiled plugin DLL should eventually be copied to:

```text
Fallout 4/Data/F4SE/Plugins/CommonwealthOnline.dll
```

If the `F4SE/Plugins/` folder does not exist inside `Data/`, create it manually.

## Testing Checklist

Use this checklist for the first plugin test:

* [ ] Fallout 4 launches normally without the plugin
* [ ] Fallout 4 launches through F4SE without the plugin
* [ ] `CommonwealthOnline.dll` builds successfully
* [ ] `CommonwealthOnline.dll` is copied to `Data/F4SE/Plugins/`
* [ ] Fallout 4 launches through F4SE with the plugin installed
* [ ] Game reaches the main menu without crashing
* [ ] Plugin log file is created
* [ ] Plugin log confirms that the plugin loaded
* [ ] Local server is running before gameplay networking tests
* [ ] Plugin log shows `[LocalPlayerId=unassigned]` before assignment
* [ ] Plugin log shows `[LocalPlayerId=N] Assigned server playerId: N`
* [ ] Plugin logs prefixed remote player updates from another connected client
* [ ] Plugin logs prefixed remote player removal after a disconnect
* [ ] `CommonwealthOnline.esp` is enabled (provides `COPlayerProxy` actor base, local FormID `002666`)
* [ ] With two Fallout 4 instances in the same cell, each instance spawns a dynamic
      proxy at the other player's networked position
* [ ] Optional regression: `F4TProxyRemotePlayer01REF` resolves when placed fallback flag is on

### Global dynamic spawn validation

* [ ] Two clients in the same vanilla interior (e.g. Sanctuary house) see each other's proxies
* [ ] Two clients in the Commonwealth exterior in the same loaded area see proxies
* [ ] Remote player in a different cell: proxy held hidden, not visible
* [ ] Local player walks into remote player's cell: proxy spawns at remote position
* [ ] Local player leaves a cell with active proxies: no orphaned actors remain in prior cell

## Notes

Remote actors are dynamically spawned at runtime in the local player's current
cell. The game-thread proxy actor controller consumes a copied remote-player
snapshot to spawn, move, and hold proxy actors per remote `playerId`.
