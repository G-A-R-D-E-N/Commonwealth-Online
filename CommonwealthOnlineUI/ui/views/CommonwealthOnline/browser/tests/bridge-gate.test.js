const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { createEnvironment, createElement } = require("./dom.js");
const browserPath = path.join(__dirname, "..");

function loadBrowser(mocksFlag) {
  const { window, document } = createEnvironment();
  const mount = createElement("div");
  mount.ownerDocument = document;
  mount.parentNode = document.body;
  document.body.children.push(mount);
  document.hasFocus = () => true;
  window.CO_ControllerGlyphs = { platform: "Xbox", setPlatform() {}, hydrate() {}, render(key) { return "pad:" + key; } };
  window.CO_ServerList = { render() {}, scrollToSelected() {}, isLocalHost() { return false; }, displayName(server) { return server.name; } };
  window.CO_ServerDetails = { render() {} };
  if (mocksFlag !== undefined) window.CO_ENABLE_MOCKS = mocksFlag;
  const context = vm.createContext(window);
  for (const name of ["tabs", "filters", "controller-input", "text-focus", "keyboard", "controller-trace", "controller-icons", "controller-prompts", "direct-connect", "steam-friends", "browser-markup", "server-records", "recent-servers", "filter-state", "server-counts", "join-flow", "password-prompt", "share-log", "modpack-detail", "controller-navigation", "directory-refresh", "mock-data"]) {
    const file = path.join(browserPath, "components", name + ".js");
    if (fs.existsSync(file)) vm.runInContext(fs.readFileSync(file, "utf8"), context);
  }
  vm.runInContext(fs.readFileSync(path.join(browserPath, "browser.js"), "utf8"), context);
  const browser = window.CO_Browser.create({ mount, layout: "embedded", exposeGlobals: true, onBack() {} });
  browser.show();
  const state = window.CO_App.getState();
  function action(name) {
    window.dispatchEvent({ type: "prisma-controller-action", detail: { action: name, state: "pressed" } });
  }
  return { window, state, action };
}

test("without the coAction bridge or CO_ENABLE_MOCKS, refresh shows the bridge error and adds no mock servers", () => {
  const env = loadBrowser(undefined);
  env.action("co.refresh");
  env.window.__flushTimers();
  assert.equal(env.state.allServers.length, 0);
  assert.equal(env.state.connectionStatus, "Unavailable");
  assert.equal(env.state.joinOverlay.error, true);
  assert.equal(env.state.joinOverlay.message, "Server browser bridge unavailable.");
});

test("without the coAction bridge or CO_ENABLE_MOCKS, Local scan adds no mock servers", () => {
  const env = loadBrowser(false);
  env.state.activeTab = "local";
  env.action("co.refresh");
  env.window.__flushTimers();
  assert.equal(env.state.localServers.length, 0);
  assert.equal(env.state.joinOverlay.error, true);
});

test("with CO_ENABLE_MOCKS true, refresh still uses the mock directory", () => {
  const env = loadBrowser(true);
  env.action("co.refresh");
  env.window.__flushTimers();
  assert.ok(env.state.allServers.some(server => server.id === "local-relay"));
  assert.equal(env.state.joinOverlay, null);
});

test("with CO_ENABLE_MOCKS true, Reset Filters completes without recursing", () => {
  const env = loadBrowser(true);
  env.state.filters.region = "Europe";
  env.state.focusZone = "filters";
  env.state.selectedFilterIndex = env.window.CO_Filters.FILTER_ROWS.length;
  env.action("co.select");
  assert.equal(env.state.filters.region, "Any Region");
  assert.equal(env.state.joinOverlay, null);
});

test("with CO_ENABLE_MOCKS true, the favorite toggle completes and shows in favorites", () => {
  const env = loadBrowser(true);
  env.action("co.refresh");
  env.window.__flushTimers();
  const target = env.state.servers[0];
  assert.ok(target, "the mock directory lists at least one server");
  env.state.selectedServerIndex = 0;
  env.action("co.favorite");
  assert.equal(env.state.allServers.find(server => server.id === target.id).favorite, true);
  assert.ok(env.state.favorites.some(server => server.id === target.id));
  assert.equal(env.state.joinOverlay, null);
});

test("with CO_ENABLE_MOCKS true, joining completes with the connecting overlay", () => {
  const env = loadBrowser(true);
  env.action("co.refresh");
  env.window.__flushTimers();
  env.state.selectedServerIndex = 0;
  env.window.CO_App.joinSelected();
  assert.equal(env.state.joinOverlay.error, false);
  assert.match(env.state.joinOverlay.message, /^Connecting to /);

  env.state.servers = [{ id: "direct-mock", host: "203.0.113.5", port: 7777, name: "Mock Direct" }];
  env.state.selectedServerIndex = 0;
  env.window.CO_App.joinSelected();
  assert.equal(env.state.joinOverlay.error, false);
  assert.equal(env.state.joinOverlay.message, "Connecting to Mock Direct...");
});
