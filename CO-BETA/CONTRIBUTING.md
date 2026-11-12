# Contributing

Commonwealth Online is an active Fallout 4 multiplayer mod in iterative development.

Contributions should stay focused, documented, and safe to test in a clean Fallout 4 profile.

## Current Contribution Areas

Useful contributions include:

- F4SE setup notes
- CommonLibF4 setup notes
- Creation Kit test cell documentation
- Fallout 4 actor sync research
- Network packet design
- Server architecture experiments
- Safe testing workflows
- Crash reports from controlled test builds

## Scope

Please keep contributions aligned with the current project scope:

> Dedicated-server Fallout 4 multiplayer with transform sync, proxy actors, and expanding presence/infrastructure features.

Avoid large unrelated rewrites or gameplay systems that are not part of the current milestone.

Out of scope for early development:

- Full campaign co-op
- Public matchmaking
- Anti-cheat
- Large modlist support
- MMO-style gameplay

## Legal Requirements

Do not contribute:

- Fallout 4 executable files
- Bethesda game assets
- Bethesda archives
- DLC files
- F4SE binaries
- Proprietary game code
- Decompiled Bethesda scripts
- Other mods' files without permission

Only contribute files that you have the right to share.

## Recommended Workflow

1. Open an issue describing the problem or idea.
2. Keep changes small and focused.
3. Document what was tested.
4. Use a clean Fallout 4 test profile when testing.
5. Avoid testing with large modlists.

## Commit Style

Suggested commit prefixes:

```text
docs: update setup notes
research: add actor sync notes
plugin: add basic plugin skeleton
server: add connection prototype
ck: document test cell plan
protocol: define transform packet
```

By submitting a contribution, you certify that you have the right to submit it
and agree that your contribution will be licensed under the licence applicable
to the files being modified.

Do not submit code copied or adapted from projects whose licences are
incompatible with the applicable Commonwealth Online licence.
