# Work log — `feat/server-vats-policy-clean` worktree

## 2026-10-27 — VATS code-notes hardened for adversarial review
- `docs/code-notes/vats-policy.md`: every non-obvious piece anchored as
  `<file>:<symbol>`; fixed the `ReadBool`/`ReadFloat` sectioned-vs-bare order claim
  (orders differ per type, intentional); documented the all-or-nothing snapshot,
  the `kNewGame` generation-flow coverage, the two 3-arg `DerivePolicy` data paths
  (`ServerPersistenceSetup.inc:168`, `ServerRuntime.inc:395`), the nomad-dev rebase
  conflict resolutions (session include, wincompile union, 747+18 test split wired
  in `co-ui-ci.yml:140`), and three cosmetic nits (applied-list over-marking,
  raw-text config store, double command-list build).
- Runtime verdicts recorded: OG 1.10.163 static-presence verified (3 versioned exes
  ×1 string, OG corpus init/dtor, ESM EDID) with behavior owed; NG 1.10.984 thin
  (1 versioned exe, no NG corpus) with behavior owed; AE 1.11.240 fully owed (no
  binary, no corpus on this machine). PR stays HELD pending the owner console check
  (`setgs fVATSMaxEngageDistance 0/5000`) + two-client run. No source edits, no new
  `.inc` files, no C++ builds.

## 2026-10-27 — PR opened (HELD), CI queued
- PR #585 `feat/server-vats-policy-clean` -> `nomad-dev`:
  https://git.nomadicinteractive.dev/GARDEN/CommonwealthOnline-Matrix/pulls/585 —
  state open, mergeable, NOT merged. Review comment posted (4 rounds, fixes,
  unverified items). CI: 24 pull_request jobs queued (`waiting`, runs ~25445+),
  none green or red yet. Temp PR payload files removed. This log entry left
  uncommitted on purpose so queued CI is not retriggered.

## 2026-10-27 - PR #587 shot-replay anim events reviewed, ready to merge
- PR #587 `feat/fs-phase1-aim-replay` -> `nomad-dev`: anim-event half of FS phase 1 shot replay. Server relay + co-core validation speak both kinds; plugin captures/applies `playerAnimEvent` only. Aim carved to `re-answers-pending/aim-replay.patch` (RE Q1/Q2 open, no answer file yet). Also carries the NoTalk + live-fire commits shared with #574 so #574 can rebase to the remainder.
- Rebased to nomad-dev b095482a (2 conflicts: co-core.md kept both sections, windows-compile-ci.py union tuple). F4TShotReplayContract passes, tests/audit 120 passed.
- Review comment posted on #587 (2 rounds, 6 fixes, unverified C++ + per-runtime in-game behavior). One minor logged on #366 (stale-holder edge). Merging with CI red allowed per owner rule; CI run to be reported after merge.
