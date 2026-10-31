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

const run = async (responses, { directoryEnabled = false } = {}) => {
  const els = { "servers-status": element(), "servers-feed": element(), "servers-stats": element(), "stat-server-count": element(), "stat-server-updated": element() };
  const requested = [];
  els["servers-feed"].dataset.directoryEnabled = String(directoryEnabled);
  const context = {
    document: { getElementById: (id) => els[id] },
    window: { matchMedia: () => ({ matches: true }), setTimeout },
    navigator: {},
    Intl, Date, Number, String, Array, Set, Promise, Error, Boolean,
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
    }, { directoryEnabled: true });
    assert.ok(requested.includes(DIRECTORY), "Browse reads the Supabase directory");
    const html = els["servers-feed"].innerHTML;
    assert.match(html, /Live One/);
    assert.match(html, /3\/8 online/);
    assert.match(html, /Curated/);
    assert.equal((html.match(/8\.8\.4\.4:7777/g) || []).length, 2, "duplicate curated endpoint is dropped (address + copy button)");
    assert.equal(els["stat-server-count"].textContent, "2");
  }
  {
    const { els, requested } = await run({ "/api/v1/servers": { status: 200, body: { servers: [curatedServer] } } });
    assert.ok(!requested.includes(DIRECTORY), "directory is not fetched while disabled");
    assert.match(els["servers-feed"].innerHTML, /Curated/, "curated list renders while the directory is disabled");
    assert.equal(els["servers-status"].textContent, "1 public server listed");
  }
  {
    const { els } = await run({});
    assert.match(els["servers-feed"].innerHTML, /Could not reach the server list/, "curated failure is reported while the directory is disabled");
  }
  {
    const { els } = await run({ "/api/v1/servers": { status: 200, body: { servers: [curatedServer] } } }, { directoryEnabled: true });
    assert.match(els["servers-feed"].innerHTML, /Curated/, "directory outage still shows curated servers");
    assert.equal(els["servers-status"].textContent, "1 public server listed", "directory outage shows no warning");
  }
  {
    const { els } = await run({ [DIRECTORY]: { status: 503, body: {} } }, { directoryEnabled: true });
    assert.match(els["servers-feed"].innerHTML, /Could not reach the server list/);
  }
  {
    const { els } = await run({ [DIRECTORY]: { status: 200, body: { servers: [{ name: "<img src=x>", host: "8.8.4.4", port: 7777 }] } } }, { directoryEnabled: true });
    assert.ok(!els["servers-feed"].innerHTML.includes("<img"), "listing names are escaped");
  }
  {
    const { els } = await run({ [DIRECTORY]: { status: 200, body: { servers: [{ ...liveServer, discord: "https://discord.gg/evil", modlist: "Mods: evil" }] } } }, { directoryEnabled: true });
    assert.doesNotMatch(els["servers-feed"].innerHTML, /discord\.gg\/evil|Mods: evil|Mod notes|>Discord</, "live entries cannot inject discord or mod notes");
  }
  console.log("servers browse tests passed");
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
