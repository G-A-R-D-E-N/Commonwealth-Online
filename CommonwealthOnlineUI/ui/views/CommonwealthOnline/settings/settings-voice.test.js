const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const dataSource = fs.readFileSync(path.join(__dirname, "settings-data.js"), "utf8");
const voiceSource = fs.readFileSync(path.join(__dirname, "settings-voice.js"), "utf8");
const moduleSources = ["settings-model.js", "settings-native.js", "settings-render.js", "settings-input.js"].map((name) => [name, fs.readFileSync(path.join(__dirname, name), "utf8")]);
const settingsSource = fs.readFileSync(path.join(__dirname, "settings.js"), "utf8");
const PROMPT = "Press a key (Esc cancels)";
const SENSITIVITY_SELECTOR = '[data-setting-key="voice.openMicSensitivity"]';
const KEYBINDS = ["voice.pushToTalkKey", "voice.whisperKey", "voice.shoutKey"];
const STATUS_CASES = [
  ["ready", "Ready"], ["transmitting", "Transmitting"],
  ["idle", "Idle"], ["serverNoVoice", "This server does not support voice chat"],
  ["micFailed", "Microphone not available"], ["micRecovering", "Reconnecting microphone"],
  ["micSilentHint", "Your microphone may be muted in Windows"], ["noOutput", "Voice playback failed: no audio output device"],
  ["serverMuted", "You are muted by the server"], ["rateLimited", "Voice is rate limited by the server"],
  ["profileMismatch", "Voice profile mismatch with the server"], ["deafened", "Deafened"],
  ["micMuted", "Microphone muted"], ["codecFailed", "Voice encoding failed; retry voice"]
];

const plain = (value) => JSON.parse(JSON.stringify(value));

function makeElement() {
  return {
    style: { props: {}, setProperty(name, value) { this.props[name] = value; } },
    children: {},
    className: "",
    querySelector(selector) { return this.children[selector] || null; },
    appendChild(child) { this.children["." + child.className] = child; return child; }
  };
}

function makeDomElement(registry) {
  const el = {
    attributes: {},
    children: {},
    classList: { names: new Set(), add(name) { this.names.add(name); }, toggle(name, force) { force ? this.names.add(name) : this.names.delete(name); } },
    addEventListener() {},
    setAttribute(name, value) {
      this.attributes[name] = String(value);
    },
    appendChild(child) {
      return child;
    },
    querySelector(selector) {
      this.children[selector] = this.children[selector] || makeDomElement(registry);
      return this.children[selector];
    }
  };
  registry.push(el);
  return el;
}

function makeWorld(options) {
  const config = Object.assign({ action: true, attach: true, rowRendered: true }, options);
  const sent = [];
  const warnings = [];
  const rowEl = makeElement();
  const window = { CO_VoiceEvents: config.early || [] };
  const document = {
    querySelector(selector) {
      return config.rowRendered && selector === SENSITIVITY_SELECTOR ? rowEl : null;
    },
    createElement() {
      return makeElement();
    }
  };
  const context = { window, document, console: { warn: (...args) => warnings.push(args) } };
  vm.runInNewContext(dataSource, context, { filename: "settings-data.js" });
  vm.runInNewContext(voiceSource, context, { filename: "settings-voice.js" });
  if (config.action) {
    window.coVoiceAction = (json) => sent.push(JSON.parse(json));
  }
  const data = window.CO_SettingsData;
  const controller = {
    renders: 0,
    getSettingsData() {
      return data;
    },
    render() {
      controller.renders += 1;
    }
  };
  const bridge = {};
  if (config.attach) {
    window.CO_VoiceSettings.attach(controller, bridge);
  }
  return {
    window,
    api: window.CO_VoiceSettings,
    bridge,
    sent,
    warnings,
    rowEl,
    controller,
    data,
    row(key) {
      return data.flatMap((section) => section.items).find((item) => item.key === key);
    }
  };
}

{
  const w = makeWorld();
  const voice = w.data.find((section) => section.section === "VOICE CHAT").items;
  assert.deepEqual(plain(voice.map((item) => item.key)), [
    "voice.voiceChat", "voice.pushToTalk", "voice.pushToTalkKey", "voice.whisperKey", "voice.shoutKey",
    "voice.openMicSensitivity", "voice.voiceChatVolume", "voice.mutePlayersByDefault", "voice.deafened", "voice.micMuted", "voice.status"
  ]);
  assert.equal(w.row("voice.voiceChat").description, "Enable voice chat with other players. Voice is relayed through the server or host.");
  assert.deepEqual(plain([w.row("voice.pushToTalk").type, w.row("voice.pushToTalk").value]), ["toggle", true]);
  assert.deepEqual(plain([w.row("voice.pushToTalkKey").type, w.row("voice.pushToTalkKey").value]), ["keybind", "B"]);
  assert.deepEqual(plain([w.row("voice.whisperKey").type, w.row("voice.whisperKey").value, w.row("voice.whisperKey").label]), ["keybind", "", "Whisper Key"]);
  const sens = w.row("voice.openMicSensitivity");
  assert.deepEqual(plain([sens.type, sens.value, sens.min, sens.max, sens.step]), ["slider", -45, -80, 0, 5]);
  const volume = w.row("voice.voiceChatVolume");
  assert.deepEqual(plain([volume.value, volume.min, volume.max]), [85, 0, 100]);
  assert.deepEqual(plain([w.row("voice.deafened").value, w.row("voice.micMuted").value]), [false, false]);
  assert.deepEqual(plain([w.row("voice.status").type, w.row("voice.status").value]), ["text", "Not connected"]);
}

{
  const w = makeWorld();
  assert.equal(w.api.forwardSetting("voice.pushToTalkKey", "V"), true);
  assert.equal(w.api.forwardSetting("voice.voiceChatVolume", 40), true);
  assert.equal(w.api.forwardSetting("voice.deafened", true), true);
  assert.deepEqual(w.sent, [
    { op: "setSetting", key: "voice.pushToTalkKey", value: "V" },
    { op: "setSetting", key: "voice.voiceChatVolume", value: 40 },
    { op: "setSetting", key: "voice.deafened", value: true }
  ]);
  assert.equal(w.api.forwardSetting("voice.status", "x"), false);
  assert.equal(w.api.forwardSetting("map.showPlayerMarkers", false), false);
  assert.equal(w.api.forwardSetting(undefined, 1), false);
  assert.equal(w.sent.length, 3);
}

{
  const w = makeWorld({ action: false });
  assert.equal(w.api.forwardSetting("voice.deafened", true), true);
  assert.equal(w.warnings.length, 1);
  assert.equal(w.bridge.beginKeyCapture("voice.whisperKey"), false);
  assert.equal(w.warnings.length, 2);
  assert.equal(w.row("voice.whisperKey").value, "");
}

{
  const w = makeWorld();
  assert.equal(w.api.beginKeyCapture, w.bridge.beginKeyCapture);
  KEYBINDS.forEach((key) => {
    assert.equal(w.bridge.beginKeyCapture(key), true);
  });
  assert.deepEqual(w.sent, [
    { op: "beginKeyCapture", target: "voice.pushToTalkKey" },
    { op: "beginKeyCapture", target: "voice.whisperKey" },
    { op: "beginKeyCapture", target: "voice.shoutKey" }
  ]);
  assert.equal(w.bridge.beginKeyCapture("voice.pushToTalk"), false);
  assert.equal(w.bridge.beginKeyCapture("voice.status"), false);
  assert.equal(w.bridge.beginKeyCapture("voice.voiceChatVolume"), false);
  assert.equal(w.sent.length, 3);
}

{
  const w = makeWorld();
  w.bridge.beginKeyCapture("voice.whisperKey");
  assert.equal(w.row("voice.whisperKey").value, PROMPT);
  w.bridge.beginKeyCapture("voice.shoutKey");
  assert.equal(w.row("voice.whisperKey").value, "");
  assert.equal(w.row("voice.shoutKey").value, PROMPT);
}

{
  const w = makeWorld();
  w.bridge.beginKeyCapture("voice.whisperKey");
  w.api.handleEvent({ kind: "keyCapture", target: "voice.whisperKey", state: "captured", name: "Left Shift", vk: 160 });
  assert.equal(w.row("voice.whisperKey").value, "Left Shift");
  w.api.handleEvent({ kind: "keyCapture", target: "voice.pushToTalkKey", state: "captured", name: "F", vk: 70 });
  assert.equal(w.row("voice.pushToTalkKey").value, "F");
}

{
  const w = makeWorld();
  w.row("voice.shoutKey").value = "Q";
  w.bridge.beginKeyCapture("voice.shoutKey");
  assert.equal(w.row("voice.shoutKey").value, PROMPT);
  w.api.handleEvent({ kind: "keyCapture", target: "voice.shoutKey", state: "cancelled", name: "", vk: 0 });
  assert.equal(w.row("voice.shoutKey").value, "Q");
}

{
  const w = makeWorld();
  w.row("voice.shoutKey").value = "Q";
  w.bridge.beginKeyCapture("voice.shoutKey");
  w.api.handleEvent({ kind: "settings", values: { "voice.shoutKey": "R" } });
  assert.equal(w.row("voice.shoutKey").value, PROMPT);
  w.api.handleEvent({ kind: "keyCapture", target: "voice.shoutKey", state: "cancelled", name: "", vk: 0 });
  assert.equal(w.row("voice.shoutKey").value, "R");
}

{
  const w = makeWorld();
  w.bridge.beginKeyCapture("voice.whisperKey");
  w.api.handleEvent({ kind: "keyCapture", target: "voice.shoutKey", state: "cancelled", name: "", vk: 0 });
  assert.equal(w.row("voice.whisperKey").value, PROMPT);
  assert.equal(w.row("voice.shoutKey").value, "");
}

{
  const w = makeWorld();
  const before = w.controller.renders;
  w.api.handleEvent({
    kind: "settings",
    values: {
      "voice.pushToTalkKey": "F",
      "voice.voiceChatVolume": 30,
      "voice.deafened": true,
      "voice.openMicSensitivity": -60,
      "voice.status": "x",
      "map.showPlayerMarkers": false
    }
  });
  assert.equal(w.row("voice.pushToTalkKey").value, "F");
  assert.equal(w.row("voice.voiceChatVolume").value, 30);
  assert.equal(w.row("voice.deafened").value, true);
  assert.equal(w.row("voice.openMicSensitivity").value, -60);
  assert.equal(w.row("voice.status").value, "Not connected");
  assert.equal(w.row("map.showPlayerMarkers").value, true);
  assert.equal(w.row("voice.deafened").dirty, undefined);
  assert.equal(w.controller.renders, before + 1);
  w.api.handleEvent({ kind: "settings", values: { "voice.voiceChatVolume": "loud", "voice.micMuted": "yes" } });
  assert.equal(w.row("voice.voiceChatVolume").value, 30);
  assert.equal(w.row("voice.micMuted").value, false);
}

{
  const w = makeWorld();
  for (const [code, text] of STATUS_CASES) {
    w.api.handleEvent({ kind: "status", active: true, code, pending: false, micHot: false });
    assert.equal(w.row("voice.status").value, text, code);
    w.api.handleEvent({ kind: "status", active: true, code, pending: true, micHot: false });
    assert.equal(w.row("voice.status").value, text + " (syncing)", code);
  }
  w.api.handleEvent({ kind: "status", active: true, code: "notACode", pending: false, micHot: false });
  assert.equal(w.row("voice.status").value, "Unknown voice state");
  w.api.handleEvent({ kind: "status", active: true, code: "constructor", pending: false, micHot: false });
  assert.equal(w.row("voice.status").value, "Unknown voice state");
  w.api.handleEvent({ kind: "status", active: false, code: "ready", pending: true, micHot: false });
  assert.equal(w.row("voice.status").value, "Not connected");
  w.api.handleEvent({ kind: "status", active: true, code: "idle", pending: false, micHot: false });
  [{ active: "yes" }, { active: 1 }, {}].forEach((extra) => {
    w.api.handleEvent(Object.assign({ kind: "status", code: "ready", pending: false, micHot: false }, extra));
  });
  assert.equal(w.row("voice.status").value, "Idle");
  w.api.handleEvent({ kind: "status", active: false, code: "ready", pending: false, micHot: false });
  assert.equal(w.row("voice.status").value, "Not connected");
}

{
  const w = makeWorld();
  w.api.handleEvent({ kind: "level", dbfs: "loud" });
  w.api.handleEvent({ kind: "level" });
  assert.equal(w.rowEl.querySelector(".settings-meter"), null);
  w.api.handleEvent({ kind: "level", dbfs: -60 });
  const meter = w.rowEl.querySelector(".settings-meter");
  assert.equal(meter.style.props["--co-meter-level"], "25%");
  assert.equal(meter.style.props["--co-meter-threshold"], "43.75%");
  w.row("voice.openMicSensitivity").value = -20;
  w.api.handleEvent({ kind: "level", dbfs: 0 });
  assert.equal(meter.style.props["--co-meter-level"], "100%");
  assert.equal(meter.style.props["--co-meter-threshold"], "75%");
  w.api.handleEvent({ kind: "level", dbfs: -200 });
  assert.equal(meter.style.props["--co-meter-level"], "0%");
}

{
  const w = makeWorld({ rowRendered: false });
  assert.doesNotThrow(() => w.api.handleEvent({ kind: "level", dbfs: -30 }));
  assert.equal(w.rowEl.querySelector(".settings-meter"), null);
}

{
  const w = makeWorld();
  const malformed = [
    null, undefined, "settings", 42, true, [], ["status"], {}, { kind: 7 }, { kind: "status" },
    { kind: "status", active: true, code: {} }, { kind: "settings" }, { kind: "settings", values: "x" },
    { kind: "settings", values: null }, { kind: "keyCapture", target: "voice.whisperKey", state: "captured", name: 5 },
    { kind: "keyCapture", target: "voice.status", state: "captured", name: "x" },
    { kind: "keyCapture", target: "voice.whisperKey", state: "pressed" }, { kind: "level" }, { kind: "level", dbfs: "x" },
    { kind: "players", players: [{ playerId: 1, name: "A" }] }, { kind: "speakers", playerIds: [1] }, { kind: "unknown" }
  ];
  malformed.forEach((event) => {
    assert.doesNotThrow(() => w.window.coVoiceEvent(event));
  });
  assert.equal(w.row("voice.whisperKey").value, "");
  assert.equal(w.row("voice.pushToTalkKey").value, "B");
}

{
  const w = makeWorld();
  const before = w.controller.renders;
  w.window.coVoiceEvent({ kind: "players", players: [] });
  w.window.coVoiceEvent({ kind: "speakers", playerIds: [1] });
  w.window.coVoiceEvent({ kind: "unknown" });
  w.window.coVoiceEvent("settings");
  assert.equal(w.controller.renders, before);
  assert.equal(w.window.coVoiceEvent, w.api.handleEvent);
}

function makeSettingsWorld(tab) {
  const clock = { now: 0 };
  const sent = [], logged = [];
  const window = {};
  const registry = [];
  const rowsEl = makeDomElement(registry);
  const mount = { innerHTML: "", querySelector: (selector) => (selector === "[data-co-settings-rows]" ? rowsEl : null) };
  const document = { createElement: () => makeDomElement(registry), querySelector: () => null };
  const context = {
    window,
    document,
    console: { log() {}, warn() {} },
    setTimeout: () => 1,
    clearTimeout() {},
    Date: { now: () => clock.now }
  };
  vm.runInNewContext(dataSource, context, { filename: "settings-data.js" });
  moduleSources.forEach(([name, source]) => vm.runInNewContext(source, context, { filename: name }));
  vm.runInNewContext(settingsSource, context, { filename: "settings.js" });
  vm.runInNewContext(voiceSource, context, { filename: "settings-voice.js" });
  window.coVoiceAction = (json) => sent.push(JSON.parse(json));
  const bridge = {
    setMultiplayerSetting(key, value) {
      if (window.CO_VoiceSettings.forwardSetting(key, value)) {
        return true;
      }
      logged.push(key);
    }
  };
  let backs = 0;
  const controller = window.CO_Settings.create({ mount, bridge, onBack: () => { backs += 1; }, defaultTab: tab });
  window.CO_VoiceSettings.attach(controller, bridge);
  controller.show();
  const rows = () => [].concat(...controller.getSettingsData().map((section) => section.items));
  return {
    window,
    controller,
    sent,
    logged,
    clock,
    backs: () => backs,
    row: (key) => rows().find((item) => item.key === key),
    el(key) { return registry.filter((item) => item.attributes["data-setting-key"] === key).pop(); },
    rendered(key) { return this.el(key).querySelector(".settings-row__value-text").textContent; },
    press: (key) => controller.handleKeydown({ key, preventDefault() {} }),
    goto(key) {
      this.press("Tab");
      const index = rows().findIndex((item) => item.key === key);
      for (let i = 0; i < index; i += 1) {
        this.press("ArrowDown");
      }
    }
  };
}

{
  const w = makeSettingsWorld("multiplayer");
  w.goto("voice.whisperKey");
  w.press("Enter");
  w.press("Enter");
  w.press("Escape");
  w.clock.now += 1000;
  w.controller.handleControllerInput("a", "down");
  w.controller.handleControllerInput("b", "down");
  w.controller.back();
  assert.equal(w.backs(), 0);
  assert.deepEqual(w.sent, [{ op: "beginKeyCapture", target: "voice.whisperKey" }]);
  assert.equal(w.row("voice.whisperKey").value, PROMPT);
  w.window.coVoiceEvent({ kind: "keyCapture", target: "voice.whisperKey", state: "cancelled", name: "", vk: 0 });
  w.press("Escape");
  assert.equal(w.backs(), 1);
}

{
  const w = makeSettingsWorld("multiplayer");
  assert.equal(w.rendered("voice.whisperKey"), "Unbound");
  assert.equal(w.rendered("voice.pushToTalkKey"), "B");
  w.goto("voice.whisperKey");
  w.press("Enter");
  assert.equal(w.rendered("voice.whisperKey"), PROMPT);
  w.window.coVoiceEvent({ kind: "keyCapture", target: "voice.whisperKey", state: "captured", name: "Left Shift", vk: 160 });
  assert.equal(w.rendered("voice.whisperKey"), "Left Shift");
  w.window.coVoiceEvent({ kind: "status", active: true, code: "transmitting", pending: false, micHot: false });
  assert.equal(w.rendered("voice.status"), "Transmitting");
  assert.deepEqual([w.el("voice.whisperKey"), w.el("voice.status")].map((item) => item.classList.names.has("is-readonly")), [false, true]);
  assert.match(w.el("voice.openMicSensitivity").innerHTML, /min="-80" max="0" step="5" value="-45"/);
}

{
  const w = makeSettingsWorld("multiplayer");
  w.goto("voice.whisperKey");
  w.press("Enter");
  w.controller.hide();
  assert.equal(w.row("voice.whisperKey").value, "");
  w.controller.show();
  w.clock.now += 1000;
  w.press("Escape");
  assert.equal(w.backs(), 1);
  assert.deepEqual(w.sent, [{ op: "beginKeyCapture", target: "voice.whisperKey" }]);
}

{
  const w = makeSettingsWorld("multiplayer");
  w.goto("voice.deafened");
  w.press("Enter");
  assert.equal(w.row("voice.deafened").value, false);
  assert.deepEqual(w.sent, [{ op: "setSetting", key: "voice.deafened", value: true }]);
  w.window.coVoiceEvent({ kind: "settings", values: { "voice.deafened": true } });
  assert.equal(w.row("voice.deafened").value, true);
}

{
  const w = makeSettingsWorld("multiplayer");
  w.goto("voice.voiceChatVolume");
  w.press("ArrowRight");
  assert.equal(w.row("voice.voiceChatVolume").value, 85);
  assert.deepEqual(w.sent, [{ op: "setSetting", key: "voice.voiceChatVolume", value: 90 }]);
}

{
  const w = makeSettingsWorld("multiplayer");
  w.goto("map.showPlayerMarkers");
  w.press("Enter");
  assert.equal(w.row("map.showPlayerMarkers").value, false);
  assert.deepEqual(w.logged, ["map.showPlayerMarkers"]);
  assert.deepEqual(w.sent, []);
}

{
  const early = [
    { kind: "status", active: true, code: "transmitting", pending: false, micHot: false },
    { kind: "settings", values: { "voice.deafened": true, "voice.micMuted": true } }
  ];
  const w = makeWorld({ attach: false, early });
  w.api.attach(w.controller, w.bridge);
  assert.equal(w.row("voice.status").value, "Transmitting");
  assert.equal(w.row("voice.deafened").value, true);
  assert.equal(w.row("voice.micMuted").value, true);
}

{
  const w = makeWorld({ attach: false });
  w.window.coVoiceEvent({ kind: "settings", values: { "voice.deafened": true } });
  w.api.attach(w.controller, w.bridge);
  assert.equal(w.row("voice.deafened").value, true);
}

{
  const w = makeWorld({ attach: false });
  for (let i = 0; i < 1000; i += 1) {
    w.window.coVoiceEvent({ kind: "level", dbfs: -30 });
  }
  for (let i = 0; i < 49; i += 1) {
    w.window.coVoiceEvent({ kind: "status", active: true, code: "ready", pending: false, micHot: false });
  }
  w.window.coVoiceEvent({ kind: "status", active: true, code: "transmitting", pending: false, micHot: false });
  w.window.coVoiceEvent({ kind: "settings", values: { "voice.micMuted": true } });
  w.window.coVoiceEvent({ kind: "settings", values: { "voice.deafened": true } });
  assert.ok(w.window.CO_VoiceEvents.length <= 2);
  w.api.attach(w.controller, w.bridge);
  assert.equal(w.row("voice.status").value, "Transmitting");
  assert.equal(w.row("voice.deafened").value, true);
}

{
  const w = makeWorld();
  w.api.handleEvent({ kind: "settings", values: { "voice.voiceChatVolume": 500, "voice.openMicSensitivity": 999 } });
  assert.equal(w.row("voice.voiceChatVolume").value, 85);
  assert.equal(w.row("voice.openMicSensitivity").value, -45);
  w.api.handleEvent({ kind: "settings", values: { "voice.voiceChatVolume": 0, "voice.openMicSensitivity": -80 } });
  assert.equal(w.row("voice.voiceChatVolume").value, 0);
  assert.equal(w.row("voice.openMicSensitivity").value, -80);
}

function anyEl() {
  return new Proxy(function () {}, { get: (t, k) => (k === Symbol.toPrimitive ? () => 0 : k === "length" ? 0 : k === "then" ? undefined : anyEl()), apply: anyEl });
}

function runMenu(page) {
  const attached = [], created = [], forwarded = [];
  const controller = anyEl();
  const window = { CO_Settings: { create: (options) => { created.push(options); return controller; } }, CO_Browser: { create: anyEl }, addEventListener() {},
    CO_PromptGlyphs: { hydrate() {} }, CO_QuitSaveStatus: { paint() {} }, CO_ServerPanel: { init() {}, setInfo() {}, syncMapVisibility() {} },
    CO_VoiceSettings: { forwardSetting: (key, value) => { forwarded.push([key, value]); return key.startsWith("voice.") && key !== "voice.status"; }, attach: (c, b) => attached.push([c, b]) } };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, "..", page, "script.js"), "utf8"), { window, document: anyEl(), getComputedStyle: anyEl, Image: anyEl, console: anyEl() }, { filename: page + "/script.js" });
  return { attached, controller, created, window, forwarded };
}

{
  ["main-menu", "pause-menu"].forEach((page) => {
    const html = fs.readFileSync(path.join(__dirname, "..", page, "index.html"), "utf8");
    const moduleTags = ["settings-model.js", "settings-native.js", "settings-render.js", "settings-input.js"].map((name) => html.indexOf(`<script src="../settings/${name}"></script>`));
    const settingsTag = html.indexOf('<script src="../settings/settings.js"></script>');
    assert.ok(moduleTags.every((tag, i) => tag >= 0 && tag < settingsTag && (i === 0 || tag > moduleTags[i - 1])), page);
    const voiceTag = html.indexOf('<script src="../settings/settings-voice.js"></script>');
    assert.ok(settingsTag >= 0 && settingsTag < voiceTag && voiceTag < html.indexOf('<script src="script.js"></script>'), page);
    const run = runMenu(page);
    const eager = page === "pause-menu";
    assert.deepEqual(run.attached.map((args) => [args[0] === run.controller, args[1] === run.window.CommonwealthOnlineUI]), eager ? [[true, true]] : [], page);
    assert.deepEqual(run.created.map((options) => options.bridge === run.window.CommonwealthOnlineUI), eager ? [true] : [], page);
    assert.deepEqual([run.window.CommonwealthOnlineUI.setMultiplayerSetting("voice.deafened", true), run.window.CommonwealthOnlineUI.setMultiplayerSetting("map.showPlayerMarkers", false), run.forwarded], [true, undefined, [["voice.deafened", true], ["map.showPlayerMarkers", false]]], page);
  });
}

console.log("VC-16 settings voice tests passed");
