# Git guardrail hooks

Portable git hooks (Node, extensionless — run on Windows Git Bash + Unix) enforcing four
policies regardless of which tool/agent runs git. Requires **Node on PATH**.

| Hook | Blocks |
| --- | --- |
| `commit-msg` | commit messages that credit an AI tool or model: authorship, sign-off, assistance and generation trailers, `Generated with/by/using` footers, and AI noreply addresses (the name list is in the hook; `node .githooks/commit-msg.test.js` checks it) |
| `pre-commit` | an AI author/committer identity; **secrets** in the staged diff (GitHub/OpenAI/OpenRouter/GitLab/npm/Hugging Face/SendGrid/AWS/Google/Slack/Stripe keys, private-key blocks, hardcoded creds); staged **`.pdb`** files; a **`docs/code-notes/*.md`** note with **no code reference** (must anchor to `<file>:<symbol>` / source path / `Class::method`) |
| `pre-push` | any push to a remote whose URL resolves to `Dear-Modding-FO4/commonlibf4` (read-only upstream); any push of `refs/heads/main` to our Forgejo repo (set `CO_ALLOW_MAIN_PUSH=1` to override; `node .githooks/pre-push.test.js` checks it) |

## Install

Per-repo (team-wide if committed):
```bash
bash .githooks/install.sh
```

Global (every repo on this machine):
```bash
bash .githooks/install.sh global
```

Both just set `core.hooksPath`. Undo with:
```bash
git config --unset core.hooksPath          # per-repo
git config --global --unset core.hooksPath # global
```

## Guard 4 — comments go in a code-notes doc (not a git hook)

Git can't rewrite files pre-commit, so this one is an **agent instruction**. Add to
`AGENTS.md` (Codex) / `CLAUDE.md` (Claude Code):

> **Code comments:** explanation, reasoning, how-it-works, design notes, and bug history
> do NOT go in source code — write them to `docs/code-notes/<project>.md` anchored to
> `<file>:<symbol>`. In source keep only a single-line constraint comment when genuinely
> needed, a license header, and short TODO/FIXME markers.

## Test they work

```bash
# Guard 1: should be blocked
git commit --allow-empty -m "x

Co-Authored-By: Claude <noreply@anthropic.com>"

# Guard 2: stage a fake token or a .pdb, then commit -> blocked
printf 'k="ghp_%s"\n' "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" > s.txt && git add s.txt && git commit -m x

# Guard 3: a remote whose URL contains dear-modding-fo4 -> push blocked
```

## Notes
- Hooks are bypassable with `git commit --no-verify` / `git push --no-verify` by design —
  they are guardrails, not a security boundary.
- If a hook doesn't run, check `git config --get core.hooksPath` and that Node is on PATH.
