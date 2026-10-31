const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { createEnvironment, createElement } = require("./dom.js");
const browserPath = path.join(__dirname, "..");

const COMPONENTS = [
  "tabs", "filters", "controller-input", "text-focus", "keyboard", "controller-trace", "controller-icons",
  "controller-prompts", "direct-connect", "steam-lan", "steam-friends", "server-settings", "browser-markup",
  "server-records", "recent-servers", "filter-state", "server-counts", "join-flow", "password-prompt",
  "share-log", "controller-navigation", "directory-refresh", "mock-data"
];

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
  for (const name of COMPONENTS) {
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

function settingsEnv() {
  const env = loadBrowser();
  env.window.dispatchCoEvent({ type: "capabilities", steamP2p: false });
  env.state.activeTab = "steam-friends";
  env.browser.render();
  return env;
}

function resultEvent(overrides) {
  return Object.assign({
    type: "serverSettingsResult",
    action: "serverSettingsGet",
    success: true,
    data: {
      serverDirectory: "C:/Games/CO",
      configPath: "C:/Games/CO/commonwealth-server.json",
      errors: [],
      limits: { maximumPlayers: 16, worldModes: ["coop-story", "free-roam"], vatsModes: ["disabled", "real-time", "vanilla"] },
      values: {
        serverName: "Vault 111",
        serverDescription: "Co-op run",
        passwordConfigured: true,
        maxPlayers: 8,
        worldMode: "free-roam",
        vats: "real-time",
        doorsShared: false,
        friendlyFire: false,
        factionFriendlyFire: true,
        downedSeconds: 45,
        enableChat: true,
        enableFactions: false,
        enablePersistence: true,
        enableObjectState: false,
        lanDiscoveryEnabled: true,
        publicListing: false,
        resumeTokenLifetimeHours: 240
      }
    }
  }, overrides || {});
}

function steamPane(env) {
  return env.mount.querySelector('[data-pane="steam-friends"]');
}

function steamHtml(env) {
  const container = steamPane(env).querySelector(".co-steam");
  return container ? container.innerHTML : "";
}

function click(el) {
  el.dispatchEvent({ type: "click", bubbles: true });
}

module.exports = { settingsEnv, resultEvent, steamPane, steamHtml, click };
