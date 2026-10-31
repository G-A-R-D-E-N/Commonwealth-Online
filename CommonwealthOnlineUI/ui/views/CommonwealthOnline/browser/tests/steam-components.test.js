const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { createEnvironment, createElement } = require("./dom.js");

function loadSteamComponents() {
  const { window, document } = createEnvironment();
  const context = vm.createContext(window);
  for (const name of ["steam-lan", "steam-friends"]) {
    const file = path.join(__dirname, "..", "components", name + ".js");
    vm.runInContext(fs.readFileSync(file, "utf8"), context);
  }
  return { window, document };
}

function copyRoot(text) {
  const el = createElement("code");
  el.textContent = text;
  return { el, root: { querySelector: (selector) => (selector === "#co-steam-host-code" ? el : null) } };
}

function stubSelection(window, document) {
  const selection = { added: [] };
  document.createRange = () => {
    const range = { node: null, selectNodeContents(node) { range.node = node; } };
    return range;
  };
  window.getSelection = () => ({ removeAllRanges() {}, addRange(range) { selection.added.push(range); } });
  return selection;
}

test("parseFriendCode accepts only canonical decimal ids up to the 64-bit maximum", () => {
  const { window } = loadSteamComponents();
  const parse = window.CO_SteamFriends.parseFriendCode;
  assert.equal(parse("76561198000000042"), "76561198000000042");
  assert.equal(parse("18446744073709551615"), "18446744073709551615");
  assert.equal(parse(42), "42");
  for (const bad of ["18446744073709551616", "99999999999999999999", "0", "012", "12a", "-5", " 7", "", null, undefined]) {
    assert.equal(parse(bad), null, String(bad));
  }
});

test("steam LAN rows escape server names and ids for HTML and attributes", () => {
  const { window } = loadSteamComponents();
  const html = window.CO_SteamLan.rows({
    localServers: [{ id: 'a"b<c&', name: '<img src="x">&', players: 1, maxPlayers: 4 }]
  }, null);
  assert.ok(html.includes('data-lan-id="a&quot;b&lt;c&amp;"'), html);
  assert.ok(html.includes('<span class="co-lan-name">&lt;img src=&quot;x&quot;&gt;&amp;</span>'), html);
});

test("copyFriendCode reports success only once the clipboard write resolves", async () => {
  const { window, document } = loadSteamComponents();
  const selection = stubSelection(window, document);
  let release;
  window.navigator = { clipboard: { writeText: () => new Promise((resolve) => { release = resolve; }) } };
  const { root } = copyRoot("76561198000000042");
  const copied = window.CO_SteamFriends.copyFriendCode(root);
  let settled = false;
  copied.then(() => { settled = true; });
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(settled, false);
  release();
  assert.equal(await copied, true);
  assert.equal(selection.added.length, 0);
});

test("copyFriendCode selects the code when the clipboard write rejects", async () => {
  const { window, document } = loadSteamComponents();
  const selection = stubSelection(window, document);
  window.console = { warn() {} };
  window.navigator = { clipboard: { writeText: () => Promise.reject(new Error("denied")) } };
  const { el, root } = copyRoot("76561198000000042");
  assert.equal(await window.CO_SteamFriends.copyFriendCode(root), true);
  assert.equal(selection.added.length, 1);
  assert.equal(selection.added[0].node, el);
});

function joinHarness() {
  const fields = {
    "#co-steam-name": { value: "Host" },
    "#co-steam-password": { value: "" },
    "#co-steam-max-players": { value: "4" },
    "#co-steam-friend-code": { value: "76561198000000042" },
    "#co-steam-join-password": { value: "" }
  };
  const pane = {
    addEventListener() {},
    querySelector(selector) { return fields[selector] || null; }
  };
  const root = { querySelector: (selector) => (selector === '[data-pane="steam-friends"]' ? pane : null) };
  return { root, fields };
}

test("join shows a connecting overlay and a failure clears it with the host's reason", () => {
  const { window } = loadSteamComponents();
  const { root } = joinHarness();
  const overlays = [];
  const actions = [];
  const state = { steamMaxPlayers: 16 };
  const component = window.CO_SteamFriends.create({
    root,
    state,
    render() {},
    sendAction: (action, payload) => actions.push({ action, payload }),
    showJoinOverlay: (message, isError) => overlays.push({ message, isError: !!isError })
  });
  state.steamP2pAvailable = true;

  component.join();
  assert.equal(actions.length, 1);
  assert.equal(actions[0].action, "steamJoin");
  assert.equal(actions[0].payload.address, "steam:76561198000000042");
  assert.equal(overlays.length, 1);
  assert.match(overlays[0].message, /^Connecting to Steam friend 76561198000000042/);
  assert.equal(overlays[0].isError, false);

  const handled = component.handleEvent(
    { type: "steamJoinFailed", reason: "That host is full." },
    (message, isError) => overlays.push({ message, isError: !!isError })
  );
  assert.equal(handled, true);
  assert.equal(state.steamJoinError, "That host is full.");
  assert.equal(overlays.length, 2);
  assert.equal(overlays[1].message, "That host is full.");
  assert.equal(overlays[1].isError, true);
});

test("steam friends render works without CO_Keyboard loaded", () => {
  const { window } = loadSteamComponents();
  assert.equal(window.CO_Keyboard, undefined);
  const container = createElement("div");
  container.className = "co-steam";
  const pane = { querySelector: (selector) => (selector === ".co-steam" ? container : null) };
  const root = { querySelector: (selector) => (selector === '[data-pane="steam-friends"]' ? pane : null) };
  window.CO_SteamFriends.render(root, {
    steam: { name: "", maxPlayers: 4, friendCode: "" },
    steamP2pAvailable: true,
    steamMaxPlayers: 16,
    steamFieldIndex: 0,
    focusZone: "steam-form",
    steamHost: { status: "idle", friendCode: "", reason: "" },
    steamJoinError: "",
    steamInviteNotice: "",
    localServers: []
  });
  assert.ok(container.innerHTML.includes('id="co-steam-join"'));
});

function click(el) {
  el.dispatchEvent({ type: "click", bubbles: true });
}

function mountSteamPane(extra = {}) {
  const { window } = loadSteamComponents();
  const calls = [];
  const overlays = [];
  const state = { steamP2pAvailable: true };
  const root = createElement("div");
  const pane = createElement("div");
  pane.setAttribute("data-pane", "steam-friends");
  const container = createElement("div");
  container.className = "co-steam";
  pane.appendChild(container);
  root.appendChild(pane);
  const render = () => window.CO_SteamFriends.render(root, state);
  const api = window.CO_SteamFriends.create({
    root,
    state,
    render,
    sendAction: (action, payload) => calls.push({ action, ...payload }),
    showJoinOverlay: (message, isError) => overlays.push({ message, isError: !!isError }),
    ...extra
  });
  api.handleEvent({ type: "capabilities", steamP2p: true });
  return { window, state, render, api, calls, container, overlays };
}

function hostedPane(friendCode = "76561198000000042") {
  const pane = mountSteamPane();
  pane.api.handleEvent({ type: "steamHostStarted", friendCode });
  pane.render();
  return pane;
}

test("steam lobby shows the code, Copy, Invite, Enter world and Stop while hosting", () => {
  const { window, container, calls } = hostedPane();
  const html = container.innerHTML;
  assert.match(html, /76561198000000042/);
  assert.match(html, /Invite Steam friends/);
  assert.match(html, /Enter world/);
  assert.match(html, /Stop Hosting/);
  assert.match(html, /Friends connected: 0/);
  assert.equal(container.querySelector("#co-steam-password"), null);
  const copied = [];
  window.navigator = { clipboard: { writeText: (text) => { copied.push(text); return Promise.resolve(); } } };
  click(container.querySelector("#co-steam-copy"));
  click(container.querySelector("#co-steam-invite"));
  click(container.querySelector("#co-steam-stop"));
  assert.equal(copied.length, 1);
  assert.deepEqual(calls, [{ action: "steamInvite" }, { action: "steamHostStop" }]);
});

test("Enter world sends steamHostEnter through the bound lobby", () => {
  const { container, calls } = hostedPane();
  click(container.querySelector("#co-steam-enter"));
  assert.deepEqual(calls, [{ action: "steamHostEnter" }]);
});

test("steamLobbyPeers updates the lobby peer list with escaped names", () => {
  const { container, render, api } = hostedPane();
  assert.equal(api.handleEvent({ type: "steamLobbyPeers", peers: [{ steamId: "76561198000000043", name: "<b>Ann</b>" }] }), true);
  render();
  const html = container.innerHTML;
  assert.match(html, /Friends connected: 1/);
  assert.ok(html.includes("&lt;b&gt;Ann&lt;/b&gt;"), html);
  assert.ok(!html.includes("<b>Ann</b>"), html);
});

test("steamHostFailed while hosting keeps the lobby and shows the reason", () => {
  const { container, render, api, state } = hostedPane();
  api.handleEvent({ type: "steamHostFailed", reason: "Steam hosting is not available" });
  render();
  assert.equal(state.steamHost.status, "hosted");
  assert.ok(container.querySelector("#co-steam-enter"));
  assert.ok(container.querySelector("#co-steam-lobby-error"));
  assert.match(container.innerHTML, /Steam hosting is not available/);
});

test("steamHostStopped resets the lobby peers and error", () => {
  const { container, render, api, state } = hostedPane();
  api.handleEvent({ type: "steamLobbyPeers", peers: [{ steamId: "76561198000000043", name: "Ann" }] });
  api.handleEvent({ type: "steamHostFailed", reason: "Steam hosting is not available" });
  api.handleEvent({ type: "steamHostStopped" });
  render();
  assert.equal(state.steamHost.status, "idle");
  assert.equal(state.steamLobbyPeers.length, 0);
  assert.equal(state.steamLobbyError, "");
  assert.equal(container.querySelector("#co-steam-enter"), null);
  assert.match(container.innerHTML, /Start Hosting/);
  assert.doesNotMatch(container.innerHTML, /Friends connected/);
});

test("steamHostStarted clears the previous lobby and focuses Enter world", () => {
  const { state, api } = mountSteamPane();
  state.steamLobbyPeers = [{ steamId: "76561198000000043", name: "Ann" }];
  state.steamLobbyError = "old";
  state.focusZone = "steam-form";
  state.steamFieldIndex = 3;
  api.handleEvent({ type: "steamHostStarted", friendCode: "76561198000000042" });
  assert.equal(state.steamLobbyPeers.length, 0);
  assert.equal(state.steamLobbyError, "");
  assert.equal(state.steamFieldIndex, 2);
});

test("pressing Enter world twice sends one join and disables the button", () => {
  const { container, calls } = hostedPane();
  assert.equal(container.querySelector("#co-steam-enter").getAttribute("disabled"), null);
  click(container.querySelector("#co-steam-enter"));
  click(container.querySelector("#co-steam-enter"));
  assert.deepEqual(calls, [{ action: "steamHostEnter" }]);
  assert.ok(container.querySelector("#co-steam-enter").getAttribute("disabled") !== null);
});

test("the Enter world guard clears when the join fails", () => {
  const { container, calls, api, render } = hostedPane();
  click(container.querySelector("#co-steam-enter"));
  api.handleEvent({ type: "steamHostFailed", reason: "Could not start the local join." });
  render();
  click(container.querySelector("#co-steam-enter"));
  assert.deepEqual(calls, [{ action: "steamHostEnter" }, { action: "steamHostEnter" }]);
});

test("the Enter world guard clears when the join succeeds", () => {
  const { container, calls, api, render } = hostedPane();
  click(container.querySelector("#co-steam-enter"));
  assert.equal(api.handleEvent({ type: "joinSucceeded" }), false);
  render();
  click(container.querySelector("#co-steam-enter"));
  assert.deepEqual(calls, [{ action: "steamHostEnter" }, { action: "steamHostEnter" }]);
});

test("the Enter world guard clears when hosting stops and starts again", () => {
  const { container, calls, api, render } = hostedPane();
  click(container.querySelector("#co-steam-enter"));
  api.handleEvent({ type: "steamHostStopped" });
  api.handleEvent({ type: "steamHostStarted", friendCode: "76561198000000042" });
  render();
  click(container.querySelector("#co-steam-enter"));
  assert.deepEqual(calls, [{ action: "steamHostEnter" }, { action: "steamHostEnter" }]);
});

test("steamInviteFailed shows the reason as the lobby invite notice", () => {
  const { container, api, render } = hostedPane();
  assert.equal(api.handleEvent({ type: "steamInviteFailed", reason: "Steam overlay is turned off." }), true);
  render();
  assert.ok(container.querySelector("#co-steam-invite-notice"));
  assert.match(container.innerHTML, /Steam overlay is turned off\./);
});

const FRIEND = "76561198000000042";

function host(overrides = {}) {
  return { friendCode: FRIEND, name: "Ann", host: "Ann's camp", players: 2, maxPlayers: 4, build: "1.2.3", compatible: true, reason: "", ...overrides };
}

function showHosts(pane, hosts) {
  pane.api.handleEvent({ type: "steamFriendHosts", hosts });
  pane.render();
}

test("steamStatus unavailable shows an escaped notice and disables Host, Join and Refresh with the reason", () => {
  const pane = mountSteamPane();
  assert.equal(pane.api.handleEvent({ type: "steamStatus", available: false, reason: "Steam is not running <b>", matchmaking: false }), true);
  pane.render();
  const html = pane.container.innerHTML;
  assert.ok(html.includes("Steam unavailable: Steam is not running &lt;b&gt;"), html);
  assert.ok(!html.includes("<b>"), html);
  for (const id of ["co-steam-host", "co-steam-join"]) {
    assert.notEqual(pane.container.querySelector("#" + id).getAttribute("disabled"), null, id);
  }
  assert.equal(pane.container.querySelector("#co-steam-hosts-refresh").getAttribute("disabled"), null);
  assert.ok(html.includes("Press Refresh to retry."), html);
  assert.doesNotMatch(html, /not available in this version yet/);
});

test("steamStatus available restores the controls and clears the notice", () => {
  const pane = mountSteamPane();
  pane.api.handleEvent({ type: "steamStatus", available: false, reason: "offline", matchmaking: false });
  pane.api.handleEvent({ type: "steamStatus", available: true, reason: "", matchmaking: true });
  pane.render();
  assert.equal(pane.container.querySelector("#co-steam-unavailable"), null);
  for (const id of ["co-steam-host", "co-steam-join", "co-steam-hosts-refresh"]) {
    assert.equal(pane.container.querySelector("#" + id).getAttribute("disabled"), null, id);
  }
});

test("without callbacks the old not-available copy stays", () => {
  const pane = mountSteamPane();
  pane.api.handleEvent({ type: "capabilities", steamP2p: false });
  pane.render();
  assert.match(pane.container.innerHTML, /Steam hosting is not available in this version yet/);
  assert.equal(pane.container.querySelector("#co-steam-unavailable"), null);
});

test("host, join and invite explain themselves instead of returning silently when Steam is unavailable", () => {
  const pane = mountSteamPane();
  pane.api.handleEvent({ type: "steamStatus", available: false, reason: "offline" });
  pane.api.hostStart();
  assert.match(pane.state.steamInviteNotice, /^Steam unavailable: offline/);
  pane.api.join();
  assert.match(pane.state.steamJoinError, /^Steam unavailable: offline/);
  pane.state.steamJoinError = "";
  pane.api.joinFriendHost(FRIEND);
  assert.match(pane.state.steamJoinError, /^Steam unavailable: offline/);
  assert.deepEqual(pane.calls, []);
  pane.api.handleEvent({ type: "capabilities", steamP2p: false });
  pane.api.handleEvent({ type: "steamStatus", available: true });
  pane.api.hostStart();
  assert.match(pane.state.steamInviteNotice, /not available in this version yet/);
});

test("steamFriendHosts renders players, host name and escapes every attacker-controlled string", () => {
  const pane = mountSteamPane();
  showHosts(pane, [host({ name: "<script>alert(1)</script>", host: "\"><img src=x onerror=1>", build: "<i>b</i>" }),
    host({ friendCode: "76561198000000043", compatible: false, reason: "Host is running a different Commonwealth Online build (host X, you Y) <script>\"" })]);
  const html = pane.container.innerHTML;
  assert.ok(html.includes("&lt;script&gt;alert(1)&lt;/script&gt;"), html);
  assert.ok(html.includes("&quot;&gt;&lt;img src=x onerror=1&gt;"), html);
  assert.ok(!html.includes("<script>"), html);
  assert.ok(!html.includes("<img"), html);
  assert.ok(!html.includes("<i>"), html);
  assert.ok(html.includes("2/4 players"), html);
  assert.ok(html.includes("Host is running a different Commonwealth Online build (host X, you Y) &lt;script&gt;&quot;"), html);
  assert.match(html, /Friends playing Commonwealth Online/);
});

test("incompatible rows are disabled and compatible rows are not", () => {
  const pane = mountSteamPane();
  showHosts(pane, [host(), host({ friendCode: "76561198000000043", compatible: false, reason: "Different build" })]);
  assert.equal(pane.container.querySelector("#co-friend-host-0").getAttribute("disabled"), null);
  assert.notEqual(pane.container.querySelector("#co-friend-host-1").getAttribute("disabled"), null);
  assert.match(pane.container.innerHTML, /Different build/);
});

test("a host row Join sends the existing steamJoin action as steam:<friendCode>", () => {
  const pane = mountSteamPane();
  showHosts(pane, [host()]);
  click(pane.container.querySelector("#co-friend-host-0"));
  assert.deepEqual(pane.calls, [{ action: "steamJoin", address: "steam:" + FRIEND, password: "" }]);
  assert.match(pane.overlays[0].message, /^Connecting to Steam friend 76561198000000042/);
});

test("joinFriendHost rejects invalid friend codes without sending", () => {
  const pane = mountSteamPane();
  for (const bad of ["012", "abc", "18446744073709551616", "", null, 42, { x: 1 }]) {
    pane.api.joinFriendHost(bad);
  }
  assert.deepEqual(pane.calls, []);
  assert.equal(pane.state.steamJoinError, pane.window.CO_SteamFriends.FRIEND_CODE_ERROR);
});

test("refresh sends steamListHosts with an empty payload and is throttled to once per 10 seconds", () => {
  let clock = 1000;
  const pane = mountSteamPane({ now: () => clock });
  pane.render();
  click(pane.container.querySelector("#co-steam-hosts-refresh"));
  assert.deepEqual(pane.calls, [{ action: "steamListHosts" }]);
  clock += 9999;
  click(pane.container.querySelector("#co-steam-hosts-refresh"));
  pane.api.requestHosts(false);
  assert.equal(pane.calls.length, 1);
  assert.ok(pane.container.querySelector("#co-steam-hosts-notice"));
  clock += 1;
  click(pane.container.querySelector("#co-steam-hosts-refresh"));
  assert.equal(pane.calls.length, 2);
});

test("opening the Steam tab requests hosts once and polls every 10 seconds only while it stays open", () => {
  let clock = 0;
  let tick = null;
  const cleared = [];
  const pane = mountSteamPane({
    now: () => clock,
    setInterval: (fn, ms) => { assert.equal(ms, 10000); tick = fn; return 7; },
    clearInterval: (id) => cleared.push(id)
  });
  pane.api.tabOpened();
  pane.api.tabOpened();
  assert.deepEqual(pane.calls, [{ action: "steamListHosts" }]);
  clock = 10000;
  tick();
  assert.equal(pane.calls.length, 2);
  pane.api.tabClosed();
  assert.deepEqual(cleared, [7]);
});

test("steamFriendHosts handles malformed events defensively", () => {
  const pane = mountSteamPane();
  const bad = [null, 3, "s", [], {}, host({ friendCode: 76561198000000042 }), host({ friendCode: "0" }), host({ friendCode: "abc" })];
  for (const hosts of [undefined, null, "x", 5, {}, bad]) {
    assert.equal(pane.api.handleEvent({ type: "steamFriendHosts", hosts }), true);
    assert.equal(pane.state.steamFriendHosts.length, 0);
  }
  pane.api.handleEvent({ type: "steamFriendHosts", hosts: [{ friendCode: FRIEND, name: { a: 1 }, host: null, players: "zzz", maxPlayers: -4, compatible: "yes" }] });
  const row = pane.state.steamFriendHosts[0];
  assert.equal(row.name, "Steam friend");
  assert.equal(row.host, "Server");
  assert.equal(row.players, 0);
  assert.equal(row.maxPlayers, 0);
  assert.equal(row.compatible, false);
  assert.ok(row.reason.length > 0);
  pane.api.handleEvent({ type: "steamFriendHosts", hosts: [host({ name: "n".repeat(500), host: "h".repeat(500), reason: "r".repeat(500), compatible: false })] });
  assert.equal(pane.state.steamFriendHosts[0].name.length, 64);
  assert.equal(pane.state.steamFriendHosts[0].host.length, 64);
  assert.equal(pane.state.steamFriendHosts[0].reason.length, 200);
  pane.render();
});

test("steamFriendHosts empty state and the 50 row cap", () => {
  const pane = mountSteamPane();
  pane.render();
  assert.match(pane.container.innerHTML, /No friends are hosting right now\./);
  const many = [];
  for (let i = 0; i < 80; i++) many.push(host({ friendCode: String(76561198000000000 + i) }));
  showHosts(pane, many);
  assert.equal(pane.state.steamFriendHosts.length, 50);
  assert.ok(pane.container.querySelector("#co-friend-host-49"));
  assert.equal(pane.container.querySelector("#co-friend-host-50"), null);
  assert.doesNotMatch(pane.container.innerHTML, /No friends are hosting/);
});

test("host rows are reachable through the gamepad field order", () => {
  const pane = mountSteamPane();
  showHosts(pane, [host(), host({ friendCode: "76561198000000043" })]);
  const ids = pane.window.CO_SteamFriends.fieldIds(pane.state);
  const refresh = ids.indexOf("co-steam-hosts-refresh");
  assert.ok(refresh > ids.indexOf("co-steam-join"));
  assert.deepEqual(Array.from(ids.slice(refresh + 1, refresh + 3)), ["co-friend-host-0", "co-friend-host-1"]);
});

test("a steamJoinFailed version refusal reaches the join overlay and the join error escaped", () => {
  const pane = mountSteamPane();
  const overlays = [];
  const text = "Host is running a different Commonwealth Online build (host X, you Y)";
  pane.api.handleEvent({ type: "steamJoinFailed", reason: text }, (message, isError) => overlays.push({ message, isError }));
  assert.deepEqual(overlays, [{ message: text, isError: true }]);
  pane.render();
  assert.ok(pane.container.innerHTML.includes(text));
  pane.api.handleEvent({ type: "steamJoinFailed", reason: "<b>" + text + "</b>" }, () => {});
  pane.render();
  assert.ok(pane.container.innerHTML.includes("&lt;b&gt;" + text));
  assert.ok(!pane.container.innerHTML.includes("<b>Host"));
});

test("an incompatible host refuses to join through the click handler and shows its escaped reason", () => {
  const pane = mountSteamPane();
  showHosts(pane, [host({ compatible: false, reason: "Host is running a different Commonwealth Online build (host X, you Y) <b>" })]);
  click(pane.container.querySelector("#co-friend-host-0"));
  pane.api.joinFriendHost(FRIEND);
  assert.deepEqual(pane.calls, []);
  assert.equal(pane.overlays.length, 0);
  assert.match(pane.state.steamJoinError, /^Host is running a different Commonwealth Online build \(host X, you Y\)/);
  pane.render();
  assert.ok(pane.container.innerHTML.includes("you Y) &lt;b&gt;"));
  assert.ok(!pane.container.innerHTML.includes("Y) <b>"));
});

test("a typed code that is not in the hosts list still joins and a compatible host still joins", () => {
  const pane = mountSteamPane();
  showHosts(pane, [host({ compatible: false, reason: "no" }), host({ friendCode: "76561198000000043" })]);
  pane.api.joinFriendHost("76561198000000099");
  pane.api.joinFriendHost("76561198000000043");
  assert.deepEqual(pane.calls.map((call) => call.address), ["steam:76561198000000099", "steam:76561198000000043"]);
});

test("the refresh throttle treats a clock that moved backwards as expired", () => {
  let clock = 50000;
  const pane = mountSteamPane({ now: () => clock });
  assert.equal(pane.api.requestHosts(false), true);
  clock = 1000;
  assert.equal(pane.api.requestHosts(false), true);
  assert.equal(pane.calls.length, 2);
});

test("a capabilities event after the tab opened requests hosts once", () => {
  const pane = mountSteamPane({ now: () => 0 });
  pane.state.steamP2pAvailable = false;
  pane.api.tabOpened();
  assert.equal(pane.calls.length, 0);
  pane.api.handleEvent({ type: "capabilities", steamP2p: true });
  pane.api.handleEvent({ type: "capabilities", steamP2p: true });
  assert.deepEqual(pane.calls, [{ action: "steamListHosts" }]);
});

test("maxPlayers 0 shows only the player count", () => {
  const pane = mountSteamPane();
  showHosts(pane, [host({ players: 1, maxPlayers: 0 })]);
  assert.ok(pane.container.innerHTML.includes("1 players"));
  assert.ok(!pane.container.innerHTML.includes("1/0"));
});

test("host text truncation does not split a surrogate pair", () => {
  const pane = mountSteamPane();
  const emoji = "\u{1F600}";
  pane.api.handleEvent({ type: "steamFriendHosts", hosts: [host({ name: emoji.repeat(100) })] });
  const name = pane.state.steamFriendHosts[0].name;
  assert.equal(Array.from(name).length, 64);
  assert.equal(name, emoji.repeat(64));
});

test("while Steam is unavailable a manual Refresh sends one steamListHosts and the button stays enabled", () => {
  const pane = mountSteamPane({ now: () => 5000 });
  pane.api.handleEvent({ type: "steamStatus", available: false, reason: "offline", matchmaking: false });
  pane.render();
  assert.equal(pane.container.querySelector("#co-steam-hosts-refresh").getAttribute("disabled"), null);
  click(pane.container.querySelector("#co-steam-hosts-refresh"));
  assert.deepEqual(pane.calls, [{ action: "steamListHosts" }]);
  click(pane.container.querySelector("#co-steam-hosts-refresh"));
  assert.equal(pane.calls.length, 1);
  assert.ok(pane.container.querySelector("#co-steam-hosts-notice"));
});

test("while Steam is unavailable tab open, the interval and capabilities send nothing", () => {
  let tick = null;
  let clock = 0;
  const pane = mountSteamPane({ now: () => clock, setInterval: (fn) => { tick = fn; return 1; }, clearInterval() {} });
  pane.api.handleEvent({ type: "steamStatus", available: false, reason: "offline" });
  pane.api.tabOpened();
  clock = 20000;
  tick();
  pane.api.handleEvent({ type: "capabilities", steamP2p: true });
  assert.deepEqual(pane.calls, []);
});

test("a steamStatus that flips to available re-enables the controls and requests hosts once", () => {
  const pane = mountSteamPane({ now: () => 0 });
  pane.api.handleEvent({ type: "steamStatus", available: false, reason: "offline" });
  pane.api.tabOpened();
  assert.deepEqual(pane.calls, []);
  pane.api.handleEvent({ type: "steamStatus", available: true, reason: "", matchmaking: true });
  pane.api.handleEvent({ type: "steamStatus", available: true, reason: "", matchmaking: true });
  pane.render();
  assert.deepEqual(pane.calls, [{ action: "steamListHosts" }]);
  assert.equal(pane.container.querySelector("#co-steam-host").getAttribute("disabled"), null);
  assert.equal(pane.container.querySelector("#co-steam-join").getAttribute("disabled"), null);
});

test("host and join stay blocked with their reason while Steam is unavailable", () => {
  const pane = mountSteamPane();
  pane.api.handleEvent({ type: "steamStatus", available: false, reason: "offline" });
  pane.api.hostStart();
  pane.api.join();
  assert.match(pane.state.steamInviteNotice, /^Steam unavailable: offline/);
  assert.match(pane.state.steamJoinError, /^Steam unavailable: offline/);
  assert.deepEqual(pane.calls, []);
});
