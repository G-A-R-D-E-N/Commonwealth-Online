# Tools

Helper utilities and development scripts for Commonwealth Online.

## Scripts

| Script | Purpose |
|---|---|
| [`restore-vanilla-mainmenu.ps1`](restore-vanilla-mainmenu.ps1) | Restore vanilla `MainMenu.swf` from game archives |
| [`extract_ba2.py`](extract_ba2.py) | BA2 archive extraction helper |

PrismaUI / Ultralight builds live in the top-level `CommonwealthOnlineUI/` package (`build.bat`). `CO-BETA/stage-mod.bat` stages from that package (`UI_DIR`). `CO-BETA/ThirdParty/CommonwealthOnlineUI` is a legacy gitlink that staging does not use.

## Game files

Do not place Fallout 4 game files or proprietary tools in this folder, and do not commit extracted vanilla assets. Use [`extract_ba2.py`](extract_ba2.py) to read an archive locally when a reference is needed.
