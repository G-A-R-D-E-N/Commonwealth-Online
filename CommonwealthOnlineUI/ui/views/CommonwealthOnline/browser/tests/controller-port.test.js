const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { createEnvironment, createElement } = require("./dom.js");
const browserPath = path.join(__dirname, "..");

function loadBrowser() {
  const { window, document } = createEnvironment();
  const mount = createElement("div");
  mount.ownerDocument = document;
  mount.parentNode = document.body;
  document.body.children.push(mount);
  window.CO_ControllerGlyphs = { platform: "Xbox", setPlatform() {}, hydrate() {}, render(key) { return "pad:" + key; } };
  window.CO_ServerList = { render() {}, scrollToSelected() {}, isLocalHost() { return false; }, displayName(server) { return server.name; } };
  window.CO_ServerDetails = { render() {} };
  const calls = [];
  window.coAction = (value) => calls.push(JSON.parse(value));
  const context = vm.createContext(window);
  for (const name of ["tabs", "filters", "controller-input", "text-focus", "keyboard", "controller-trace", "controller-icons", "controller-prompts", "modpack-detail", "modpack-copy", "direct-connect", "steam-lan", "steam-friends", "server-settings", "browser-markup", "server-records", "recent-servers", "filter-state", "server-counts", "join-flow", "password-prompt", "share-log", "controller-navigation", "directory-refresh", "mock-data"]) {
    vm.runInContext(fs.readFileSync(path.join(browserPath, "components", name + ".js"), "utf8"), context);
  }
  vm.runInContext(fs.readFileSync(path.join(browserPath, "browser.js"), "utf8"), context);
  const browser = window.CO_Browser.create({ mount, layout: "embedded", exposeGlobals: true, onBack() {} });
  browser.show();
  calls.length = 0;
  const state = window.CO_App.getState();
  function action(name, phase = "pressed") {
    window.dispatchEvent({ type: "prisma-controller-action", detail: { action: name, state: phase } });
  }
  return { window, document, mount, browser, state, calls, action };
}

function servers() {
  return [
    { id: "a", host: "1.1.1.1", port: 1, name: "Alpha", favorite: false },
    { id: "b", host: "2.2.2.2", port: 2, name: "Beta", favorite: true },
    { id: "c", host: "3.3.3.3", port: 3, name: "Gamma", favorite: true }
  ];
}

function click(el) {
  el.dispatchEvent({ type: "click", bubbles: true });
}

function steamEnv() {
  const env = loadBrowser();
  env.window.dispatchCoEvent({ type: "capabilities", steamP2p: true });
  env.state.activeTab = "steam-friends";
  env.browser.render();
  env.calls.length = 0;
  return env;
}

function steamHtml(env) {
  return env.mount.querySelector(".co-steam").innerHTML;
}

function seedServerIdentity(env, name, maxPlayers) {
  env.state.serverSettings = {
    loaded: true,
    loading: false,
    folder: "C:/Games/CO",
    configPath: "C:/Games/CO/commonwealth-server.json",
    exists: true,
    values: { serverName: name, maxPlayers: maxPlayers, worldMode: "coop-story" },
    draft: {},
    folderDraft: "",
    folderDraftSet: false,
    errors: [],
    status: "",
    limits: { maximumPlayers: 16, worldModes: ["coop-story", "free-roam"] },
    passwordClear: false
  };
  env.browser.render();
}

function setFriendCode(env, value) {
  const field = env.mount.querySelector("#co-steam-friend-code");
  field.value = value;
  field.dispatchEvent({ type: "input", bubbles: true });
}

test("one left press from the server list reaches the tab strip", () => {
  const env = loadBrowser();
  env.state.focusZone = "server-list";
  env.state.activeTab = "server-browser";
  env.action("co.navRight");
  assert.equal(env.state.focusZone, "filters");
  env.action("co.navLeft");
  assert.equal(env.state.focusZone, "server-list");
  env.action("co.navLeft");
  assert.equal(env.state.focusZone, "tabs");
});

test("dpad down on an empty list keeps the selection at zero", () => {
  const env = loadBrowser();
  env.state.activeTab = "recent";
  env.state.recent = [];
  env.state.focusZone = "server-list";
  env.state.selectedServerIndex = 0;
  env.action("co.navDown");
  assert.equal(env.state.selectedServerIndex, 0);
});

test("tab strip up and down preview without committing until A", () => {
  const env = loadBrowser();
  env.state.activeTab = "server-browser";
  env.state.selectedTabIndex = 1;
  env.state.focusZone = "server-list";
  env.action("co.navLeft");
  assert.equal(env.state.focusZone, "tabs");
  env.action("co.navDown");
  assert.equal(env.state.activeTab, "server-browser");
  assert.equal(env.state.selectedTabIndex, env.window.CO_Tabs.TAB_ORDER.indexOf("favorites"));
  assert.equal(env.calls.filter((call) => call.action === "switchTab").length, 0);
  env.action("co.select");
  assert.equal(env.state.activeTab, "favorites");
  assert.equal(env.state.focusZone, "server-list");
  assert.equal(env.calls.filter((call) => call.action === "switchTab").length, 1);
});

test("A on the direct-connect tab strip commits the previewed tab and leaves the form", () => {
  const env = loadBrowser();
  env.state.activeTab = "direct-connect";
  env.state.selectedTabIndex = 0;
  env.state.focusZone = "tabs";
  env.browser.render();
  env.action("co.navDown");
  env.action("co.select");
  assert.equal(env.state.activeTab, "server-browser");
  assert.equal(env.state.focusZone, "server-list");
});

test("A on the active Direct Connect tab sends no second switchTab and keeps the form field", () => {
  const env = loadBrowser();
  env.state.activeTab = "direct-connect";
  env.state.selectedTabIndex = 0;
  env.state.focusZone = "tabs";
  env.state.directConnectFieldIndex = 2;
  env.browser.render();
  env.action("co.select");
  assert.equal(env.calls.filter((call) => call.action === "switchTab").length, 0);
  assert.equal(env.state.directConnectFieldIndex, 2);
});

test("A on the active server-browser tab sends no second switchTab", () => {
  const env = loadBrowser();
  env.state.activeTab = "server-browser";
  env.state.selectedTabIndex = 1;
  env.state.focusZone = "tabs";
  env.browser.render();
  env.action("co.select");
  assert.equal(env.calls.filter((call) => call.action === "switchTab").length, 0);
  assert.equal(env.state.activeTab, "server-browser");
  assert.equal(env.state.focusZone, "server-list");
});

test("reopening the browser abandons an unsettled tab preview", () => {
  const env = loadBrowser();
  env.state.activeTab = "server-browser";
  env.state.selectedTabIndex = 1;
  env.state.focusZone = "server-list";
  env.action("co.navLeft");
  env.action("co.navDown");
  env.action("co.navDown");
  assert.equal(env.state.selectedTabIndex, env.window.CO_Tabs.TAB_ORDER.indexOf("recent"));
  env.window.dispatchCoEvent({ type: "menuOpened" });
  assert.equal(env.state.selectedTabIndex, env.window.CO_Tabs.TAB_ORDER.indexOf("server-browser"));
});

test("a browse-list refresh clamps the favorites selection against the favorites list", () => {
  const env = loadBrowser();
  env.state.filters.search = "Beta";
  env.window.dispatchCoEvent({ type: "serverListUpdated", servers: servers() });
  assert.equal(env.state.servers.length, 1);
  assert.equal(env.state.favorites.length, 2);
  env.state.activeTab = "favorites";
  env.state.selectedServerIndex = 1;
  env.window.dispatchCoEvent({ type: "serverListUpdated", servers: servers() });
  assert.equal(env.state.selectedServerIndex, 1);
});

test("prompts cover the tab strip, the connecting overlay and the Direct Connect field labels", () => {
  const env = loadBrowser();
  const prompts = (state) => env.window.CO_ControllerPrompts.getPrompts(state);
  const labelOf = (list, key) => (list.find((p) => p.key === key) || {}).label;
  assert.equal(labelOf(prompts({ activeTab: "server-browser", focusZone: "tabs", selectedFilterIndex: 0 }), "A"), "Select");
  assert.equal(labelOf(prompts({ activeTab: "server-browser", focusZone: "server-list", joinOverlay: { message: "Connecting...", error: false } }), "B"), "Cancel");
  const form = (index) => prompts({ activeTab: "direct-connect", focusZone: "direct-form", directConnectFieldIndex: index });
  assert.equal(labelOf(form(0), "A"), "Change");
  assert.equal(labelOf(form(1), "A"), "Edit");
  assert.equal(labelOf(form(2), "A"), "Edit");
  assert.equal(labelOf(form(3), "A"), "Connect");
  assert.equal(labelOf(form(4), "A"), "Retry");
});

test("Direct Connect address opens the OSK, types through it, and B restores the value", () => {
  const env = loadBrowser();
  env.state.activeTab = "direct-connect";
  env.state.focusZone = "direct-form";
  env.state.directConnectFieldIndex = 0;
  env.browser.render();
  env.action("co.select");
  assert.equal(env.window.CO_Keyboard.isOpen(), true);
  assert.equal(env.window.CO_Keyboard.getTarget().id, "co-dc-address");
  const field = env.document.getElementById("co-dc-address");
  env.action("co.select");
  assert.notEqual(field.value, "127.0.0.1");
  env.action("co.back");
  assert.equal(env.window.CO_Keyboard.isOpen(), false);
  assert.equal(field.value, "127.0.0.1");
});

test("the prompt bar is hidden in mouse mode and shown only for the gamepad-mode root", () => {
  const css = fs.readFileSync(path.join(browserPath, "styles.css"), "utf8");
  assert.match(css, /\.co-prompts \{ display: none;/);
  assert.match(css, /\.co-root\.co-gamepad-mode \.co-prompts \{ display: flex; \}/);
});

test("controller trace records navigation enter and exit only while enabled", () => {
  const env = loadBrowser();
  env.window.dispatchCoEvent({ type: "serverListUpdated", servers: servers() });
  env.state.focusZone = "server-list";
  env.state.activeTab = "server-browser";
  env.action("co.navDown");
  assert.equal(env.window.CO_ControllerTrace.entries().length, 0);
  env.window.CO_ControllerTrace.setEnabled(true);
  env.action("co.navDown");
  assert.equal(env.window.CO_ControllerTrace.stages().join(" > "), "prisma-controller-action > navigateFocus:enter > navigateFocus:exit");
  assert.equal(env.window.CO_ControllerTrace.entries()[2].outcome, "serverIndex");
});

test("the tab strip carries Friends (Steam) exactly once, right after Browse", () => {
  const env = loadBrowser();
  env.window.dispatchCoEvent({ type: "capabilities", steamP2p: true });
  const order = env.window.CO_Tabs.TAB_ORDER;
  assert.equal(order.filter((id) => id === "steam-friends").length, 1);
  assert.equal(order[order.indexOf("server-browser") + 1], "steam-friends");
  const nav = env.mount.querySelector(".co-nav-list").innerHTML;
  assert.match(nav, /data-tab="steam-friends"/);
  assert.match(nav, /Friends \(Steam\)/);

  click(env.mount.querySelector('[data-tab="steam-friends"]'));
  assert.equal(env.state.activeTab, "steam-friends");
  assert.equal(env.state.selectedTabIndex, order.indexOf("steam-friends"));
  assert.equal(env.mount.querySelector('[data-pane="steam-friends"]').classList.contains("active"), true);
  assert.equal(env.mount.querySelector('[data-pane="server-browser"]').classList.contains("active"), false);
});

test("Friend Code parsing follows the steam:<id> transport rule", () => {
  const env = loadBrowser();
  const parse = env.window.CO_SteamFriends.parseFriendCode;
  assert.equal(parse("76561197960265728"), "76561197960265728");
  assert.equal(parse("1"), "1");
  assert.equal(parse("18446744073709551615"), "18446744073709551615");
  for (const bad of ["", "0", "01", "abc", "1.5", " 1", "1 ", "-1", "+1", "0x1", "1e3", "18446744073709551616", "123456789012345678901", null, undefined]) {
    assert.equal(parse(bad), null, "rejects " + JSON.stringify(bad));
  }
});

test("Friends (Steam) tab shows only when the bridge reports steamP2p", () => {
  const env = loadBrowser();
  assert.equal(env.state.steamP2pAvailable, false);
  assert.equal(env.mount.querySelector('[data-tab="steam-friends"]'), null);
  env.window.dispatchCoEvent({ type: "capabilities", steamP2p: true });
  assert.notEqual(env.mount.querySelector('[data-tab="steam-friends"]'), null);
  env.window.dispatchCoEvent({ type: "capabilities", steamP2p: "yes" });
  assert.equal(env.state.steamP2pAvailable, false);
  assert.equal(env.mount.querySelector('[data-tab="steam-friends"]'), null);
});

test("dpad and bumper tab cycling skips the hidden Friends (Steam) tab", () => {
  const env = loadBrowser();
  env.state.activeTab = "server-browser";
  env.state.selectedTabIndex = 1;
  env.state.focusZone = "tabs";
  env.browser.render();
  env.action("co.navDown");
  assert.equal(env.state.selectedTabIndex, env.window.CO_Tabs.TAB_ORDER.indexOf("favorites"));
  env.action("co.nextTab");
  assert.equal(env.state.activeTab, "favorites");
  env.action("co.prevTab");
  assert.equal(env.state.activeTab, "server-browser");
});

test("losing the Steam capability while on the Friends tab moves to Browse", () => {
  const env = steamEnv();
  env.window.dispatchCoEvent({ type: "capabilities", steamP2p: false });
  assert.equal(env.state.activeTab, "server-browser");
  assert.equal(env.state.selectedTabIndex, env.window.CO_Tabs.TAB_ORDER.indexOf("server-browser"));
});

test("Steam hosting stays gated until the bridge reports the capability", () => {
  const env = loadBrowser();
  env.state.activeTab = "steam-friends";
  env.browser.render();
  env.calls.length = 0;
  assert.equal(env.state.steamP2pAvailable, false);
  assert.notEqual(env.mount.querySelector("#co-steam-host").getAttribute("disabled"), null);
  assert.equal(env.mount.querySelector("#co-steam-host").getAttribute("aria-disabled"), "true");
  assert.notEqual(env.mount.querySelector("#co-steam-join").getAttribute("disabled"), null);

  setFriendCode(env, "76561197960265728");
  click(env.mount.querySelector("#co-steam-host"));
  click(env.mount.querySelector("#co-steam-join"));
  env.state.focusZone = "steam-form";
  env.state.steamFieldIndex = 3;
  env.action("co.select");
  env.action("co.back");
  assert.equal(env.calls.length, 0);

  env.window.dispatchCoEvent({ type: "capabilities", steamP2p: true });
  assert.equal(env.state.steamP2pAvailable, true);
  assert.equal(env.mount.querySelector("#co-steam-host").getAttribute("disabled"), null);
  assert.equal(env.mount.querySelector("#co-steam-join").getAttribute("disabled"), null);

  env.window.dispatchCoEvent({ type: "capabilities", steamP2p: "yes" });
  assert.equal(env.state.steamP2pAvailable, false);
});

test("an enabled Join sends steam:<id> for a valid Friend Code", () => {
  const env = steamEnv();
  setFriendCode(env, "76561197960265728");
  click(env.mount.querySelector("#co-steam-join"));
  assert.deepEqual(env.calls, [{ action: "steamJoin", address: "steam:76561197960265728", password: "" }]);

  env.calls.length = 0;
  setFriendCode(env, "1");
  click(env.mount.querySelector("#co-steam-join"));
  assert.deepEqual(env.calls, [{ action: "steamJoin", address: "steam:1", password: "" }]);
});

test("an enabled Join refuses a malformed numeric Friend Code inline and sends nothing", () => {
  const env = steamEnv();
  for (const bad of ["0", "01", "18446744073709551616", "123456789012345678901"]) {
    setFriendCode(env, bad);
    click(env.mount.querySelector("#co-steam-join"));
    assert.equal(env.calls.length, 0, "no join for " + JSON.stringify(bad));
    assert.match(steamHtml(env), /Friend Code should be a number from the host's Steam profile/);
  }
  setFriendCode(env, "76561197960265728");
  assert.equal(env.mount.querySelector("#co-steam-join-error").textContent, "");
  click(env.mount.querySelector("#co-steam-join"));
  assert.equal(env.calls.length, 1);
  assert.doesNotMatch(steamHtml(env), /should be a number/);
});

test("the host card advertises the server settings name and capacity", () => {
  const env = steamEnv();
  seedServerIdentity(env, "Vault 111", 8);
  assert.match(steamHtml(env), /Advertised as Vault 111 \u00b7 8 players/);
  assert.equal(env.mount.querySelector("#co-steam-name"), null);
  assert.equal(env.mount.querySelector("#co-steam-max-players"), null);
});

test("hosting is refused until a server name is known", () => {
  const env = steamEnv();
  click(env.mount.querySelector("#co-steam-host"));
  assert.equal(env.calls.filter((call) => call.action === "steamHostStart").length, 0);
  assert.match(steamHtml(env), /Reading the server settings/, "nothing is known yet, so say that");

  seedServerIdentity(env, "", 8);
  click(env.mount.querySelector("#co-steam-host"));
  assert.equal(env.calls.filter((call) => call.action === "steamHostStart").length, 0);
  assert.match(steamHtml(env), /Set your server name in Server settings/);
});

test("hosting is refused when the saved world mode cannot be served", () => {
  const env = steamEnv();
  seedServerIdentity(env, "Nomad's Game", 8);
  env.state.serverSettings.worldModeRefusal =
    "World mode free-roam needs a generated world policy file with story or companion entries.";
  env.browser.render();
  click(env.mount.querySelector("#co-steam-host"));
  assert.equal(env.calls.filter((call) => call.action === "steamHostStart").length, 0);
  assert.match(env.state.steamHost.reason, /needs a generated world policy file/);
  assert.match(steamHtml(env), /Could not start hosting/);
});

test("hosting sends steamHostStart, keeps the typed password until the start lands", () => {
  const env = steamEnv();
  seedServerIdentity(env, "Nomad's Game", 8);
  env.mount.querySelector("#co-steam-password").value = "hunter2";
  click(env.mount.querySelector("#co-steam-host"));
  assert.deepEqual(env.calls, [{ action: "steamHostStart", name: "Nomad's Game", password: "hunter2", maxPlayers: 8 }]);
  assert.equal(env.mount.querySelector("#co-steam-password").value, "hunter2",
    "the request is not the server taking it, so the field keeps it");
  assert.equal(JSON.stringify(env.state.steam).includes("hunter2"), false);
  assert.match(steamHtml(env), /Starting your server/);

  env.window.dispatchCoEvent({ type: "steamHostStarted", friendCode: "76561197960265728" });
  assert.equal(env.mount.querySelector("#co-steam-password"), null, "the Lobby card has no password field while hosting");
  assert.match(steamHtml(env), /76561197960265728/);
  assert.match(steamHtml(env), /Stop Hosting/);
  assert.ok(env.mount.querySelector("#co-steam-copy"), "the Copy button appears with the Friend Code");
  assert.equal(env.state.steamHost.status, "hosted");

  env.calls.length = 0;
  click(env.mount.querySelector("#co-steam-copy"));
  assert.equal(env.calls.length, 0, "Copy sends nothing to the bridge");

  click(env.mount.querySelector("#co-steam-stop"));
  assert.deepEqual(env.calls, [{ action: "steamHostStop" }]);

  env.window.dispatchCoEvent({ type: "steamHostStopped" });
  assert.equal(env.state.steamHost.status, "idle");
  assert.doesNotMatch(steamHtml(env), /76561197960265728/);
  assert.match(steamHtml(env), /Start Hosting/);
  assert.equal(env.mount.querySelector("#co-steam-password").value, "", "a stopped host starts with an empty password field");
});

test("a typed host password survives a rerender and a refused start", () => {
  const env = steamEnv();
  seedServerIdentity(env, "Nomad's Game", 8);
  const field = () => env.mount.querySelector("#co-steam-password");
  field().value = "hunter2";

  env.browser.render();
  assert.equal(field().value, "hunter2", "a rerender must not drop what the host typed");

  env.window.dispatchCoEvent({ type: "steamHostFailed", reason: "Steam is not running." });
  assert.equal(field().value, "hunter2", "a refused start keeps the password for the retry");
  assert.match(steamHtml(env), /Could not start hosting/);

  env.state.serverSettings.worldModeRefusal = "World mode free-roam needs a generated world policy file.";
  env.browser.render();
  click(env.mount.querySelector("#co-steam-host"));
  assert.equal(field().value, "hunter2", "a start refused before it is sent keeps it too");
  assert.equal(env.calls.filter((call) => call.action === "steamHostStart").length, 0);

  env.state.serverSettings.worldModeRefusal = "";
  env.browser.render();
  env.mount.querySelector("#co-steam-join-password").value = "joinpw";
  click(env.mount.querySelector("#co-steam-join"));
  assert.equal(field().value, "hunter2", "joining must not clear the host password");
  assert.equal(env.mount.querySelector("#co-steam-join-password").value, "", "the join password was sent");
  assert.equal(JSON.stringify(env.state.steam).includes("hunter2"), false);
  assert.equal(JSON.stringify(env.state.steam).includes("joinpw"), false);
});

test("a host failure is shown as escaped plain text", () => {
  const env = steamEnv();
  env.window.dispatchCoEvent({ type: "steamHostFailed", reason: "<img src=x onerror=alert(1)>" });
  assert.match(steamHtml(env), /Could not start hosting/);
  assert.equal(steamHtml(env).includes("<img"), false);
  assert.match(steamHtml(env), /&lt;img src=x onerror=alert\(1\)&gt;/);
  env.window.dispatchCoEvent({ type: "steamHostFailed" });
  assert.match(steamHtml(env), /Could not start hosting\. Try again\./);
});

test("controller navigation reaches the Friends (Steam) fields and leaves the form", () => {
  const env = steamEnv();
  env.state.focusZone = "tabs";
  env.browser.render();
  env.action("co.navRight");
  assert.equal(env.state.focusZone, "steam-form");
  assert.equal(env.state.steamFieldIndex, 0);
  env.action("co.select");
  assert.equal(env.window.CO_Keyboard.isOpen(), true);
  assert.equal(env.window.CO_Keyboard.getTarget().id, "co-steam-password");
  env.action("co.back");
  assert.equal(env.window.CO_Keyboard.isOpen(), false);
  assert.equal(env.state.focusZone, "steam-form");

  env.state.steamFieldIndex = env.window.CO_SteamFriends.fieldIds(env.state).indexOf("co-steam-friend-code");
  assert.notEqual(env.state.steamFieldIndex, -1);
  env.action("co.select");
  assert.equal(env.window.CO_Keyboard.getTarget().id, "co-steam-friend-code");
  env.action("co.back");

  seedServerIdentity(env, "Vault 111", 4);
  env.state.steamFieldIndex = 1;
  env.action("co.select");
  assert.equal(env.calls.length, 1);
  assert.equal(env.calls[0].action, "steamHostStart");

  env.action("co.back");
  assert.equal(env.state.focusZone, "tabs");
  assert.equal(env.state.activeTab, "steam-friends");
  env.action("co.navLeft");
  assert.equal(env.state.focusZone, "tabs");
});

test("the prompt bar names the focused Steam field", () => {
  const env = steamEnv();
  const labelOf = (state) => env.window.CO_ControllerPrompts.getPrompts(state).find((p) => p.key === "A").label;
  assert.equal(labelOf({ activeTab: "steam-friends", focusZone: "tabs" }), "Select");
  const fieldState = (extra) => Object.assign(
    { activeTab: "steam-friends", focusZone: "steam-form" },
    extra || {}
  );
  const indexOf = (id, extra) => {
    const index = env.window.CO_SteamFriends.fieldIds(fieldState(extra)).indexOf(id);
    assert.notEqual(index, -1, id + " must be a Steam form field");
    return index;
  };
  const labelFor = (id, extra) => labelOf(Object.assign(fieldState(extra), { steamFieldIndex: indexOf(id, extra) }));
  assert.equal(labelFor("co-steam-password"), "Edit");
  assert.equal(labelFor("co-steam-host"), "Select");
  assert.equal(labelFor(env.window.CO_ServerSettings.FOLDER_ID), "Edit");
  assert.equal(labelFor("co-ss-name"), "Edit");
  assert.equal(labelFor("co-ss-max-players"), "Change");
  assert.equal(labelFor("co-ss-world-mode"), "Change");
  assert.equal(labelFor("co-ss-doors-shared"), "Change");
  assert.equal(labelFor("co-ss-reload"), "Reload");
  assert.equal(labelFor("co-ss-save"), "Save");
  assert.equal(labelFor("co-steam-friend-code"), "Edit");
  assert.equal(labelFor("co-steam-join"), "Select");
  assert.equal(indexOf("co-ss-save"), indexOf("co-ss-reload") + 1);
  assert.equal(indexOf("co-steam-friend-code"), indexOf("co-ss-save") + 1);
  const hosted = { steamHost: { status: "hosted", friendCode: "76561197960265728" } };
  assert.equal(labelFor("co-steam-stop", hosted), "Select");
  assert.equal(labelFor("co-steam-friend-code", hosted), "Edit");
});

test("opening the Steam tab requests friend hosts once and a steamFriendHosts event renders rows", () => {
  const env = loadBrowser();
  env.window.dispatchCoEvent({ type: "capabilities", steamP2p: true });
  env.calls.length = 0;
  click(env.mount.querySelector('[data-tab="steam-friends"]'));
  click(env.mount.querySelector('[data-tab="steam-friends"]'));
  assert.equal(env.calls.filter((call) => call.action === "steamListHosts").length, 1);
  env.window.dispatchCoEvent({ type: "steamFriendHosts", hosts: [{ friendCode: "76561198000000042", name: "Ann", host: "Camp", players: 1, maxPlayers: 4, build: "b", compatible: true, reason: "" }] });
  assert.match(steamHtml(env), /id="co-friend-host-0"/);
  click(env.mount.querySelector('[data-tab="server-browser"]'));
  click(env.mount.querySelector('[data-tab="steam-friends"]'));
  assert.equal(env.calls.filter((call) => call.action === "steamListHosts").length, 1);
});

test("A on an incompatible host row sends no steamJoin and shows the reason", () => {
  const env = loadBrowser();
  env.window.dispatchCoEvent({ type: "capabilities", steamP2p: true });
  env.state.activeTab = "steam-friends";
  env.window.dispatchCoEvent({ type: "steamFriendHosts", hosts: [{ friendCode: "76561198000000042", name: "Ann", host: "Camp", players: 1, maxPlayers: 4, build: "b", compatible: false, reason: "Host is running a different Commonwealth Online build (host X, you Y)" }] });
  env.state.focusZone = "steam-form";
  env.state.steamFieldIndex = env.window.CO_SteamFriends.fieldIds(env.state).indexOf("co-friend-host-0");
  env.calls.length = 0;
  env.action("co.select");
  assert.equal(env.calls.filter((call) => call.action === "steamJoin").length, 0);
  assert.match(env.state.steamJoinError, /different Commonwealth Online build/);
});

test("A on Refresh while Steam is unavailable sends steamListHosts", () => {
  const env = loadBrowser();
  env.window.dispatchCoEvent({ type: "capabilities", steamP2p: true });
  env.state.activeTab = "steam-friends";
  env.window.dispatchCoEvent({ type: "steamStatus", available: false, reason: "Steam is not running", matchmaking: false });
  env.state.focusZone = "steam-form";
  env.state.steamFieldIndex = env.window.CO_SteamFriends.fieldIds(env.state).indexOf("co-steam-hosts-refresh");
  env.calls.length = 0;
  env.action("co.select");
  assert.equal(env.calls.filter((call) => call.action === "steamListHosts").length, 1);
  assert.equal(env.mount.querySelector("#co-steam-hosts-refresh").getAttribute("disabled"), null);
});
