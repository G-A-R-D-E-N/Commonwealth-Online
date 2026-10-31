const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { createEnvironment, createElement } = require("./dom.js");
const browserPath = path.join(__dirname, "..");

function loadBrowser(layout = "embedded") {
  const { window, document } = createEnvironment();
  const mount = createElement("div");
  mount.ownerDocument = document;
  mount.parentNode = document.body;
  document.body.children.push(mount);
  let focused = true;
  document.hasFocus = () => focused;
  window.CO_ControllerGlyphs = { platform: "Xbox", setPlatform(value) { this.platform = value; }, hydrate() {}, render(key) { return "pad:" + key; } };
  window.CO_ServerList = { render() {}, scrollToSelected() {}, isLocalHost() { return false; }, displayName(server) { return server.name; } };
  window.CO_ServerDetails = { render() {} };
  const calls = [];
  window.coAction = (value) => calls.push(JSON.parse(value));
  const context = vm.createContext(window);
  for (const name of ["tabs", "filters", "controller-input", "text-focus", "keyboard", "controller-trace", "controller-icons", "controller-prompts", "direct-connect", "steam-lan", "steam-friends", "server-settings", "browser-markup", "server-records", "recent-servers", "filter-state", "server-counts", "join-flow", "password-prompt", "share-log", "modpack-detail", "modpack-copy", "controller-navigation", "directory-refresh", "mock-data"]) {
    const file = path.join(browserPath, "components", name + ".js");
    if (fs.existsSync(file)) vm.runInContext(fs.readFileSync(file, "utf8"), context);
  }
  vm.runInContext(fs.readFileSync(path.join(browserPath, "browser.js"), "utf8"), context);
  let backCount = 0;
  const browser = window.CO_Browser.create({ mount, layout, exposeGlobals: true, onBack() { backCount++; } });
  browser.show();
  calls.length = 0;
  const state = window.CO_App.getState();
  function action(action, phase = "pressed") {
    window.dispatchEvent({ type: "prisma-controller-action", detail: { action, state: phase } });
  }
  return { window, document, mount, browser, state, calls, action, setFocused(value) { focused = value; }, backCount() { return backCount; } };
}

test("Prisma actions dispatch once, repeat only directions, and stop outside browser focus", () => {
  const env = loadBrowser();
  env.action("co.refresh");
  env.action("co.refresh", "repeat");
  env.action("co.refresh", "released");
  assert.equal(env.calls.filter(call => call.action === "refreshServerList").length, 1);
  env.setFocused(false);
  env.action("co.refresh");
  assert.equal(env.calls.length, 1);
  env.setFocused(true);
  env.browser.hide();
  env.action("co.refresh");
  assert.equal(env.calls.length, 1);
  assert.equal(env.window.__timers.length, 0);
});

test("filter A changes its value without joining the selected server", () => {
  const env = loadBrowser();
  env.browser.dispatchCoEvent({ type: "serverListUpdated", servers: [{ id: "one", host: "127.0.0.1", port: 7777, name: "One" }] });
  env.state.focusZone = "filters";
  env.state.selectedFilterIndex = 1;
  env.browser.handleControllerInput("a", "down");
  assert.equal(env.state.filters.region, "Oceania");
  assert.equal(env.calls.filter(call => call.action === "joinServer").length, 0);
});

test("friends tab gamepad reaches Start Hosting and can leave the form", () => {
  const env = loadBrowser();
  env.state.steamP2pAvailable = true;
  env.state.activeTab = "steam-friends";
  env.state.selectedTabIndex = 2;
  env.state.focusZone = "tabs";
  env.browser.render();
  env.browser.handleControllerInput("dpad_right", "down");
  assert.equal(env.state.focusZone, "steam-form");
  env.state.serverSettings = {
    loaded: true,
    values: { serverName: "Vault 111", maxPlayers: 4 },
    draft: {},
    errors: [],
    limits: { maximumPlayers: 16, worldModes: ["coop-story", "free-roam"] }
  };
  env.browser.render();
  env.state.steamFieldIndex = 1;
  env.browser.handleControllerInput("a", "down");
  assert.equal(env.calls.filter(call => call.action === "steamHostStart").length, 1);
  env.browser.handleControllerInput("b", "down");
  assert.equal(env.state.focusZone, "tabs");
  assert.equal(env.browser.isVisible(), true);
});

test("direct connect reaches both editable fields, Connect and Retry, and can leave the form", () => {
  const env = loadBrowser();
  env.state.activeTab = "direct-connect";
  env.state.selectedTabIndex = 0;
  env.state.focusZone = "tabs";
  env.state.retryAvailable = true;
  env.browser.render();
  env.browser.handleControllerInput("dpad_right", "down");
  assert.equal(env.state.focusZone, "direct-form");
  for (const id of ["co-dc-address", "co-dc-port", "co-dc-password"]) {
    env.browser.handleControllerInput("a", "down");
    assert.equal(env.window.CO_Keyboard.getTarget().id, id);
    env.browser.handleControllerInput("b", "down");
    assert.equal(env.browser.isVisible(), true);
    env.browser.handleControllerInput("dpad_down", "down");
  }
  assert.equal(env.state.directConnectFieldIndex, 3);
  env.browser.handleControllerInput("dpad_down", "down");
  assert.equal(env.state.directConnectFieldIndex, 4);
  env.browser.handleControllerInput("a", "down");
  assert.equal(env.calls.filter(call => call.action === "retryLastJoin").length, 1);
  env.browser.dispatchCoEvent({ type: "joinFailed", reason: "Test" });
  env.browser.handleControllerInput("b", "down");
  env.browser.handleControllerInput("dpad_left", "down");
  assert.equal(env.state.focusZone, "tabs");
  assert.equal(env.state.selectedTabIndex, 0);
});

test("OSK edits stay attached across rerenders and cancel restores the field", () => {
  const env = loadBrowser();
  env.state.activeTab = "direct-connect";
  env.state.focusZone = "direct-form";
  env.browser.render();
  const original = env.document.getElementById("co-dc-address");
  env.browser.handleControllerInput("a", "down");
  env.browser.handleControllerInput("a", "down");
  assert.ok(env.document.getElementById("co-dc-address") === original, "editing must preserve the field node");
  assert.equal(original.value, "127.0.0.11");
  assert.equal(env.state.directConnect.address, original.value);
  env.browser.handleControllerInput("b", "down");
  assert.equal(env.state.directConnect.address, "127.0.0.1");
  assert.equal(env.browser.isVisible(), true);
  assert.equal(env.calls.filter(call => call.action === "openDirectConnect").length, 0);
});

test("shoulder navigation returns Direct Connect to list focus and B closes embedded browser locally", () => {
  const env = loadBrowser();
  env.state.activeTab = "direct-connect";
  env.state.focusZone = "tabs";
  env.action("co.prevTab");
  assert.equal(env.state.activeTab, "recent");
  assert.equal(env.state.focusZone, "server-list");
  env.action("co.back");
  assert.equal(env.browser.isVisible(), false);
  assert.equal(env.backCount(), 1);
  assert.equal(env.calls.filter(call => call.action === "closeBrowser").length, 0);
});

test("typing in direct fields survives rendering and blocks letter shortcuts", () => {
  const env = loadBrowser();
  env.state.activeTab = "direct-connect";
  env.browser.render();
  const field = env.document.getElementById("co-dc-address");
  field.focus();
  field.value = "fresh.example";
  field.dispatchEvent({ type: "input", bubbles: true });
  env.browser.handleKeydown({ key: "r", target: field });
  assert.equal(env.calls.length, 0);
  assert.ok(env.document.getElementById("co-dc-address") === field, "typing must preserve the field node");
  assert.equal(field.value, "fresh.example");
});

test("direction repeats advance selection while face actions remain press-only", () => {
  const env = loadBrowser();
  env.browser.dispatchCoEvent({ type: "serverListUpdated", servers: [0, 1, 2].map(index => ({ id: "s" + index, host: "127.0.0.1", port: 7777, name: "Server " + index })) });
  env.action("co.navDown");
  env.action("co.navDown", "repeat");
  assert.equal(env.state.selectedServerIndex, 2);
  env.action("co.favorite");
  env.action("co.favorite", "repeat");
  assert.equal(env.calls.filter(call => call.action === "toggleFavorite").length, 1);
  env.action("co.select");
  env.action("co.select", "repeat");
  assert.equal(env.calls.filter(call => call.action === "joinServer").length, 1);
  env.action("co.back");
  env.action("co.back", "repeat");
  assert.equal(env.calls.filter(call => call.action === "cancelJoin").length, 1);
  assert.equal(env.window.__timers.length, 0);
});

test("controller alone can edit both Direct Connect fields and connect", () => {
  const env = loadBrowser();
  env.state.activeTab = "direct-connect";
  env.state.focusZone = "tabs";
  env.browser.render();
  env.action("co.navRight");
  env.action("co.select");
  env.action("co.select");
  for (let index = 0; index < 4; index++) env.action("co.navDown");
  env.action("co.navRight");
  env.action("co.navRight");
  env.action("co.select");
  assert.equal(env.window.CO_Keyboard.isOpen(), false);
  env.action("co.navDown");
  env.action("co.select");
  for (let index = 0; index < 4; index++) env.action("co.navDown");
  for (let index = 0; index < 4; index++) env.action("co.select");
  for (let index = 0; index < 4; index++) env.action("co.navUp");
  env.action("co.select");
  for (let index = 0; index < 4; index++) env.action("co.navDown");
  env.action("co.navRight");
  env.action("co.navRight");
  env.action("co.select");
  env.action("co.navDown");
  env.action("co.navDown");
  env.action("co.select");
  const joins = env.calls.filter(call => call.action === "openDirectConnect");
  assert.equal(joins.length, 1);
  assert.equal(joins[0].address, "127.0.0.11");
  assert.equal(joins[0].port, "1");
});

test("device events change prompt glyphs and controller trace stays gated", () => {
  const env = loadBrowser();
  assert.match(env.window.CO_ControllerIcons.render("A"), /Enter/);
  env.window.dispatchEvent({ type: "prisma-input-device-change", detail: { gamepad: true, style: 1 } });
  assert.equal(env.window.CO_ControllerGlyphs.platform, "Playstation");
  assert.equal(env.window.CO_ControllerIcons.render("A"), "pad:A");
  env.action("co.refresh");
  assert.equal(env.window.CO_ControllerTrace.entries().length, 0);
  env.window.CO_ControllerTrace.setEnabled(true);
  env.action("co.refresh");
  assert.equal(env.window.CO_ControllerTrace.entries().length, 1);
  env.window.dispatchEvent({ type: "prisma-input-device-change", detail: { gamepad: false, style: 0 } });
  assert.match(env.window.CO_ControllerIcons.render("A"), /Enter/);
});

test("a passworded entry opens the password prompt and sends nothing until submit", () => {
  const env = loadBrowser();
  env.browser.dispatchCoEvent({ type: "joinPasswordRequired", serverId: "b", name: "Beta" });
  assert.equal(env.state.passwordPrompt, true);
  assert.equal(env.document.getElementById("coPasswordPrompt").classList.contains("hidden"), false);
  assert.equal(env.document.getElementById("coPasswordTitle").textContent, "Password for Beta");
  assert.equal(env.calls.length, 0);
});

test("Cancel closes the password prompt, returns to the list and sends no join", () => {
  const env = loadBrowser();
  env.browser.dispatchCoEvent({ type: "joinPasswordRequired", serverId: "b", name: "Beta" });
  env.action("co.back");
  assert.equal(env.state.passwordPrompt, false);
  assert.equal(env.document.getElementById("coPasswordPrompt").classList.contains("hidden"), true);
  assert.equal(env.browser.isVisible(), true);
  assert.equal(env.backCount(), 0);
  env.browser.dispatchCoEvent({ type: "joinPasswordRequired", serverId: "b", name: "Beta" });
  env.browser.handleControllerInput("dpad_down", "down");
  env.browser.handleControllerInput("dpad_down", "down");
  env.browser.handleControllerInput("a", "down");
  assert.equal(env.state.passwordPrompt, false);
  assert.equal(env.calls.length, 0);
});

test("submit sends joinWithPassword with the value, clears the field, and the field opens the keyboard", () => {
  const env = loadBrowser();
  env.browser.dispatchCoEvent({ type: "joinPasswordRequired", serverId: "b", name: "Beta" });
  const input = env.document.getElementById("coPasswordInput");
  assert.equal(input.getAttribute("type"), "password");
  assert.equal(input.getAttribute("maxlength"), "128");
  env.browser.handleControllerInput("a", "down");
  assert.equal(env.window.CO_Keyboard.getTarget(), input);
  env.browser.handleControllerInput("b", "down");
  assert.equal(env.state.passwordPrompt, true);
  input.value = "hunter2";
  env.browser.handleControllerInput("dpad_down", "down");
  env.browser.handleControllerInput("a", "down");
  assert.deepEqual(env.calls, [{ action: "joinWithPassword", serverId: "b", password: "hunter2" }]);
  assert.equal(input.value, "");
  assert.equal(env.state.passwordPrompt, false);
});

test("Enter in the password field submits joinWithPassword and clears the field", () => {
  const env = loadBrowser();
  env.browser.dispatchCoEvent({ type: "joinPasswordRequired", serverId: "b", name: "Beta" });
  const input = env.document.getElementById("coPasswordInput");
  input.focus();
  input.value = "hunter2";
  env.browser.handleKeydown({ key: "Enter", target: input });
  assert.deepEqual(env.calls, [{ action: "joinWithPassword", serverId: "b", password: "hunter2" }]);
  assert.equal(input.value, "");
  assert.equal(env.state.passwordPrompt, false);
});

test("a password over 128 characters is refused on the client without a join", () => {
  const env = loadBrowser();
  env.browser.dispatchCoEvent({ type: "joinPasswordRequired", serverId: "b", name: "Beta" });
  env.document.getElementById("coPasswordInput").value = "x".repeat(129);
  env.browser.handleControllerInput("dpad_down", "down");
  env.browser.handleControllerInput("a", "down");
  assert.equal(env.calls.length, 0);
  assert.equal(env.state.passwordPrompt, true);
  assert.equal(env.document.getElementById("coPasswordTitle").textContent, "Password must be 128 characters or fewer.");
});

test("a wrong-password failure shows the message and returns to the list", () => {
  const env = loadBrowser();
  env.browser.dispatchCoEvent({ type: "joinPasswordRequired", serverId: "b", name: "Beta" });
  env.document.getElementById("coPasswordInput").value = "wrong";
  env.browser.handleControllerInput("dpad_down", "down");
  env.browser.handleControllerInput("a", "down");
  assert.equal(env.calls.filter(call => call.action === "joinWithPassword").length, 1);
  env.browser.dispatchCoEvent({ type: "joinFailed", serverId: "b", reason: "Wrong password." });
  assert.equal(env.document.getElementById("coJoinMessage").textContent, "Wrong password.");
  assert.equal(env.state.joinOverlay.error, true);
  assert.equal(env.state.passwordPrompt, false);
  assert.equal(env.document.getElementById("coPasswordPrompt").classList.contains("hidden"), true);
  env.browser.handleControllerInput("a", "down");
  assert.equal(env.state.joinOverlay, null);
  assert.equal(env.browser.isVisible(), true);
  assert.equal(env.state.activeTab, "server-browser");
});

test("a modpack mismatch failure lists every differing required entry", () => {
  const env = loadBrowser();
  env.browser.dispatchCoEvent({
    type: "joinFailed",
    serverId: "b",
    reason: "Your modpack does not match this server.\nDifferences from this server:\n  Missing: Alpha.esp\n  Different content: Beta.esp\n  ...and 3 more"
  });
  const detail = env.document.getElementById("coModpackDetail");
  assert.ok(detail, "modpack detail list is mounted");
  assert.equal(env.document.getElementById("coJoinMessage").textContent, "Your modpack does not match this server.");
  assert.equal(detail.classList.contains("hidden"), false);
  assert.deepEqual(detail.children.map(item => item.textContent), ["Missing: Alpha.esp", "Different content: Beta.esp", "...and 3 more"]);
  env.browser.dispatchCoEvent({ type: "joinFailed", serverId: "b", reason: "Wrong password." });
  assert.equal(detail.classList.contains("hidden"), true);
  assert.equal(detail.children.length, 0);
});

test("a modpack mismatch without a local comparison keeps the server's required list", () => {
  const env = loadBrowser();
  env.browser.dispatchCoEvent({
    type: "joinFailed",
    serverId: "b",
    reason: "Client files were not compared on this machine.\n  Alpha.esp: required by server"
  });
  const detail = env.document.getElementById("coModpackDetail");
  assert.equal(env.document.getElementById("coJoinMessage").textContent, "Server modpack does not match.");
  assert.deepEqual(detail.children.map(item => item.textContent), ["Alpha.esp: required by server"]);
});

test("a modpack mismatch offers Copy list and copies the differences", async () => {
  const env = loadBrowser();
  let copied = "";
  env.window.navigator = { clipboard: { writeText(value) { copied = value; } } };
  env.browser.dispatchCoEvent({
    type: "joinFailed",
    serverId: "b",
    reason: "Your modpack does not match this server.\nDifferences from this server:\n  Missing: Alpha.esp\n  Wrong load order: Beta.esp"
  });
  const button = env.document.getElementById("coCopyModpackButton");
  assert.ok(button, "the copy button is mounted");
  assert.equal(button.classList.contains("hidden"), false);
  button.dispatchEvent({ type: "click", bubbles: true });
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(copied, "Missing: Alpha.esp\nWrong load order: Beta.esp");
  assert.equal(env.document.getElementById("coCopyModpackNotice").textContent, "Copied to clipboard.");
});

test("Direct Connect sends the optional password and keeps it out of browser state", () => {
  const env = loadBrowser();
  env.state.activeTab = "direct-connect";
  env.state.focusZone = "direct-form";
  env.state.directConnectFieldIndex = 2;
  env.browser.render();
  env.document.getElementById("co-dc-password").value = "pw-direct";
  env.browser.handleControllerInput("dpad_down", "down");
  assert.equal(env.document.getElementById("co-dc-password").value, "pw-direct");
  env.browser.handleControllerInput("a", "down");
  const joins = env.calls.filter(call => call.action === "openDirectConnect");
  assert.equal(joins.length, 1);
  assert.equal(joins[0].password, "pw-direct");
  assert.equal(JSON.stringify(env.state.directConnect).includes("pw-direct"), false);
  assert.equal(env.document.getElementById("co-dc-password").value, "");
});

test("Share log sends one shareLog action and renders the native notice", () => {
  const env = loadBrowser();
  const button = env.mount.querySelector("#coShareLogButton");
  const notice = env.mount.querySelector("#coShareLogNotice");
  assert.ok(button, "share log button is mounted");
  assert.ok(notice, "share log notice is mounted");

  button.click();
  assert.equal(env.calls.length, 1);
  assert.equal(env.calls[0].action, "shareLog");

  env.browser.dispatchCoEvent({ type: "shareLogResult", success: true, message: "Saved redacted log to C:/log" });
  assert.equal(notice.textContent, "Saved redacted log to C:/log");
  assert.equal(notice.classList.contains("co-share-log-notice--error"), false);

  env.browser.dispatchCoEvent({ type: "shareLogResult", success: false, message: "Log file not found." });
  assert.equal(notice.textContent, "Log file not found.");
  assert.equal(notice.classList.contains("co-share-log-notice--error"), true);
});

test("play tab keeps Start Hosting at the same pad slot and invites only while hosting", () => {
  const env = loadBrowser();
  env.state.steamP2pAvailable = true;
  env.state.activeTab = "steam-friends";
  env.state.focusZone = "steam-form";
  env.browser.render();
  assert.equal(env.window.CO_SteamFriends.fieldIds(env.state)[1], "co-steam-host");
  assert.equal(env.window.CO_Tabs.TAB_ORDER.includes("local"), false);
  assert.equal(env.window.CO_Tabs.TAB_ORDER[2], "steam-friends");
  env.state.steamHost = { status: "hosted", friendCode: "42", reason: "" };
  env.browser.render();
  const ids = env.window.CO_SteamFriends.fieldIds(env.state);
  assert.equal(ids[3], "co-steam-stop");
  assert.equal(ids.indexOf("co-steam-invite"), ids.indexOf("co-steam-copy") + 1);
  env.state.steamFieldIndex = ids.indexOf("co-steam-invite");
  env.browser.handleControllerInput("a", "down");
  assert.equal(env.calls.filter(call => call.action === "steamInvite").length, 1);
});

test("play tab scans the network and joins a LAN server without Steam", () => {
  const env = loadBrowser();
  env.state.steamP2pAvailable = false;
  env.state.activeTab = "steam-friends";
  env.state.focusZone = "steam-form";
  env.state.localServers = [{ id: "local:1", name: "Vault", host: "192.168.1.8", port: 7777, players: 1, maxPlayers: 4 }];
  env.browser.render();
  env.browser.handleControllerInput("x", "down");
  assert.equal(env.calls.filter(call => call.action === "scanLocalServers").length, 1);
  const ids = env.window.CO_SteamFriends.fieldIds(env.state);
  env.state.steamFieldIndex = ids.indexOf("co-lan-0");
  env.browser.handleControllerInput("a", "down");
  const joins = env.calls.filter(call => call.action === "joinServer");
  assert.equal(joins.length, 1);
  assert.equal(joins[0].serverId, "local:1");
});
