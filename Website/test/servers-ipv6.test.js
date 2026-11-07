"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const vm = require("node:vm");

const config = require("../src/config");
const { getServers } = require("../src/lib/servers");

const SOURCE = fs.readFileSync(path.join(__dirname, "..", "static", "js", "servers.js"), "utf8");

const HOSTS = [
  ["::1", "[::1]:7777"],
  ["[::1]", "[::1]:7777"],
  ["2001:db8::7", "[2001:db8::7]:7777"],
  ["8.8.4.4", "8.8.4.4:7777"],
  ["play.example.com", "play.example.com:7777"],
];

const element = () => ({
  innerHTML: "",
  textContent: "",
  hidden: true,
  attributes: {},
  dataset: {},
  classList: { toggle() {}, add() {}, remove() {} },
  setAttribute() {},
  removeAttribute() {},
  querySelectorAll() { return []; },
});

const renderBrowse = async (servers) => {
  const feed = element();
  const els = { "servers-status": element(), "servers-feed": feed, "servers-stats": element(), "stat-server-count": element(), "stat-server-updated": element() };
  const context = {
    document: { getElementById: (id) => els[id] },
    window: { matchMedia: () => ({ matches: true }), setTimeout },
    navigator: {},
    Intl, Date, Number, String, Array, Set, Promise, Error, Boolean,
    fetch: async () => ({ ok: true, status: 200, json: async () => ({ servers }) }),
  };
  vm.runInNewContext(SOURCE, context);
  await new Promise((resolve) => setImmediate(resolve));
  await new Promise((resolve) => setImmediate(resolve));
  return feed.innerHTML;
};

(async () => {
  const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "co-servers-ipv6-"));
  const original = config.paths.serversJson;
  try {
    const file = path.join(tempDir, "server.json");
    fs.writeFileSync(file, JSON.stringify({ servers: HOSTS.map(([host], i) => ({ id: `s-${i}`, name: `Server ${i}`, host, port: 7777 })) }));
    config.paths.serversJson = file;
    const { servers } = getServers();
    assert.deepEqual(servers.map((s) => s.address), HOSTS.map(([, address]) => address), "site API brackets IPv6 hosts");

    const html = await renderBrowse(HOSTS.map(([host], i) => ({ id: `b-${i}`, name: `Browse ${i}`, host, port: 7777 })));
    for (const [, address] of HOSTS) {
      assert.ok(html.includes(`>${address}<`), `Browse renders ${address}`);
      assert.ok(html.includes(`data-copy-address="${address}"`), `Browse copies ${address}`);
    }
    assert.doesNotMatch(html, /[^[]::1:7777/, "no unbracketed IPv6 endpoint is rendered");

    console.log("servers ipv6 tests passed");
  } finally {
    config.paths.serversJson = original;
    fs.rmSync(tempDir, { recursive: true, force: true });
  }
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
