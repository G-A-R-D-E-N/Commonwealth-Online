const { spawnSync } = require("child_process");
const path = require("path");
const assert = require("assert");

const hook = path.join(__dirname, "pre-push");
const ours = "https://git.nomadicinteractive.dev/GARDEN/CommonwealthOnline-Matrix.git";
const sha = "a".repeat(40);
const zero = "0".repeat(40);
const run = (url, refs, env) =>
  spawnSync(process.execPath, [hook, "origin", url], {
    input: refs,
    encoding: "utf8",
    env: { ...process.env, CO_ALLOW_MAIN_PUSH: "", ...env },
  }).status;
const line = (remoteRef) => `refs/heads/x ${sha} ${remoteRef} ${zero}\n`;

assert.strictEqual(run(ours, line("refs/heads/main")), 1, "push to main must be blocked");
assert.strictEqual(run(ours, line("refs/heads/night/x") + line("refs/heads/main")), 1, "main in a multi-ref push must be blocked");
assert.strictEqual(run(ours, line("refs/heads/nomad-dev")), 0, "nomad-dev is allowed");
assert.strictEqual(run(ours, line("refs/heads/night/main-fix")), 0, "branch names containing main are allowed");
assert.strictEqual(run(ours, line("refs/heads/main"), { CO_ALLOW_MAIN_PUSH: "1" }), 0, "owner override works");
assert.strictEqual(run("ssh://git@git.nomadicinteractive.dev:2222/GARDEN/CommonwealthOnline-Matrix.git", line("refs/heads/main")), 1, "ssh url blocked");
assert.strictEqual(run("git@git.nomadicinteractive.dev:GARDEN/CommonwealthOnline-Matrix.git", line("refs/heads/main")), 1, "scp url blocked");
assert.strictEqual(run(ours, `(delete) ${zero} refs/heads/main ${sha}
`), 1, "deleting main is blocked");
assert.strictEqual(run("https://example.com/other/repo.git", line("refs/heads/main")), 0, "other repos are untouched");
assert.strictEqual(run("https://github.com/Dear-Modding-FO4/commonlibf4.git", line("refs/heads/x")), 1, "upstream stays blocked");
console.log("pre-push tests passed");
