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
  "account/index.html",
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
    assert.match(html, /href="\/static\/css\/widgetbot\.css"/);
    assert.match(html, /src="\/static\/js\/widgetbot\.js"/);
    assert.doesNotMatch(html, /@widgetbot\/crate/);
    assert.match(html, /script-src 'self' https:\/\/cdn\.jsdelivr\.net/);
    assert.match(html, /frame-src https:\/\/e\.widgetbot\.io/);
    assert.doesNotMatch(html, /script-src[^;]*unsafe-inline/);
    assert.doesNotMatch(html, /script-src[^;]*unsafe-eval/);
    assert.doesNotMatch(html, /frame-src[^;]*https:\/\/discord\.com/);
  }

  const staticServer = await startServer(serveStatic);
  const staticBase = `http://127.0.0.1:${staticServer.address().port}`;
  try {
    for (const route of ["/", "/media/", "/roadmap/", "/servers/", "/updates/", "/account/", "/apply/", "/forum/"]) {
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
    const staticAccount = await request(staticBase, "/account/");
    const staticTeamForm = await request(staticBase, "/apply/team/");
    const staticBetaForm = await request(staticBase, "/apply/beta/");
    const staticThanks = await request(staticBase, "/apply/thanks/?ref=static-proof");
    const staticUpdates = await request(staticBase, "/updates/");
    const staticMedia = await request(staticBase, "/media/");
    assert.match(staticTeamForm.text, /data-supabase-url/);
    assert.match(staticBetaForm.text, /data-supabase-key/);
    assert.match(staticAccount.text, /data-account/);
    assert.match(staticAccount.text, /data-account-nav[^>]*>Login \/ Sign Up<\/a>/);
    const supabaseConfigured =
      /data-supabase-url="[^"]+"/.test(staticAccount.text) &&
      /data-supabase-key="[^"]+"/.test(staticAccount.text);
    const supabaseClientCount = (staticAccount.text.match(/@supabase\/supabase-js@2\.105\.0/g) || []).length;
    assert.equal(supabaseClientCount, supabaseConfigured ? 1 : 0);
    assert.doesNotMatch(staticAccount.text, /supabase\.min\.js/);
    assert.match(staticAccount.text, /data-discord-link/);
    assert.doesNotMatch(staticAccount.text, /type="file"/);
    const profileIcons = [
      "armorer.png",
      "hacker.png",
      "rifleman.png",
      "medic.png",
      "scrapper.png",
      "cap_collector.png",
    ];
    for (const icon of profileIcons) {
      assert.ok(staticAccount.text.includes(`/assets/profile-icons/${icon}`), `missing real perk icon ${icon}`);
      const iconPath = path.join(dist, "assets/profile-icons", icon);
      assert.equal(fs.existsSync(iconPath), true, `missing fetched perk icon ${icon}`);
      assert.deepEqual([...fs.readFileSync(iconPath).subarray(0, 8)], [137, 80, 78, 71, 13, 10, 26, 10]);
    }
    assert.doesNotMatch(staticAccount.text, /profile-icons\/[^"]+\.svg/);
    const iconFetcher = fs.readFileSync(path.join(root, "scripts/fetch-profile-icons.js"), "utf8");
    assert.match(iconFetcher, /918547cc872c3288122f9d15ed0416cf33aa8bbf/);
    const accountJs = fs.readFileSync(path.join(dist, "static/js/account.js"), "utf8");
    const navbarJs = fs.readFileSync(path.join(dist, "static/js/navbar.js"), "utf8");
    const widgetbotJs = fs.readFileSync(path.join(dist, "static/js/widgetbot.js"), "utf8");
    assert.ok(widgetbotJs.includes("https://e.widgetbot.io/channels/"));
    assert.ok(widgetbotJs.includes("1512018618680999976"));
    assert.ok(widgetbotJs.includes("1512018620060794982"));
    assert.doesNotMatch(widgetbotJs, /\bCrate\b/);
    assert.doesNotMatch(widgetbotJs, /\beval\s*\(/);
    assert.doesNotMatch(widgetbotJs, /new Function\s*\(/);
    assert.match(navbarJs, /getSession\(\)/);
    assert.match(navbarJs, /onAuthStateChange/);
    assert.ok(navbarJs.includes('signedIn ? "Account" : "Login / Sign Up"'));
    assert.match(navbarJs, /window\.coSupabase/);
    assert.match(accountJs, /window\.coSupabase/);
    assert.match(accountJs, /linkIdentity/);
    assert.match(accountJs, /\/auth\/v1\/settings/);
    assert.match(accountJs, /settings\.external\?\.discord/);
    assert.match(accountJs, /settings\.disable_signup/);
    assert.match(accountJs, /Account services are temporarily unavailable\./);
    assert.ok(accountJs.includes("/assets/profile-icons/armorer.png"));
    assert.doesNotMatch(accountJs, /Fallout_Perk_Planner/);
    assert.ok(accountJs.includes('new URL(`${assetBase}/account/`, window.location.origin).href'));
    assert.doesNotMatch(accountJs, /new URL\("\\.", window\.location\.href\)/);
    assert.doesNotMatch(accountJs, /storage\.from/);
    const updatesJs = fs.readFileSync(path.join(dist, "static/js/updates.js"), "utf8");
    assert.doesNotMatch(updatesJs, /\/repo/, "updates.js must not reference the removed /repo page");
    assert.equal(fs.existsSync(path.join(dist, "repo/index.html")), false, "removed /repo page must not ship");
    assert.equal(fs.existsSync(path.join(dist, "static/js/repo.js")), false, "removed repo browser must not ship");
    assert.match(
      staticUpdates.text,
      /href="https:\/\/github\.com\/G-A-R-D-E-N\/Commonwealth-Online"[^>]*>Repository</,
      "Repository footer link must target the public GitHub repository"
    );
    const linksJs = fs.readFileSync(path.join(dist, "static/js/links.js"), "utf8");
    assert.match(
      linksJs,
      /repository: "https:\/\/github\.com\/G-A-R-D-E-N\/Commonwealth-Online"/,
      "Repository link registry must target the public GitHub repository"
    );
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
