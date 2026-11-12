"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const SOURCE = fs.readFileSync(path.join(__dirname, "..", "static", "js", "servers.js"), "utf8");
const DIRECTORY = "https://oeajjfquvmdxqqtiotfa.supabase.co/functions/v1/server-directory";

const element = () => ({
  innerHTML: "",
  textContent: "",
  hidden: true,
  attributes: {},
  dataset: {},
  classList: { toggle() {}, add() {}, remove() {} },
  setAttribute(k, v) { this.attributes[k] = v; },
  removeAttribute(k) { delete this.attributes[k]; },
  querySelectorAll() { return []; },
});

const run = async (responses) => {
  const els = { "servers-status": element(), "servers-feed": element(), "servers-stats": element(), "stat-server-count": element(), "stat-server-updated": element() };
  const requested = [];
  els["servers-feed"].dataset.directoryUrl = DIRECTORY;
  const context = {
    document: { getElementById: (id) => els[id] },
    window: { matchMedia: () => ({ matches: true }), setTimeout },
    navigator: {},
    Intl, Date, Number, String, Array, Set, Promise, Error, Boolean, AbortSignal,
    fetch: async (url) => {
      requested.push(url);
      const r = responses[url];
      if (!r) throw new Error("network down");
      return { ok: r.status === 200, status: r.status, json: async () => r.body };
    },
  };
  vm.runInNewContext(SOURCE, context);
  await new Promise((resolve) => setImmediate(resolve));
  await new Promise((resolve) => setImmediate(resolve));
  return { els, requested };
};

const liveServer = { id: "live-1", name: "Live One", host: "8.8.4.4", port: 7777, players: 3, maxPlayers: 8, live: true };
const curatedServer = { id: "c-1", name: "Curated", host: "9.9.9.9", port: 7777 };

(async () => {
  {
    const { els, requested } = await run({
      [DIRECTORY]: { status: 200, body: { version: 2, updatedAt: "2026-09-23T00:00:00Z", servers: [liveServer] } },
      "/api/v1/servers": { status: 200, body: { servers: [curatedServer, { ...curatedServer, id: "dup", host: "8.8.4.4" }] } },
    });
    assert.ok(requested.includes(DIRECTORY), "Browse reads the directory URL from the page");
    const html = els["servers-feed"].innerHTML;
    assert.match(html, /Live One/);
    assert.match(html, /3\/8 online/);
    assert.match(html, /Curated/);
    assert.equal((html.match(/8\.8\.4\.4:7777/g) || []).length, 2, "duplicate curated endpoint is dropped (address + copy button)");
    assert.equal(els["stat-server-count"].textContent, "2");
  }
  {
    const { els, requested } = await run({ "/api/v1/servers": { status: 200, body: { servers: [curatedServer] } } });
    assert.ok(requested.includes(DIRECTORY), "the directory is read even when the curated list is the only one that answers");
    assert.match(els["servers-feed"].innerHTML, /Curated/, "curated list renders when the directory is down");
    assert.equal(els["servers-status"].textContent, "1 public server listed", "directory outage shows no warning");
  }
  {
    const { els } = await run({});
    assert.match(els["servers-feed"].innerHTML, /Server list unavailable/, "both sources failing is reported as unavailable");
    assert.equal(els["servers-status"].textContent, "Server list unavailable");
  }
  {
    const { els } = await run({
      [DIRECTORY]: { status: 200, body: { servers: [liveServer] } },
      "/api/v1/servers": { status: 503, body: {} },
    });
    assert.match(els["servers-feed"].innerHTML, /Live One/, "a broken curated list still shows live servers");
    assert.doesNotMatch(els["servers-feed"].innerHTML, /Server list unavailable/);
  }
  {
    const { els } = await run({ [DIRECTORY]: { status: 200, body: { servers: [] } }, "/api/v1/servers": { status: 503, body: {} } });
    assert.match(els["servers-feed"].innerHTML, /Server list unavailable/, "an empty directory does not hide a broken curated list");
    assert.doesNotMatch(els["servers-feed"].innerHTML, /No public servers are online/);
  }
  {
    const { els } = await run({ [DIRECTORY]: { status: 200, body: { servers: [] } }, "/api/v1/servers": { status: 200, body: { servers: [] } } });
    assert.match(els["servers-feed"].innerHTML, /No public servers are online/, "a genuinely empty list is still an empty list");
  }
  {
    const { els } = await run({ [DIRECTORY]: { status: 503, body: {} } });
    assert.match(els["servers-feed"].innerHTML, /Server list unavailable/);
  }
  {
    const { els } = await run({ [DIRECTORY]: { status: 200, body: { servers: [{ name: "<img src=x>", host: "8.8.4.4", port: 7777 }] } } });
    assert.ok(!els["servers-feed"].innerHTML.includes("<img"), "listing names are escaped");
  }
  {
    const { els } = await run({ [DIRECTORY]: { status: 200, body: { servers: [{ ...liveServer, discord: "https://discord.gg/evil", modlist: "Mods: evil" }] } } });
    assert.doesNotMatch(els["servers-feed"].innerHTML, /discord\.gg\/evil|Mods: evil|Mod notes|>Discord</, "live entries cannot inject discord or mod notes");
  }
  {
    const official = { id: "official", name: "Official", host: "195.26.244.234", port: 7777, region: "North America", tags: ["official", "usa"] };
    const { els } = await run({
      [DIRECTORY]: { status: 200, body: { servers: [{ ...liveServer, host: "195.26.244.234", players: 0, maxPlayers: 100, tags: ["fake"], region: "Mars" }] } },
      "/api/v1/servers": { status: 200, body: { servers: [official] } },
    });
    const html = els["servers-feed"].innerHTML;
    assert.match(html, /0\/100 online/, "live count is shown for a curated endpoint");
    assert.match(html, />North America</, "curated region is kept on the live entry");
    assert.match(html, />official</, "curated tags are kept on the live entry");
    assert.doesNotMatch(html, /Mars|>fake</, "live region and tags do not override the curated ones");
  }
  console.log("servers browse tests passed");
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
