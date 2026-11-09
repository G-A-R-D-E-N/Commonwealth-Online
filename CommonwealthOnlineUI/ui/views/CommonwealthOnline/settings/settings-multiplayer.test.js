const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const path = require("node:path");
const { createEnvironment, createElement } = require("../browser/tests/dom.js");

function mountSettings({ native = true } = {}) {
  const { window, document } = createEnvironment();
  const mount = createElement("div");
  mount.ownerDocument = document;
  document.body.appendChild(mount);
  const calls = [];
  if (native) {
    window.coSettingsAction = raw => calls.push(JSON.parse(raw));
  } else {
    window.console = { log() {}, warn() {} };
  }
  const context = vm.createContext(window);
  for (const file of ["settings-data.js", "settings-model.js", "settings-native.js", "settings-render.js", "settings-input.js", "settings.js"]) {
    vm.runInContext(fs.readFileSync(path.join(__dirname, file), "utf8"), context);
  }
  const settings = window.CO_Settings.create({ mount });
  settings.show();
  const gameRequest = calls.find(call => call.action === "settingsGetAll");
  if (gameRequest) {
    settings.handleSettingsEvent({ type: "settingsResult", requestId: gameRequest.requestId,
      action: "settingsGetAll", success: true, data: { settings: [] } });
  }
  return { window, mount, calls, settings };
}

function multiplayerRow(settings, key) {
  return settings.getSettingsData().flatMap(section => section.items).find(item => item.key === key);
}

function lastRequest(calls, action) {
  return calls.filter(call => call.action === action).at(-1);
}

function reply(window, request, success, data, error) {
  window.dispatchCoSettingsEvent({ type: "settingsResult", requestId: request.requestId,
    action: request.action, success, data, error });
}

function openMultiplayerAndToggle(mount, key) {
  mount.querySelector('[data-tab="multiplayer"]').dispatchEvent({ type: "click", bubbles: true });
  mount.querySelector(`[data-setting-key="${key}"]`).dispatchEvent({ type: "click", bubbles: true });
}

test("show loads native multiplayer settings and keeps only values that fit each row", () => {
  const { window, settings, calls } = mountSettings();
  const get = lastRequest(calls, "settingsGetMultiplayerSettings");
  assert.ok(get, "show() must request the native multiplayer settings");
  reply(window, get, true, { settings: {
    "map.showPlayerMarkers": false,
    "profile.onlineStatus": "Nobody",
    "map.mapMarkerSize": "Huge",
    "network.showPing": "no"
  } });
  assert.equal(multiplayerRow(settings, "map.showPlayerMarkers").value, false);
  assert.equal(multiplayerRow(settings, "profile.onlineStatus").value, "Nobody");
  assert.equal(multiplayerRow(settings, "map.mapMarkerSize").value, "Medium");
  assert.equal(multiplayerRow(settings, "network.showPing").value, true);
});

test("every show() refetches the native multiplayer settings", () => {
  const { calls, settings } = mountSettings();
  settings.show();
  assert.equal(calls.filter(call => call.action === "settingsGetMultiplayerSettings").length, 2);
});

test("a late native reply does not overwrite a value the user already changed", () => {
  const { window, mount, calls, settings } = mountSettings();
  const get = lastRequest(calls, "settingsGetMultiplayerSettings");
  openMultiplayerAndToggle(mount, "map.showPlayerMarkers");
  reply(window, get, true, { settings: { "map.showPlayerMarkers": true } });
  assert.equal(multiplayerRow(settings, "map.showPlayerMarkers").value, false);
});

test("toggling a multiplayer row sends the key and value to the native store", () => {
  const { mount, calls } = mountSettings();
  openMultiplayerAndToggle(mount, "map.showPlayerMarkers");
  const set = lastRequest(calls, "settingsSetMultiplayerSetting");
  assert.ok(set, "a multiplayer change must reach the native store");
  assert.equal(set.key, "map.showPlayerMarkers");
  assert.equal(set.value, false);
});

test("a native error reverts the toggle and shows the native message", () => {
  const { window, mount, calls, settings } = mountSettings();
  openMultiplayerAndToggle(mount, "map.showPlayerMarkers");
  assert.equal(multiplayerRow(settings, "map.showPlayerMarkers").value, false);
  reply(window, lastRequest(calls, "settingsSetMultiplayerSetting"), false, undefined,
    { code: "VALUE_OUT_OF_RANGE", message: "Pick a valid value." });
  assert.equal(multiplayerRow(settings, "map.showPlayerMarkers").value, true);
  assert.match(mount.querySelector("[data-co-settings-description]").textContent, /Pick a valid value\./);
});

test("a successful native save keeps the new value without reverting", () => {
  const { window, mount, calls, settings } = mountSettings();
  openMultiplayerAndToggle(mount, "map.showPlayerMarkers");
  const set = lastRequest(calls, "settingsSetMultiplayerSetting");
  reply(window, set, true, { key: "map.showPlayerMarkers", value: false });
  assert.equal(multiplayerRow(settings, "map.showPlayerMarkers").value, false);
});

test("with no native bridge the toggle keeps the new value and does not revert", () => {
  const { mount, settings } = mountSettings({ native: false });
  openMultiplayerAndToggle(mount, "map.showPlayerMarkers");
  assert.equal(multiplayerRow(settings, "map.showPlayerMarkers").value, false);
  assert.doesNotMatch(mount.querySelector("[data-co-settings-description]").textContent, /Native|unavailable|CommonwealthOnline/);
});

test("every browser-region reply reaches the browser listener, not only the first", () => {
  const { window } = mountSettings();
  const seen = [];
  window.coSettingsResultListener = event => seen.push(event.data.settings["network.regionPreference"]);
  const send = region => window.dispatchCoSettingsEvent({ type: "settingsResult", requestId: "browser-region",
    action: "settingsGetMultiplayerSettings", success: true, data: { settings: { "network.regionPreference": region } } });
  send("Auto");
  send("Europe");
  assert.deepEqual(seen, ["Auto", "Europe"]);
});

test("a region reply that arrives before Settings is ever opened still reaches the browser", () => {
  const html = fs.readFileSync(path.join(__dirname, "../main-menu/index.html"), "utf8");
  const stub = /<script>\s*\/\/ Early stub[\s\S]*?<\/script>/.exec(html)[0].replace(/<\/?script>/g, "");
  const window = {};
  vm.runInNewContext(stub, { window });
  const seen = [];
  window.coSettingsResultListener = event => seen.push(event.data.settings["network.regionPreference"]);
  window.dispatchCoSettingsEvent(JSON.stringify({ type: "settingsResult", requestId: "browser-region",
    action: "settingsGetMultiplayerSettings", success: true, data: { settings: { "network.regionPreference": "Asia" } } }));
  assert.deepEqual(seen, ["Asia"]);
  assert.equal((window.__coSettingsEventQueue || []).length, 0);
});
