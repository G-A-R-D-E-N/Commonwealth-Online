"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const http = require("node:http");
const os = require("node:os");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const dist = path.join(root, "dist");
const expectedPages = [
  "index.html",
  "media/index.html",
  "roadmap/index.html",
  "servers/index.html",
  "updates/index.html",
  "repo/index.html",
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
  }

  const staticServer = await startServer(serveStatic);
  const databaseDirectory = fs.mkdtempSync(path.join(os.tmpdir(), "commonwealth-online-static-"));
  process.env.DATABASE_PATH = path.join(databaseDirectory, "test.sqlite");
  process.env.ADMIN_TOKEN = "";
  const { createApp } = require("../src/app");
  const { closeDb } = require("../src/db");
  const expressServer = await new Promise((resolve) => {
    const server = createApp().listen(0, "127.0.0.1", () => resolve(server));
  });
  const staticBase = `http://127.0.0.1:${staticServer.address().port}`;
  const expressBase = `http://127.0.0.1:${expressServer.address().port}`;

  try {
    const parity = [
      ["/", "/"],
      ["/media/", "/media"],
      ["/roadmap/", "/roadmap"],
      ["/servers/", "/servers"],
      ["/updates/", "/updates"],
      ["/repo/", "/repo"],
      ["/apply/", "/apply"],
      ["/forum/", "/forum"],
    ];
    for (const [staticRoute, expressRoute] of parity) {
      const staticPage = await request(staticBase, staticRoute);
      const expressPage = await request(expressBase, expressRoute);
      assert.equal(staticPage.response.status, 200, `static ${staticRoute}`);
      assert.equal(expressPage.response.status, 200, `Express ${expressRoute}`);
      assert.equal(staticPage.text, expressPage.text, `HTML parity for ${staticRoute}`);
    }

    const staticData = JSON.parse(fs.readFileSync(path.join(dist, "data/servers.json"), "utf8"));
    const apiServers = await request(expressBase, "/api/v1/servers");
    const expressData = JSON.parse(apiServers.text);
    assert.equal(expressData.version, staticData.version, "server version parity");
    assert.equal(expressData.updatedAt, staticData.updatedAt, "server timestamp parity");
    assert.equal(expressData.servers.length, staticData.servers.length, "server count parity");
    for (const [index, server] of expressData.servers.entries()) {
      const source = staticData.servers[index];
      assert.deepEqual(
        { ...server, address: undefined },
        { ...source, address: undefined },
        `server source parity for index ${index}`
      );
      assert.equal(server.address, `${source.host}:${source.port}`, `derived address for index ${index}`);
    }
    assert.equal(expressData.count, expressData.servers.length, "Express API count metadata");

    const staticServers = await request(staticBase, "/data/servers.json");
    const expressServers = await request(expressBase, "/data/servers.json");
    assert.equal(staticServers.text, expressServers.text, "raw server JSON parity");

    const staticApply = await request(staticBase, "/apply/");
    const staticTeamForm = await request(staticBase, "/apply/team/");
    const staticBetaForm = await request(staticBase, "/apply/beta/");
    const staticThanks = await request(staticBase, "/apply/thanks/?ref=static-proof");
    assert.equal(staticTeamForm.response.status, 200);
    assert.equal(staticBetaForm.response.status, 200);
    assert.equal(staticThanks.response.status, 200);
    assert.match(staticTeamForm.text, /data-supabase-url/);
    assert.match(staticBetaForm.text, /data-supabase-key/);
    assert.match(staticThanks.text, /data-application-reference/);
    assert.match(staticApply.text, /href="\/apply\/team"/);
    assert.match(staticApply.text, /href="\/apply\/beta"/);

    const teamForm = await request(expressBase, "/apply/team");
    const betaForm = await request(expressBase, "/apply/beta");
    const thanks = await request(expressBase, "/apply/thanks?ref=static-proof");
    assert.match(teamForm.text, /<form/);
    assert.match(betaForm.text, /<form/);
    assert.match(thanks.text, /static-proof/);

    const applicationsApi = await request(expressBase, "/api/v1/applications");
    assert.equal(applicationsApi.response.status, 503);
    assert.match(applicationsApi.text, /admin_token_not_configured/);

    const forumApi = await request(expressBase, "/api/v1/forum/categories");
    assert.equal(forumApi.response.status, 200);
    assert.match(forumApi.text, /categories/);
  } finally {
    await closeServer(expressServer);
    closeDb();
    await closeServer(staticServer);
    fs.rmSync(databaseDirectory, { recursive: true, force: true });
  }

  console.log(`static checks passed: ${htmlFiles.length} HTML pages`);
};

run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
