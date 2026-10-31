# Commonwealth Online Matrix

This repository contains the current Commonwealth Online client, server, shared protocol core, and UI work.

## Current development authority

`nomad-dev` is the integration branch and the source of truth for current work. Every pull request targets it. `main` is protected and moves only when the owner approves a release through the release tracking pull request (#345).

Historical audit and repair PRs are retained only as evidence; validate new work against the exact current `nomad-dev` SHA.

The project is **not release-ready**. Current release targets are exactly:

- Fallout 4 `1.10.163` via `CommonwealthOnline-OG.dll`
- Fallout 4 `1.10.984` via `CommonwealthOnline-NG.dll`
- Fallout 4 `1.11.240` via `CommonwealthOnline.dll`

Do not claim support for `1.10.980` or other runtimes without separate proof.

## Repository map

| Path | Authority |
| --- | --- |
| `co-core/` | Shared engine-independent protocol, identity, validation, snapshots, persistence, authority, and compatibility logic. Put code here when it does not require Fallout/F4SE. |
| `CO-SERVER/` | Dedicated server, native GNS transport, persistence, admission, admin control, and the server Host GUI. |
| `CO-BETA/` | Fallout 4 F4SE client, gameplay/network integration, Creation Kit content, Papyrus, build scripts, and client packaging. |
| `CommonwealthOnlineUI/` | Source of truth for the Commonwealth Online UI plugin and Prisma HTML/CSS/JS views. |
| `CO-BETA/ThirdParty/CommonwealthOnlineUI` | Release/build gitlink for the UI mirror. It is not a second place to implement UI features; source authority is the top-level `CommonwealthOnlineUI/`. |
| `docs/` | Architecture, protocol contracts, code notes, proof records, roadmap, and RE documentation. |
| `.github/workflows/` | CI and acceptance gates. Tests must describe the real supported path rather than a parallel implementation. |

## Where to make a change

- Wire format, identity, validation, snapshot model, generic authority logic: **`co-core/`**
- Dedicated server behavior or persistence: **`CO-SERVER/`**
- Fallout actor/gameplay integration: **`CO-BETA/plugin/`**
- ESP, Papyrus, meshes, materials, textures: **`CO-BETA/creation-kit/`**
- Menus, browser, alerts, Prisma views: **`CommonwealthOnlineUI/`**
- Cross-component acceptance: **`CO-BETA/ci/`** and the relevant server acceptance workflow

Avoid creating another implementation of the same subsystem in a different tree.

## Reverse engineering and CommonLib

For engine-facing or reverse-engineering work, inspect this repository first:

`PRISMA-USER-INTERFACE-FRAMEWORK/commonlib_NonVR`

Treat it as **read-only reference material**. Never modify or push to CommonLib as part of Commonwealth Online work.

If the required engine fact cannot be established from `commonlib_NonVR` or other clean evidence, stop that specific change and document the RE blocker in the active PR. Do not guess offsets, layouts, members, or runtime behavior.

## Pull request policy during consolidation

- **`nomad-dev`**: the canonical integration line.
- Focused PRs containing unique behavior may remain open, but must be restacked or reconstructed on the canonical line before merge.
- Validation-only, old stacked prerequisite, obsolete migration, and superseded reconstruction PRs should remain closed as historical references.
- Do not merge stale PRs wholesale simply because they contain useful code. Extract the proven delta and revalidate it on the current integration head.

## Release gates

A shippable client requires all of the following:

1. AE (`1.11.240`), NG (`1.10.984`), and OG (`1.10.163`) DLLs build.
2. Native server and client protocol acceptance are green.
3. The final Windows package contains both client DLLs, GNS bridge, UI/Prisma package, ESP, shipped Papyrus, and required assets.
4. `CommonwealthOnlineClient.json` and the content fingerprint are generated from that final staged package.
5. Clean-install testing succeeds.
6. Live Fallout testing succeeds on both supported runtimes.
7. Any engine-facing assumption used by the release has been verified against `commonlib_NonVR` or explicitly documented as blocked.

Until those gates are complete, keep the integration PR Draft and do not ship.
