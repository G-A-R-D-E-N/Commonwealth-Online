const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const path = require("node:path");

const root = __dirname;

{
  const window = {};
  runScript("keyboard-navigation.js", { window });
  for (const [key, expected] of [["ArrowUp", -1], ["ArrowDown", 1], ["Enter", "activate"], [" ", "activate"], ["Escape", "back"], ["Tab", null]]) {
    let action = null;
    let prevented = false;
    window.CO_MainMenuKeys({ key, preventDefault: () => { prevented = true; } },
      value => { action = value; }, () => { action = "activate"; }, () => { action = "back"; });
    assert.equal(action, expected);
    assert.equal(prevented, expected !== null);
  }
}

function runScript(name, context) {
  const source = fs.readFileSync(path.join(root, name), "utf8");
  vm.runInNewContext(source, context, { filename: name });
}

const mainMenuScript = fs.readFileSync(path.join(root, "script.js"), "utf8");
assert.match(mainMenuScript, /action === "singlePlayer"[\s\S]*sendCoAction\(\{ action: "singlePlayer" \}\)/);
assert.doesNotMatch(mainMenuScript, /single-player\.js/);
assert.strictEqual(fs.existsSync(path.join(root, "single-player.js")), false);

const listeners = {};
const launcher = {
  getBoundingClientRect: () => ({ left: 1700, top: 24, width: 220, height: 54 }),
  addEventListener: (name, callback) => { listeners[name] = callback; }
};
const launcherEvents = [];
const launcherContext = {
  document: {
    documentElement: { style: { setProperty: () => {} } },
    getElementById: () => launcher
  },
  window: {
    innerWidth: 1920,
    innerHeight: 1080,
    coLauncherHitRegion: (payload) => launcherEvents.push(["region", JSON.parse(payload)]),
    coLauncherPointerFocus: (payload) => launcherEvents.push(["focus", payload]),
    coLauncherAction: (payload) => launcherEvents.push(["action", payload]),
    addEventListener: () => {}
  }
};
runScript("../launcher/script.js", launcherContext);
listeners.pointerenter();
listeners.pointerleave();
listeners.click();
assert.deepEqual(launcherEvents, [
  ["region", { generation: 0, active: true, left: 1700, top: 24, width: 220, height: 54 }],
  ["focus", '{"generation":0,"focus":true}'],
  ["focus", '{"generation":0,"focus":false}'],
  ["action", '{"generation":0,"action":"restore"}']
]);

const html = fs.readFileSync(path.join(root, "index.html"), "utf8");
assert.ok(html.indexOf('data-action="multiplayer"') < html.indexOf('data-action="settings"'));
assert.ok(html.indexOf('data-action="singlePlayer"') < html.indexOf('data-action="settings"'));
assert.ok(html.indexOf('data-action="singlePlayer"') < html.indexOf('data-action="quit"'));

const css = fs.readFileSync(path.join(root, "styles.css"), "utf8");
assert.doesNotMatch(html, /class="multiplayer-logo"/);
assert.match(css, /pointer-events:\s*none/);
assert.match(css, /prefers-reduced-motion/);

function fakeElement(overrides) {
  const classes = new Set();
  const listeners = {};
  return Object.assign({
    hidden: false,
    tabIndex: 0,
    offsetWidth: 0,
    style: {},
    classList: {
      add: (...names) => names.forEach(name => classes.add(name)),
      remove: (...names) => names.forEach(name => classes.delete(name)),
      contains: name => classes.has(name),
      toggle: (name, force) => {
        const on = force === undefined ? !classes.has(name) : Boolean(force);
        if (on) {
          classes.add(name);
        } else {
          classes.delete(name);
        }
        return on;
      }
    },
    getAttribute: () => null,
    setAttribute: () => {},
    querySelectorAll: () => [],
    addEventListener: (type, callback) => {
      listeners[type] = (listeners[type] || []).concat([callback]);
    },
    removeEventListener: (type, callback) => {
      listeners[type] = (listeners[type] || []).filter(fn => fn !== callback);
    },
    dispatch: type => (listeners[type] || []).forEach(callback => callback({ type }))
  }, overrides);
}

function mountMainMenu() {
  const actions = ["multiplayer", "settings", "singlePlayer", "quit"];
  const items = actions.map(action => fakeElement({
    getAttribute: name => (name === "data-action" ? action : null)
  }));
  const byId = {
    mainScreen: fakeElement(),
    menu: fakeElement({ querySelectorAll: () => items }),
    settingsPanel: fakeElement({ hidden: true }),
    browserPanel: fakeElement({ hidden: true }),
    quitConfirmPanel: fakeElement({ hidden: true }),
    quitConfirmOptions: fakeElement(),
    mainPromptBar: fakeElement(),
    settingsPromptBar: fakeElement({ hidden: true })
  };
  const timers = new Map();
  let nextTimerId = 1;
  let settingsCreates = 0;
  let settingsOptions = null;
  let voiceAttaches = [];
  const controller = {
    show() {},
    hide() {},
    back() {},
    onShown() {},
    setPointerMode() {},
    handleKeydown() {},
    handleControllerInput() {}
  };
  const windowStub = {
    innerWidth: 1920,
    innerHeight: 1080,
    addEventListener() {},
    setTimeout(callback) {
      timers.set(nextTimerId, callback);
      nextTimerId += 1;
      return nextTimerId - 1;
    },
    clearTimeout(id) {
      timers.delete(id);
    },
    CO_Settings: { create: options => { settingsCreates += 1; settingsOptions = options; return controller; } },
    CO_VoiceSettings: { forwardSetting: () => false, attach: (...args) => { voiceAttaches.push(args); } },
    CO_Browser: { create: () => controller }
  };
  runScript("keyboard-navigation.js", { window: windowStub });
  runScript("script.js", {
    window: windowStub,
    document: {
      documentElement: { style: { setProperty() {} } },
      getElementById: id => byId[id],
      querySelectorAll: () => [],
      addEventListener() {}
    },
    getComputedStyle: () => ({ getPropertyValue: () => "" }),
    console: { log() {}, warn() {} }
  });
  return {
    byId,
    settingsCreates: () => settingsCreates,
    settingsOptions: () => settingsOptions,
    voiceAttaches: () => voiceAttaches,
    controller,
    bridge: windowStub.CommonwealthOnlineUI,
    multiplayer: items[0],
    settings: items[1],
    quit: items[3],
    closeBrowser: () => windowStub.closeBrowser(),
    flushTimers() {
      [...timers.keys()].forEach(id => {
        const callback = timers.get(id);
        timers.delete(id);
        callback();
      });
    }
  };
}

{
  const menu = mountMainMenu();
  assert.equal(menu.settingsCreates(), 0, "settings stay unbuilt during initial page setup");
  menu.settings.dispatch("click");
  assert.equal(menu.settingsCreates(), 1);
  assert.equal(menu.voiceAttaches().length, 1, "voice settings attach to the first controller");
  const [attachedController, attachedBridge] = menu.voiceAttaches()[0];
  assert.equal(attachedController, menu.controller, "voice settings attach the controller settings created");
  assert.equal(attachedBridge, menu.bridge, "voice settings attach with the CO bridge");
  assert.equal(menu.settingsOptions().bridge, menu.bridge, "settings are created with the CO bridge");
  menu.multiplayer.dispatch("click");
  menu.settings.dispatch("click");
  assert.equal(menu.settingsCreates(), 1, "settings reuse the first controller");
  assert.equal(menu.voiceAttaches().length, 1, "voice settings attach once");
}
{
  const menu = mountMainMenu();
  menu.multiplayer.dispatch("click");
  menu.flushTimers();
  menu.settings.dispatch("click");
  menu.flushTimers();
  assert.equal(menu.byId.mainPromptBar.hidden, true, "main footer stays hidden in settings");
}
{
  const menu = mountMainMenu();
  menu.settings.dispatch("click");
  menu.flushTimers();
  menu.multiplayer.dispatch("click");
  menu.flushTimers();
  assert.equal(menu.byId.mainPromptBar.hidden, true, "main footer stays hidden in browser");
}
{
  const menu = mountMainMenu();
  menu.multiplayer.dispatch("click");
  menu.flushTimers();
  menu.closeBrowser();
  menu.flushTimers();
  assert.equal(menu.byId.mainPromptBar.hidden, false, "main footer returns after browser closes");
}
{
  const menu = mountMainMenu();
  menu.multiplayer.dispatch("click");
  menu.flushTimers();
  menu.quit.dispatch("click");
  menu.flushTimers();
  assert.equal(menu.byId.mainPromptBar.hidden, false, "main footer stays visible in quit confirm");
}
