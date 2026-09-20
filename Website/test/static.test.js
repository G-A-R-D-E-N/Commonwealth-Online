"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const http = require("node:http");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const dist = path.join(root, "dist");
const expectedPages = [
  "index.html",
  "media/index.html",
  "roadmap/index.html",
  "servers/index.html",
  "updates/index.html",
  "apply/index.html",
  "apply/team/index.html",
  "apply/beta/index.html",
  "apply/thanks/index.html",
  "forum/index.html",
];

const startServer = (handler) =>
  new Promise((resolve) => {
    const server = http.createServer(handler);
    server.listen(0, "127.0.0.1", () => resolve(server));
  });

const closeServer = (server) =>
  new Promise((resolve, reject) => {
    server.close((error) => (error ? reject(error) : resolve()));
    server.closeAllConnections?.();
  });

const request = async (base, route) => {
  const response = await fetch(`${base}${route}`);
  return { response, text: await response.text() };
};

const serveStatic = (req, res) => {
  const requestPath = decodeURIComponent(new URL(req.url, "http://127.0.0.1").pathname);
  const relativePath = requestPath.endsWith("/") ? `${requestPath}index.html` : requestPath;
  const filePath = path.resolve(dist, `.${relativePath}`);
  if (!filePath.startsWith(`${dist}${path.sep}`)) {
    res.writeHead(404).end();
    return;
  }
  fs.createReadStream(filePath).on("error", () => res.writeHead(404).end()).pipe(res);
};

const run = async () => {
  assert.equal(fs.existsSync(dist), true, "run npm run build:static first");
  for (const page of expectedPages) {
    assert.equal(fs.existsSync(path.join(dist, page)), true, `missing ${page}`);
  }

  const htmlFiles = [];
  const collectHtml = (directory) => {
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
      const filePath = path.join(directory, entry.name);
      if (entry.isDirectory()) {
        collectHtml(filePath);
      } else if (entry.name.endsWith(".html")) {
        htmlFiles.push(filePath);
      }
    }
  };
  collectHtml(dist);
  for (const filePath of htmlFiles) {
    const html = fs.readFileSync(filePath, "utf8");
    assert.equal(/<%|<%=|<%-/.test(html), false, `unresolved EJS in ${filePath}`);
    assert.match(html, /https:\/\/cdn\.jsdelivr\.net\/npm\/@widgetbot\/crate@3/);
    assert.match(html, /src="\/static\/js\/widgetbot\.js"/);
    assert.match(html, /script-src 'self' https:\/\/cdn\.jsdelivr\.net/);
    assert.match(html, /frame-src https:\/\/e\.widgetbot\.io/);
    assert.doesNotMatch(html, /script-src[^;]*unsafe-inline/);
    assert.doesNotMatch(html, /frame-src[^;]*https:\/\/discord\.com/);
  }

  const staticServer = await startServer(serveStatic);
  const staticBase = `http://127.0.0.1:${staticServer.address().port}`;
  try {
    for (const route of ["/", "/media/", "/roadmap/", "/servers/", "/updates/", "/apply/", "/forum/"]) {
      const page = await request(staticBase, route);
      assert.equal(page.response.status, 200, route);
    }

    const staticData = JSON.parse(fs.readFileSync(path.join(dist, "data/servers.json"), "utf8"));
    const sourceData = JSON.parse(fs.readFileSync(path.join(root, "servers/server.json"), "utf8"));
    assert.deepEqual(staticData, sourceData, "server data copied unchanged");
    for (const image of ["server-browser-direct-connect.webp", "server-browser-recent.webp"]) {
      assert.equal(fs.existsSync(path.join(dist, "assets/images", image)), true, `missing ${image}`);
    }

    const changelogs = JSON.parse(fs.readFileSync(path.join(dist, "data/changelogs.json"), "utf8"));
    assert.equal(changelogs.length > 0, true, "changelog data is populated");
    assert.equal(changelogs[0].version, "1.0.6");
    assert.equal(changelogs[0].date, "2026-09-20");
    assert.match(changelogs[0].markdown, /server browser/i);

    const staticApply = await request(staticBase, "/apply/");
    const staticTeamForm = await request(staticBase, "/apply/team/");
    const staticBetaForm = await request(staticBase, "/apply/beta/");
    const staticThanks = await request(staticBase, "/apply/thanks/?ref=static-proof");
    const staticUpdates = await request(staticBase, "/updates/");
    const staticMedia = await request(staticBase, "/media/");
    assert.match(staticTeamForm.text, /data-supabase-url/);
    assert.match(staticBetaForm.text, /data-supabase-key/);
    // The /repo browser and its Gitea API client were removed; the changelog
    // error path must not link to that dead route any more.
    const updatesJs = fs.readFileSync(path.join(dist, "static/js/updates.js"), "utf8");
    assert.doesNotMatch(updatesJs, /\/repo/, "updates.js must not reference the removed /repo page");
    assert.match(
      updatesJs,
      /https:\/\/github\.com\/G-A-R-D-E-N\/Commonwealth-Online/,
      "updates.js recovery link must point at GitHub"
    );
    assert.doesNotMatch(staticTeamForm.text, /discord-membership/);
    assert.doesNotMatch(staticTeamForm.text, /\/api\/v1/);
  } finally {
    await closeServer(staticServer);
  }

  console.log(`static checks passed: ${htmlFiles.length} HTML pages`);
};

run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
