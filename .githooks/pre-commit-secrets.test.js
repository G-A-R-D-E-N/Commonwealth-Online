const { spawnSync } = require("child_process");
const fs = require("fs");
const os = require("os");
const path = require("path");
const assert = require("assert");

const hook = path.join(__dirname, "pre-commit");
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "precommit-"));
const git = (...args) => spawnSync("git", args, { cwd: dir, encoding: "utf8" });
git("init", "-q");
git("config", "user.name", "Test User");
git("config", "user.email", "test@example.com");

const stage = (text) => {
  fs.writeFileSync(path.join(dir, "f.txt"), text);
  git("add", "f.txt");
  return spawnSync(process.execPath, [hook], { cwd: dir, encoding: "utf8" }).status;
};
const hex = (n) => "a1B2".repeat(n).slice(0, n);

const blocked = {
  "OpenRouter key": "sk-or-" + "v1-" + hex(64),
  "GitLab token": "glpat-" + hex(24),
  "npm token": "npm_" + hex(36),
  "Hugging Face token": "hf_" + hex(36),
  "SendGrid key": "SG." + hex(22) + "." + hex(43),
  "GitHub token": "ghp_" + hex(36),
};
for (const [name, key] of Object.entries(blocked)) {
  assert.strictEqual(stage("value " + key + "\n"), 1, name + " must be blocked");
}
assert.strictEqual(stage("a normal line of text\n"), 0, "plain text is allowed");
assert.strictEqual(stage("see hf_short and glpat-short and npm_short\n"), 0, "short look-alikes are allowed");
console.log("pre-commit secret tests passed");
