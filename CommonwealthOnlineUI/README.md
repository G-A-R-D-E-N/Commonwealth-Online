# CommonwealthOnlineUI

Commonwealth Online UI F4SE plugin. Owns PrismaUI (Ultralight) views, HTML/CSS/JS assets, menu input/cursor handling, main-menu inject, Scaleform UI openers, and server-browser presentation.

Networking and join/connect behaviour stay in Commonwealth-Online (and later CoSync). Those are invoked through the exported `RequestPluginAPI` / `SessionCallbacks` surface in [`include/CommonwealthOnlineUI_API.h`](include/CommonwealthOnlineUI_API.h).

Uses the supplied Prisma V12 consumer API. Public PrismaUI_F4 2.1.0 uses an in-process
Ultralight 1.4.0 runtime. Build **this** plugin against `include/PrismaUI_F4_API.h`; install
the separate PrismaUI_F4 framework package in the Fallout 4 mod list.

## Requirements

- Windows
- Visual Studio 2022 with C++ tools, or another MSVC/Clang-CL C++23 toolchain
- xmake 3.0.0 or newer
- Git
- Fallout 4 Script Extender installed for testing
- **PrismaUI_F4 2.1.0** installed as a separate Fallout 4 mod

## Submodules

```powershell
git submodule update --init --recursive
```

| Path | Purpose |
|------|---------|
| `ThirdParty/CommonLibF4` | CommonLibF4 |
| `include/PrismaUI_F4_API.h` | Supplied Prisma V12 consumer header |

## Build

```powershell
.\build.bat
```

`build.bat` will:

1. Check the supplied Prisma V12 consumer header
2. Build `CommonwealthOnlineUI.dll`
3. Stage the plugin and CommonwealthOnlineUI views into `build\Fallout 4\Data` and `package\`

Staged output:

```text
build/Fallout 4/Data/
├─ F4SE/Plugins/CommonwealthOnlineUI.dll
└─ PrismaUI_F4/
   ├─ shell/                      ← includes CO override that hides Prisma's boot toast
   └─ views/…                     ← framework glyphs + CommonwealthOnline views
```

`ui/shell-overrides/` is copied into `PrismaUI_F4/shell` on stage/deploy so the temporary top-right **Prisma Loaded** toast stays hidden.

## Deploy to Fallout 4

```powershell
.\deploy.bat
.\deploy.bat "D:\SteamLibrary\steamapps\common\Fallout 4"
```

Or enable the staged package / `package/` folder as an MO2 mod. PrismaUI_F4 2.1.0 must load before CommonwealthOnlineUI.

## Package for mod managers

```powershell
.\scripts\package.ps1 -OutputName "CommonwealthOnlineUI"
```

## Generate Visual Studio project

```powershell
xmake project -k vsxmake
```

## Repo layout

```text
src/                  C++ UI plugin source
include/              Public API + internal headers
ui/                   HTML/CSS/JS views and icons
ThirdParty/CommonLibF4/
include/PrismaUI_F4_API.h       Supplied Prisma V12 consumer header
build/                           Staged mod output
package/                         Same layout for zip / parent-repo staging
scripts/
```

## Public API

Commonwealth-Online loads this plugin and calls:

```cpp
auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
    COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
ui->SetSessionCallbacks(&callbacks);
ui->OnGameDataReady();
```

Session callbacks must implement `JoinSession`, `Disconnect`, `ConnectLocal`, `IsConnected`, and `ScanLocalServers`.

## Server browser data

- **Browse** refreshes from the public directory at
  `https://commonwealth-online.com/servers/server.json` (C++ WinHTTP in-game;
  `fetch` in standalone HTML when the bridge is unavailable). Directory
  failures remain distinct from an empty result; Local, Recent, and Direct
  Connect remain available independently.
- **Local** uses LAN discovery via `ScanLocalServers`.
- **Recent** persists successful joins to
  `%USERPROFILE%\Documents\My Games\Fallout4\CommonwealthOnline\recent-servers.json`
  (C++), and pushes the list when the browser opens. Browser `localStorage` is only a soft cache.
- Join: click a row to select, click again (or double-click) to connect.
- The pause menu **CURRENT SERVER** panel shows the joined server name, live player
  count (`1 + remote players`), max players from the join listing, and the join-time
  ping (no second TCP connect while already in-session). The panel is hidden when
  disconnected.
