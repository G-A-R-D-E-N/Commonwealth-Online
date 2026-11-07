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
