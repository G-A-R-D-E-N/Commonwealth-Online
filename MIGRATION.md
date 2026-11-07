# Commonwealth Online Matrix migration notes

## Layout

- `CO-SERVER/` — native C++ authoritative server (`server-cpp`), Avalonia Host GUI, GNS bridge sources
- `CO-BETA/` — F4SE client / beta plugin tree
- `CommonwealthOnlineUI/` — injected UI

## CI

The private repository does not execute its workflows. There is no `.github/workflows/`
folder at the repository root; the CI workflows are kept in `.github/public-ci/workflows/`.
`.github/public-ci/sync/public-ci-sync.yml` runs on a 10-minute schedule (and on manual dispatch):
it exports every open PR head (whatever its base, including `nomad-dev`) to `ci/pr-<number>` in the
public mirror `G-A-R-D-E-N/Commonwealth-Online`, and `main` and `nomad-dev` to `ci/main` and
`ci/nomad-dev`. `scripts/export-public-ci.py` builds each export commit and moves the workflows to
`.github/workflows/`, where their `on: push: branches: ["ci/**"]` trigger runs them. Each run posts a
`public-ci/<name>` commit status back on the private head commit. A new head is exported on the next
sync tick (the schedule is best-effort; a manual dispatch starts it at once). The historical nested
`CO-SERVER/.github/workflows/` tree has been deleted; any remaining nested `*/.github/workflows/`
files are historical and are **not** executed by GitHub Actions.

Runner inventory is maintained in `G-A-R-D-E-N/GHRUNNER`.

## Transport

The production transport is GameNetworkingSockets end to end. The managed
authoritative server's TCP path is retired: no workflow builds, tests, or runs
it, canonical acceptance drives the native C++ server over real GNS, and
`scripts/verify-production-transport.py` fails the build if
a socket-shaped path reappears in either plugin target or the native server.

The managed acceptance harness is gone. `CO-SERVER/server/` now holds only `native_transport/`
(the GNS bridge sources) and `scripts/` (bridge build and the repository runtime policy), which
the native build depends on. The native C++ server (`CO-SERVER/server-cpp`) is the only server
launch path. The Host GUI starts only a native package and checks its native marker and GNS
bridge (`CO-SERVER/host/Services/ServerController.cs`).

## Runtime policy scope

`verify-no-legacy-runtime.sh` scans `CO-SERVER/` only. The Python relay was removed in commit `680f600` and is superseded by the native dedicated server (`CO-SERVER/server-cpp`). `CO-BETA/server/` no longer exists. Upgrade steps are in `CO-BETA/docs/install-and-migration.md`. Widening the guard to the whole repository is still the checkpoint that proves the retirement is finished rather than a change to make now.

## Identity

`playerId` is transient. Durable continuity is `profileId` + resume
credential. See `docs/server/IDENTITY.md`.

## Known LFS gap

These Creation Kit RawAssets LFS objects 404 on the old `CO-BETA` LFS
endpoint and are therefore still absent from Matrix:

- `CO-BETA/creation-kit/assets/RawAssets/*.fbx` / `*.spp` / `*.mb` listed in the Phase 1 audit

Recover from a local artist backup if the binaries are still needed.
