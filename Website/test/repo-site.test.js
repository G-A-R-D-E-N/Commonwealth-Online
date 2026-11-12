"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const ejs = require("ejs");

const ROOT_DIR = path.resolve(__dirname, "..");
const GOOD_ENV = {
  GITEA_BASE: "https://git.example.test/",
  GITEA_OWNER: "Example-Owner",
  GITEA_REPO: "example.repo",
};

const loadConfig = (env) => {
  const saved = {};
  for (const key of Object.keys(env)) {
    saved[key] = process.env[key];
    process.env[key] = env[key];
  }
  delete require.cache[require.resolve("../src/config")];
  try {
    return require("../src/config");
  } finally {
    for (const key of Object.keys(env)) {
      if (saved[key] === undefined) {
        delete process.env[key];
      } else {
        process.env[key] = saved[key];
      }
    }
  }
};

const loadResolver = () => {
  const sandbox = { window: {} };
  vm.runInNewContext(fs.readFileSync(path.join(ROOT_DIR, "static/js/repo-paths.js"), "utf8"), sandbox);
  return sandbox.window.CO_REPO_PATHS.resolveRepoPath;
};

const main = async () => {
  for (const [env, field] of [
    [{ GITEA_BASE: "ftp://git.example.test" }, "GITEA_BASE"],
    [{ GITEA_BASE: "https://user@git.example.test" }, "GITEA_BASE"],
    [{ GITEA_BASE: "https://git.example.test/?x=1" }, "GITEA_BASE"],
    [{ GITEA_BASE: "git.example.test" }, "GITEA_BASE"],
    [{ GITEA_OWNER: "evil/owner" }, "GITEA_OWNER"],
    [{ GITEA_REPO: "../repo" }, "GITEA_REPO"],
  ]) {
    assert.throws(() => loadConfig(env), new RegExp(`${field} must`), `${JSON.stringify(env)} is refused at startup`);
  }

  const config = loadConfig(GOOD_ENV);
  assert.equal(config.gitea.base, "https://git.example.test", "a trailing slash on the base is trimmed");
  assert.equal(config.links.github, "https://git.example.test/Example-Owner");
  assert.equal(config.links.repository, "https://git.example.test/Example-Owner/example.repo");

  const resolveRepoPath = loadResolver();
  const shape = (value, baseDir) => {
    const resolved = resolveRepoPath(value, baseDir);
    return resolved ? { path: resolved.path, query: resolved.query, fragment: resolved.fragment } : null;
  };
  assert.deepEqual(shape("guide.md", "docs/setup"), { path: "docs/setup/guide.md", query: "", fragment: "" });
  assert.deepEqual(shape("../README.md", "docs/setup"), { path: "docs/README.md", query: "", fragment: "" }, "one parent step");
  assert.deepEqual(shape("../../LICENSE", "docs/setup"), { path: "LICENSE", query: "", fragment: "" }, "two parent steps reach the root");
  assert.equal(shape("../../../LICENSE", "docs/setup"), null, "a link cannot climb above the repository root");
  assert.equal(shape("../x.md", ""), null, "a root document cannot link above the root");
  assert.deepEqual(shape("./img/a.png", ""), { path: "img/a.png", query: "", fragment: "" });
  assert.deepEqual(shape("/assets/a.png", "docs/setup"), { path: "assets/a.png", query: "", fragment: "" }, "a leading slash is the repository root");
  assert.deepEqual(shape("guide.md#setup", "docs"), { path: "docs/guide.md", query: "", fragment: "#setup" }, "anchors are kept");
  assert.deepEqual(shape("guide.md?plain=1#L4", ""), { path: "guide.md", query: "?plain=1", fragment: "#L4" });
  assert.deepEqual(shape("../guide.md?x=1", "docs/setup"), { path: "docs/guide.md", query: "?x=1", fragment: "" });
  assert.deepEqual(shape("sub/", "docs"), { path: "docs/sub", query: "", fragment: "" }, "a trailing slash names a directory");
  assert.deepEqual(shape("  guide.md  ", ""), { path: "guide.md", query: "", fragment: "" }, "surrounding spaces are ignored");
  for (const value of ["#top", "?plain=1", "", "   ", "./", "//evil.example/x.md", "https://example.com/a", "mailto:dev@example.com", "javascript:alert(1)", undefined]) {
    assert.equal(shape(value, "docs"), null, `${String(value)} is left as written`);
  }

  const partial = fs.readFileSync(path.join(ROOT_DIR, "views/partials/scripts.ejs"), "utf8");
  const hostile = "https://git.example.test/</script><script>alert(1)</script>";
  const rendered = ejs.render(partial, {
    page: { scripts: ["/static/js/repo.js"] },
    site: { gitea: { base: hostile, owner: "o", repo: "r" }, links: {} },
  });
  assert.equal(rendered.includes("</script><script>alert"), false, "a config value cannot close the island");
  const island = rendered.match(/id="co-site-config">([\s\S]*?)<\/script>/);
  assert.ok(island, "the partial renders the site config island");
  assert.equal(JSON.parse(island[1]).gitea.base, hostile, "the island decodes back to the original value");

  const dbPath = require.resolve("../src/db");
  require.cache[dbPath] = { exports: { initDb: () => null, getDb: () => null } };
  const { createApp } = require("../src/app");
  const server = await new Promise((resolve) => {
    const instance = createApp().listen(0, "127.0.0.1", () => resolve(instance));
  });
  try {
    const response = await fetch(`http://127.0.0.1:${server.address().port}/repo`);
    assert.equal(response.status, 200);
    const html = await response.text();
    const site = JSON.parse(html.match(/<script type="application\/json" id="co-site-config">([\s\S]*?)<\/script>/)[1]);
    assert.deepEqual(site.gitea, { base: "https://git.example.test", owner: "Example-Owner", repo: "example.repo" });
    assert.equal(site.links.repository, "https://git.example.test/Example-Owner/example.repo");
    assert.ok(html.indexOf("/static/js/repo-paths.js") < html.indexOf("/static/js/repo.js"), "the resolver loads before repo.js");

    const csp = response.headers.get("content-security-policy");
    const directive = (name) => csp.split("; ").find((part) => part.startsWith(`${name} `));
    const directoryOrigin = new URL(config.serverDirectoryUrl).origin;
    assert.equal(directive("img-src"), "img-src 'self' data: https://git.example.test https://cdn.jsdelivr.net");
    assert.equal(directive("connect-src"), `connect-src 'self' https://git.example.test ${directoryOrigin}`);
  } finally {
    server.close();
  }

  console.log("repo site tests passed");
};

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
