const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

function loadServerList() {
  const sandbox = { console };
  sandbox.window = sandbox;
  const context = vm.createContext(sandbox);
  vm.runInContext(
    fs.readFileSync(path.join(__dirname, "..", "components", "server-list.js"), "utf8"),
    context
  );
  sandbox.context = context;
  return sandbox;
}

function renderRows(servers, activeTab, extraState) {
  const list = { innerHTML: "", scrollHeight: 0, clientHeight: 0 };
  const wrap = { className: "", querySelector: () => null };
  const root = {
    querySelector: (selector) => (selector === ".co-server-list-wrap" ? wrap : selector === ".co-server-list" ? list : null),
  };
  const state = Object.assign({ servers, activeTab, selectedServerIndex: 0, focusZone: "" }, extraState || {});
  loadServerList().CO_ServerList.render(root, state);
  return list.innerHTML;
}

function renderDetails(server, extraState) {
  const sandbox = loadServerList();
  vm.runInContext(
    fs.readFileSync(path.join(__dirname, "..", "components", "server-details.js"), "utf8"),
    sandbox.context
  );
  const area = { innerHTML: "", classList: { add() {}, remove() {} } };
  const pane = { querySelector: (selector) => (selector === ".co-preview-area" ? area : null) };
  const root = {
    querySelector: (selector) => (selector === '.co-tab-pane[data-pane="server-browser"]' ? pane : null),
  };
  const state = Object.assign({ activeTab: "server-browser", servers: [server], selectedServerIndex: 0 }, extraState || {});
  sandbox.CO_ServerDetails.render(root, state);
  return area.innerHTML;
}

const payload = '<img src=x onerror="alert(1)">';
const answering = { id: "a", name: "A", host: "203.0.113.10", port: 7777, reachability: "answering", reachabilityMs: 42 };
const noAnswer = { id: "b", name: "B", host: "198.51.100.7", port: 7777, reachability: "no_answer", reachabilityReason: "connection timed out" };
const unchecked = { id: "c", name: "C", host: "198.51.100.8", port: 7777, reachability: "unchecked", reachabilityReason: "the running plugin cannot check servers" };

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

test("a refresh marks entries as CHECKING until the answer is measured", () => {
  const html = renderRows([answering, noAnswer], "server-browser", { isRefreshing: true });
  assert.equal(html.includes('class="co-server-state co-server-state--checking"'), true);
  assert.equal(html.includes(">CHECKING<"), true);
  assert.equal(html.includes("ANSWERING"), false);
  assert.equal(html.includes("NO ANSWER"), false);
});

test("an answering entry shows ANSWERING with the measured time", () => {
  const html = renderRows([answering], "server-browser");
  assert.equal(html.includes('class="co-server-state co-server-state--answering"'), true);
  assert.equal(html.includes(">ANSWERING 42 ms<"), true);
});

test("an entry that did not answer is marked NO ANSWER and stays in the list", () => {
  const html = renderRows([noAnswer], "server-browser");
  assert.equal(html.includes('class="co-server-state co-server-state--no-answer"'), true);
  assert.equal(html.includes(">NO ANSWER<"), true);
  assert.equal(html.includes('data-id="b"'), true);
  assert.equal(html.includes("connection timed out"), true);
});

test("NOT CHECKED only appears when no check could run, with its reason", () => {
  const html = renderRows([unchecked], "server-browser");
  assert.equal(html.includes('class="co-server-state co-server-state--unchecked"'), true);
  assert.equal(html.includes(">NOT CHECKED<"), true);
  assert.equal(html.includes("the running plugin cannot check servers"), true);
});

test("an entry without a measured state is NOT CHECKED instead of claiming an answer", () => {
  const html = renderRows([{ id: "d", name: "D", host: "198.51.100.9", port: 7777 }], "server-browser");
  assert.equal(html.includes(">NOT CHECKED<"), true);
  assert.equal(html.includes("ANSWERING"), false);
  assert.equal(html.includes("LISTED"), false);
  assert.equal(html.includes("UNVERIFIED"), false);
});

test("an entry without an address carries no state badge", () => {
  const html = renderRows([{ id: "e", name: "E" }], "server-browser");
  assert.equal(html.includes("co-server-state"), false);
});

test("the details pane reports the measured answer time", () => {
  const html = renderDetails(answering);
  assert.match(html, /REACHABILITY: ANSWERING in 42 ms/);
  assert.match(html, /connection handshake completed, then closed/);
  assert.match(html, /> ANSWERING</);
});

test("the details pane reports a no-answer reason escaped", () => {
  const html = renderDetails(Object.assign({}, noAnswer, { reachabilityReason: payload }));
  assert.match(html, /REACHABILITY: NO ANSWER - /);
  assert.equal(html.includes("<img"), false);
  assert.equal(html.includes("&lt;img"), true);
  assert.match(html, /> NO ANSWER</);
});

test("the details pane says why a check could not run and keeps the directory heartbeat", () => {
  const html = renderDetails(Object.assign({}, unchecked, {
    live: true,
    lastSeen: new Date(Date.now() - 42000).toISOString()
  }));
  assert.match(html, /REACHABILITY: NOT CHECKED - the running plugin cannot check servers/);
  assert.match(html, /LISTING: live directory heartbeat 4[0-9]s ago/);
  assert.match(html, /> NOT CHECKED</);
});

test("a website entry is named as one in the listing line", () => {
  const html = renderDetails(Object.assign({}, answering, { live: false }));
  assert.match(html, /LISTING: website entry, no directory heartbeat/);
});

test("a refresh marks the details status CHECKING", () => {
  const html = renderDetails(answering, { isRefreshing: true });
  assert.match(html, /> CHECKING</);
});
