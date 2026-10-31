const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

function loadServerList() {
  const sandbox = { console };
  sandbox.window = sandbox;
  vm.runInContext(
    fs.readFileSync(path.join(__dirname, "..", "components", "server-list.js"), "utf8"),
    vm.createContext(sandbox)
  );
  return sandbox.CO_ServerList;
}

function renderRows(servers, activeTab) {
  const list = { innerHTML: "", scrollHeight: 0, clientHeight: 0 };
  const wrap = { className: "", querySelector: () => null };
  const root = {
    querySelector: (selector) => (selector === ".co-server-list-wrap" ? wrap : selector === ".co-server-list" ? list : null),
  };
  loadServerList().render(root, { servers, activeTab, selectedServerIndex: 0, focusZone: "" });
  return list.innerHTML;
}

const payload = '<img src=x onerror="alert(1)">';

test("player counts from a directory row are escaped", () => {
  const html = renderRows([{ id: "a", name: "A", players: payload, maxPlayers: 8 }], "server-browser");
  assert.equal(html.includes("<img"), false);
  assert.equal(html.includes("&lt;img src=x"), true);
});

test("local rows escape players and max players", () => {
  const html = renderRows([{ id: "l", name: "L", host: "127.0.0.1", players: 1, maxPlayers: payload }], "local");
  assert.equal(html.includes("<img"), false);
  assert.equal(html.includes("1/&lt;img"), true);
});

test("numeric counts and pings render unchanged", () => {
  const html = renderRows([{ id: "a", name: "A", players: 3, maxPlayers: 16, ping: 42 }], "server-browser");
  assert.equal(html.includes('<span class="co-capacity-val">3/16</span>'), true);
  assert.equal(html.includes("42 ms"), true);
});
