const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { createEnvironment, createElement } = require("./dom.js");
const browserPath = path.join(__dirname, "..");

function loadServerList() {
  const sandbox = { console };
  sandbox.window = sandbox;
  const context = vm.createContext(sandbox);
  for (const name of ["server-records", "server-list"]) {
    vm.runInContext(
      fs.readFileSync(path.join(__dirname, "..", "components", name + ".js"), "utf8"),
      context
    );
  }
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

test("only an entry whose own check is unfinished shows CHECKING", () => {
  const checking = { id: "a", name: "A", host: "203.0.113.10", port: 7777, reachability: "checking" };
  const html = renderRows([checking, noAnswer], "server-browser");
  assert.equal(html.includes('class="co-server-state co-server-state--checking"'), true);
  assert.equal(html.includes(">CHECKING<"), true);
  assert.equal(html.includes(">NO ANSWER<"), true);
});

test("a refresh flag alone does not put CHECKING over an entry that has a result", () => {
  const html = renderRows([answering, noAnswer, unchecked], "server-browser", { isRefreshing: true });
  assert.equal(html.includes("CHECKING"), false);
  assert.equal(html.includes(">ANSWERING 42 ms<"), true);
  assert.equal(html.includes(">NO ANSWER<"), true);
  assert.equal(html.includes(">NOT CHECKED<"), true);
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

test("the details status follows the entry's own check state, not the refresh flag", () => {
  const checking = Object.assign({}, answering, { reachability: "checking", reachabilityMs: 0 });
  assert.match(renderDetails(checking, { isRefreshing: true }), /> CHECKING</);
  assert.match(renderDetails(answering, { isRefreshing: true }), /> ANSWERING</);
  assert.match(renderDetails(noAnswer, { isRefreshing: true }), /> NO ANSWER</);
});

const BROWSER_COMPONENTS = [
  "tabs", "filters", "controller-input", "text-focus", "keyboard", "controller-trace", "controller-icons",
  "controller-prompts", "direct-connect", "steam-lan", "steam-friends", "server-settings", "browser-markup",
  "server-records", "recent-servers", "filter-state", "server-counts", "join-flow", "password-prompt",
  "share-log", "modpack-detail", "modpack-copy", "controller-navigation", "directory-refresh", "mock-data",
  "server-list", "server-details"
];

function loadBrowserWithList() {
  const { window, document } = createEnvironment();
  const mount = createElement("div");
  mount.ownerDocument = document;
  mount.parentNode = document.body;
  document.body.children.push(mount);
  document.hasFocus = () => true;
  window.CO_ControllerGlyphs = { platform: "Xbox", setPlatform() {}, hydrate() {}, render(key) { return "pad:" + key; } };
  const calls = [];
  window.coAction = (value) => calls.push(JSON.parse(value));
  const context = vm.createContext(window);
  for (const name of BROWSER_COMPONENTS) {
    const file = path.join(browserPath, "components", name + ".js");
    if (fs.existsSync(file)) vm.runInContext(fs.readFileSync(file, "utf8"), context);
  }
  vm.runInContext(fs.readFileSync(path.join(browserPath, "browser.js"), "utf8"), context);
  const browser = window.CO_Browser.create({ mount, layout: "embedded", exposeGlobals: true, onBack() {} });
  browser.show();
  return { window, document, mount, browser, state: window.CO_App.getState(), calls };
}

function activeList(env) {
  const pane = env.mount.querySelector('.co-tab-pane[data-pane="server-browser"]');
  return pane && pane.querySelector(".co-server-list");
}

function rowAt(env, index) {
  const list = activeList(env);
  return list && list.querySelector('.co-server-row[data-index="' + index + '"]');
}

function badgeAt(env, index) {
  const row = rowAt(env, index);
  return row && row.querySelector(".co-server-state");
}

function checkingPair() {
  return [
    { id: "a", name: "Alpha", host: "203.0.113.10", port: 7777, reachability: "checking" },
    { id: "b", name: "Beta", host: "198.51.100.7", port: 7777, reachability: "checking" }
  ];
}

test("a per-entry result changes only the matching entry", () => {
  const env = loadBrowserWithList();
  env.browser.dispatchCoEvent({ type: "serverListUpdated", servers: checkingPair() });
  const betaRow = rowAt(env, 1);
  const betaBadge = badgeAt(env, 1);
  env.browser.dispatchCoEvent({
    type: "serverReachabilityUpdated",
    updates: [{ host: "203.0.113.10", port: 7777, state: "answering", milliseconds: 42 }]
  });
  const alphaBadge = badgeAt(env, 0);
  assert.equal(alphaBadge.className, "co-server-state co-server-state--answering");
  assert.equal(alphaBadge.getAttribute("title"), "This server completed a connection handshake");
  assert.equal(badgeAt(env, 1), betaBadge);
  assert.equal(betaBadge.className, "co-server-state co-server-state--checking");
  assert.equal(rowAt(env, 1), betaRow);
  assert.equal(env.state.allServers[0].reachability, "answering");
  assert.equal(env.state.allServers[0].reachabilityMs, 42);
  assert.equal(env.state.allServers[1].reachability, "checking");
});

test("a batch applies every known update and drops an unknown address", () => {
  const env = loadBrowserWithList();
  env.browser.dispatchCoEvent({ type: "serverListUpdated", servers: checkingPair() });
  env.browser.dispatchCoEvent({
    type: "serverReachabilityUpdated",
    updates: [
      { host: "203.0.113.10", port: 7777, state: "answering", milliseconds: 42 },
      { host: "192.0.2.55", port: 7777, state: "no_answer", reason: "stale refresh result" },
      { host: "198.51.100.7", port: 7777, state: "no_answer", reason: "connection timed out" }
    ]
  });
  assert.equal(env.state.allServers.length, 2);
  assert.equal(env.state.allServers[0].reachability, "answering");
  assert.equal(env.state.allServers[1].reachability, "no_answer");
  assert.equal(env.state.allServers[1].reachabilityReason, "connection timed out");
  assert.equal(badgeAt(env, 0).className, "co-server-state co-server-state--answering");
  assert.equal(badgeAt(env, 1).className, "co-server-state co-server-state--no-answer");
  assert.equal(badgeAt(env, 1).getAttribute("title"), "connection timed out");
});

test("a batch with no known address leaves the state and the rows untouched", () => {
  const env = loadBrowserWithList();
  env.browser.dispatchCoEvent({ type: "serverListUpdated", servers: checkingPair() });
  const before = JSON.stringify(env.state.allServers);
  const alphaRow = rowAt(env, 0);
  const betaRow = rowAt(env, 1);
  const changed = env.window.CO_ServerList.applyReachabilityUpdates(env.mount, env.state, {
    type: "serverReachabilityUpdated",
    updates: [{ host: "192.0.2.55", port: 7787, state: "answering", milliseconds: 5 }]
  });
  assert.equal(changed, false);
  assert.equal(JSON.stringify(env.state.allServers), before);
  assert.equal(rowAt(env, 0), alphaRow);
  assert.equal(rowAt(env, 1), betaRow);
  assert.equal(badgeAt(env, 0).className, "co-server-state co-server-state--checking");
  assert.equal(badgeAt(env, 1).className, "co-server-state co-server-state--checking");
});

test("selection, focus, filters and scroll survive reachability updates", () => {
  const env = loadBrowserWithList();
  env.browser.dispatchCoEvent({ type: "serverListUpdated", servers: checkingPair() });
  env.state.selectedServerIndex = 1;
  env.state.focusZone = "server-list";
  env.state.filters = Object.assign({}, env.state.filters, { search: "" });
  env.browser.render();
  const list = activeList(env);
  list.scrollTop = 120;
  const betaRow = rowAt(env, 1);
  const filtersBefore = JSON.stringify(env.state.filters);
  env.browser.dispatchCoEvent({
    type: "serverReachabilityUpdated",
    updates: [{ host: "203.0.113.10", port: 7777, state: "answering", milliseconds: 17 }]
  });
  assert.equal(env.state.selectedServerIndex, 1);
  assert.equal(env.state.activeTab, "server-browser");
  assert.equal(env.state.focusZone, "server-list");
  assert.equal(JSON.stringify(env.state.filters), filtersBefore);
  assert.equal(list.scrollTop, 120);
  assert.equal(rowAt(env, 1), betaRow);
  assert.equal(betaRow.classList.contains("selected"), true);
  assert.equal(rowAt(env, 0).classList.contains("selected"), false);
  assert.equal(badgeAt(env, 0).className, "co-server-state co-server-state--answering");
  assert.equal(badgeAt(env, 0).getAttribute("title"), "This server completed a connection handshake");
});

function tabList(env, tab) {
  const pane = env.mount.querySelector('.co-tab-pane[data-pane="' + tab + '"]');
  return pane && pane.querySelector(".co-server-list");
}

function tabRow(env, tab, index) {
  const list = tabList(env, tab);
  return list && list.querySelector('.co-server-row[data-index="' + index + '"]');
}

function tabBadge(env, tab, index) {
  const row = tabRow(env, tab, index);
  return row && row.querySelector(".co-server-state");
}

test("one result updates every matching entry in every list, not only the first", () => {
  const sandbox = loadServerList();
  const first = { id: "a", name: "A", host: "203.0.113.10", port: 7777, reachability: "checking" };
  const second = { id: "a-copy", name: "A copy", host: "203.0.113.10", port: 7777, reachability: "checking" };
  const otherPort = { id: "p", name: "A other port", host: "203.0.113.10", port: 7778, reachability: "checking" };
  const recent = { id: "r", name: "A recent", host: "203.0.113.10", port: 7777 };
  const local = { id: "l", name: "A local", host: "203.0.113.10", port: 7777, reachability: "checking" };
  const state = {
    activeTab: "server-browser",
    allServers: [first, second, otherPort],
    servers: [first, second, otherPort],
    favorites: [],
    recent: [recent],
    localServers: [local],
    selectedServerIndex: 0
  };
  const applied = sandbox.CO_ServerList.applyReachabilityUpdates({ querySelector: () => null }, state, {
    updates: [{ host: "203.0.113.10", port: 7777, state: "answering", milliseconds: 31 }]
  });
  assert.equal(applied, true);
  [first, second, recent, local].forEach((entry) => {
    assert.equal(entry.reachability, "answering");
    assert.equal(entry.reachabilityMs, 31);
  });
  assert.equal(otherPort.reachability, "checking");
});

test("a result repaints a Favorites row in place when the favorite is its own object", () => {
  const env = loadBrowserWithList();
  env.browser.dispatchCoEvent({ type: "serverListUpdated", servers: checkingPair() });
  const favorite = Object.assign({}, env.state.allServers[0], { favorite: true });
  env.state.favorites = [favorite];
  env.state.selectedServerIndex = 0;
  env.state.activeTab = "favorites";
  env.browser.render();
  const row = tabRow(env, "favorites", 0);
  assert.equal(tabBadge(env, "favorites", 0).className, "co-server-state co-server-state--checking");
  env.browser.dispatchCoEvent({
    type: "serverReachabilityUpdated",
    updates: [{ host: "203.0.113.10", port: 7777, state: "answering", milliseconds: 9 }]
  });
  assert.equal(tabRow(env, "favorites", 0), row);
  assert.equal(tabBadge(env, "favorites", 0).className, "co-server-state co-server-state--answering");
  assert.equal(env.state.allServers[0].reachability, "answering");
  assert.equal(favorite.reachability, "answering");
  assert.equal(favorite.reachabilityMs, 9);
  assert.equal(env.state.activeTab, "favorites");
  assert.equal(env.state.selectedServerIndex, 0);
});

test("a result reaches a separate Recent record and its details without a full render", () => {
  const env = loadBrowserWithList();
  env.browser.dispatchCoEvent({ type: "serverListUpdated", servers: checkingPair() });
  const recent = { id: "r", name: "Alpha recent", host: "203.0.113.10", port: 7777 };
  env.state.recent = [recent];
  env.state.selectedServerIndex = 0;
  env.state.activeTab = "recent";
  env.browser.render();
  const pane = env.mount.querySelector('.co-tab-pane[data-pane="recent"]');
  const preview = pane.querySelector(".co-preview-area");
  const row = tabRow(env, "recent", 0);
  assert.match(preview.innerHTML, /REACHABILITY: NOT CHECKED/);
  env.browser.dispatchCoEvent({
    type: "serverReachabilityUpdated",
    updates: [{ host: "203.0.113.10", port: 7777, state: "answering", milliseconds: 15 }]
  });
  assert.equal(env.state.allServers[0].reachability, "answering");
  assert.equal(recent.reachability, "answering");
  assert.equal(tabRow(env, "recent", 0), row);
  assert.match(preview.innerHTML, /REACHABILITY: ANSWERING in 15 ms/);
});

test("an IPv6 address matches across bracketed and unbracketed spellings", () => {
  const env = loadBrowserWithList();
  env.browser.dispatchCoEvent({
    type: "serverListUpdated",
    servers: [
      { id: "v6a", name: "Bracketed", host: "[2001:db8::1]", port: 7777, reachability: "checking" },
      { id: "v6b", name: "Bare", host: "2001:db8::2", port: 7777, reachability: "checking" }
    ]
  });
  assert.equal(
    env.window.CO_ServerList.addressKey({ host: "[2001:db8::1]", port: 7777 }),
    env.window.CO_ServerList.addressKey({ host: "2001:db8::1", port: 7777 })
  );
  env.browser.dispatchCoEvent({
    type: "serverReachabilityUpdated",
    updates: [{ host: "2001:db8::1", port: 7777, state: "answering", milliseconds: 8 }]
  });
  assert.equal(env.state.allServers[0].reachability, "answering");
  assert.equal(badgeAt(env, 0).className, "co-server-state co-server-state--answering");
  assert.equal(badgeAt(env, 1).className, "co-server-state co-server-state--checking");
  env.browser.dispatchCoEvent({
    type: "serverReachabilityUpdated",
    updates: [{ host: "[2001:db8::2]", port: 7777, state: "no_answer", reason: "connection timed out" }]
  });
  assert.equal(env.state.allServers[1].reachability, "no_answer");
  assert.equal(env.state.allServers[1].reachabilityReason, "connection timed out");
  assert.equal(badgeAt(env, 1).className, "co-server-state co-server-state--no-answer");
});

test("address keys follow the same rules as the C++ key", () => {
  const key = loadServerList().CO_ServerList.addressKey;
  assert.equal(key({ host: " [2001:DB8::1] ", port: 0 }), "2001:db8::1:7777");
  assert.equal(key({ host: "[ ::1 ]", port: 7777 }), "::1:7777");
  assert.equal(key({ host: "[]", port: 7777 }), "[]:7777");
  assert.equal(key({ host: "Example.COM", port: 7777 }), "example.com:7777");
});

test("player counts show full servers and hide counts for unreachable servers", () => {
  const html = renderRows([
    { id: "a", name: "Open", host: "10.0.0.1", port: 7777, players: 3, maxPlayers: 16, reachability: "answering" },
    { id: "b", name: "Full", host: "10.0.0.2", port: 7777, players: 16, maxPlayers: 16, reachability: "answering" },
    { id: "c", name: "Dark", host: "10.0.0.3", port: 7777, players: 5, maxPlayers: 16, reachability: "no_answer" },
  ], "server-browser");
  assert.match(html, /co-server-capacity"><span class="co-capacity-val">3\/16</);
  assert.match(html, /co-server-capacity co-capacity-full" title="Server is full"><span class="co-capacity-val">16\/16</);
  assert.match(html, /co-capacity-val">\u2014\/16</);
  assert.doesNotMatch(html, /co-capacity-val">5\/16</);
});
