const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const source = fs.readFileSync(__dirname + "/quests.js", "utf8");

function harness() {
  const listeners = { root: {}, document: {} };
  const sent = [];
  const timers = new Map();
  const focused = [];
  let timerId = 0;
  let html = "";
  let panel = null;
  let scrollers = null;
  let buttons = [];
  const buttonsIn = (markup) => (markup.match(/<button [^>]*>/g) || []).map((tag) => {
    const dataset = {};
    for (const [, name, value] of tag.matchAll(/data-([a-z-]+)="([^"]*)"/g)) {
      dataset[name.replace(/-([a-z])/g, (_, letter) => letter.toUpperCase())] = value;
    }
    return { dataset, disabled: /\sdisabled[\s>]/.test(tag), focus() { focused.push(this); } };
  });
  const root = {
    writes: 0,
    get innerHTML() {
      return html;
    },
    set innerHTML(value) {
      html = value;
      this.writes += 1;
      panel = value.includes('class="quest-panel"') ? { scrollTop: 0 } : null;
      scrollers = value.includes('class="quest-index"') ? { index: { scrollTop: 0 }, detail: { scrollTop: 0 } } : null;
      buttons = buttonsIn(value);
    },
    querySelector(selector) {
      if (selector === ".quest-index") return scrollers && scrollers.index;
      if (selector === ".quest-detail") return scrollers && scrollers.detail;
      return selector === ".quest-panel" ? panel : null;
    },
    querySelectorAll(selector) {
      return selector === "button[data-action]" ? buttons : [];
    },
    addEventListener(type, fn) {
      listeners.root[type] = fn;
    }
  };
  const document = {
    readyState: "complete",
    activeElement: null,
    documentElement: { style: { setProperty() {} } },
    querySelectorAll() {
      return [];
    },
    getElementById(id) {
      return id === "coQuests" ? root : null;
    },
    addEventListener(type, fn) {
      listeners.document[type] = fn;
    }
  };
  const window = {
    innerWidth: 1920,
    coQuestAction(payload) {
      sent.push(JSON.parse(payload));
    },
    setTimeout(fn) {
      timerId += 1;
      timers.set(timerId, fn);
      return timerId;
    },
    clearTimeout(id) {
      timers.delete(id);
    }
  };
  vm.runInNewContext(source, { document, window, JSON, String, Number, Object, Array, Math, Error, isFinite });

  return {
    root,
    document,
    sent,
    focused,
    panel: () => root.querySelector(".quest-index"),
    detail: () => root.querySelector(".quest-detail"),
    timerCount: () => timers.size,
    runTimers() {
      const callbacks = [...timers.values()];
      timers.clear();
      callbacks.forEach((fn) => fn());
    },
    model: window.coQuestModel,
    setState: window.coQuestSetState,
    controller: window.handleControllerInput,
    click(data) {
      listeners.root.click({ target: { closest: () => ({ dataset: data }) } });
    },
    keydown(key) {
      let prevented = false;
      listeners.document.keydown({ key, preventDefault() { prevented = true; } });
      return prevented;
    }
  };
}

const questKey = "1A2B3C:Fallout4.esm";
const quests = [
  {
    quest: questKey,
    name: "Out of Time",
    stage: 30,
    scope: "shared",
    friends: [
      { id: "3", name: "Ana", stage: 30 },
      { id: "7", name: "Codsworth", stage: 10 }
    ]
  },
  { quest: "000800:CommonwealthOnline.esp", name: "Settler Handshake", stage: 5, scope: "held", friends: [] }
];
const notices = [
  { text: "Ana completed Out of Time", time: "12:04" },
  { text: "Held: waiting for you to start Settler Handshake", time: "12:05" }
];
const coopState = { enabled: true, mode: "coop-story", quests, notices };
const freeRoamState = { enabled: true, mode: "free-roam", quests: [], notices: [] };

function plain(value) {
  return JSON.parse(JSON.stringify(value));
}

{
  const h = harness();
  assert.match(h.model.renderState(null), /Loading quest status/);
  h.setState({ enabled: false });
  assert.match(h.root.innerHTML, /Quests are not available on this server/);
  assert.doesNotMatch(h.root.innerHTML, /Complete the intro to unlock quests/);
  assert.match(h.root.innerHTML, /data-action="close"/);
  assert.doesNotMatch(h.root.innerHTML, /data-action="setScope"/);
}

{
  const h = harness();
  h.setState({ enabled: false, tutorial: false });
  assert.match(h.root.innerHTML, /Quests are not available on this server/);
  assert.doesNotMatch(h.root.innerHTML, /Complete the intro to unlock quests/);
  assert.doesNotMatch(h.root.innerHTML, /data-action="setScope"/);
  assert.doesNotMatch(h.root.innerHTML, /data-action="refresh"/);
}

{
  const h = harness();
  h.setState({ enabled: false, tutorial: true });
  assert.match(h.root.innerHTML, /Complete the intro to unlock quests/);
  assert.match(h.root.innerHTML, /data-action="close"/);
  assert.doesNotMatch(h.root.innerHTML, /data-action="setScope"/);
  assert.doesNotMatch(h.root.innerHTML, /data-action="refresh"/);
}

{
  const h = harness();
  h.setState(freeRoamState);
  assert.match(h.root.innerHTML, /Quests are disabled on this server/);
  assert.match(h.root.innerHTML, /FREE ROAM/);
  assert.doesNotMatch(h.root.innerHTML, /data-action="joinFriend"/);
  assert.doesNotMatch(h.root.innerHTML, /SHARED QUESTS/);
  assert.match(h.root.innerHTML, /data-action="close"/);
}

{
  const h = harness();
  h.setState(JSON.stringify(coopState));
  assert.match(h.root.innerHTML, /CO-OP STORY/);
  assert.match(h.root.innerHTML, /Out of Time/);
  assert.match(h.root.innerHTML, /1A2B3C:Fallout4.esm/);
  assert.match(h.root.innerHTML, /STAGE 30/);
  assert.match(h.root.innerHTML, /quest-badge is-shared">SHARED/);
  assert.match(h.root.innerHTML, /Ana/);
  assert.match(h.root.innerHTML, /STAGE 10/);
  assert.match(h.root.innerHTML, /Ana completed Out of Time/);
  assert.match(h.root.innerHTML, /Held: waiting for you to start Settler Handshake/);
  assert.match(h.root.innerHTML, /data-action="select" data-quest="000800:CommonwealthOnline\.esp"/, "every quest is listed by name");
  h.click({ action: "select", quest: "000800:CommonwealthOnline.esp" });
  assert.match(h.root.innerHTML, /quest-badge is-held">HELD/, "selecting a quest shows its detail");
  assert.deepEqual(plain(h.sent), [], "selecting is local and sends nothing");
}

{
  const h = harness();
  h.setState({ enabled: true, mode: "faction-war", quests: [{ quest: questKey, name: "Q", stage: 1, scope: "solo", friends: [] }], notices: [] });
  assert.match(h.root.innerHTML, /FACTION WAR/);
  assert.match(h.root.innerHTML, /quest-badge is-solo">SOLO/);
  assert.match(h.root.innerHTML, /No friends on this quest yet/);
}

{
  const h = harness();
  h.setState(coopState);
  h.click({ action: "setScope", quest: questKey, scope: "shared" });
  assert.deepEqual(plain(h.sent), [{ action: "setScope", quest: questKey, scope: "shared" }]);

  h.runTimers();
  h.click({ action: "setScope", quest: questKey, scope: "solo" });
  assert.deepEqual(plain(h.sent[1]), { action: "setScope", quest: questKey, scope: "solo" });

  h.runTimers();
  h.click({ action: "refresh" });
  h.click({ action: "close" });
  assert.deepEqual(plain(h.sent[2]), { action: "refresh" });
  assert.deepEqual(plain(h.sent[3]), { action: "close" });
  assert.equal(h.sent.length, 4);
}

{
  const h = harness();
  h.setState(coopState);
  h.click({ action: "setScope", quest: questKey, scope: "held" });
  h.click({ action: "joinFriend", quest: questKey, playerId: "3" });
  h.click({ action: "kick", quest: questKey });
  h.click({ action: "setScope" });
  assert.deepEqual(plain(h.sent), [], "invalid actions must not be sent");
  assert.equal(h.model.actionFor(null), null);
  assert.equal(h.model.actionFor({ action: "setScope", quest: questKey, scope: "held" }), null);
  assert.equal(h.model.actionFor({ action: "joinFriend", quest: questKey, playerId: "3" }), null, "joinFriend is not an action");
  assert.deepEqual(plain(h.model.actionFor({ action: "setScope", quest: questKey, scope: "SOLO" })),
    { action: "setScope", quest: questKey, scope: "solo" });
}

{
  const h = harness();
  h.setState(coopState);
  assert.equal(h.keydown("Escape"), true, "Escape is handled");
  assert.deepEqual(plain(h.sent), [{ action: "close" }]);

  h.controller("b", "down");
  assert.deepEqual(plain(h.sent[1]), { action: "close" });
  h.controller("b", "up");
  assert.equal(h.sent.length, 2, "button release must not resend");

  h.controller("dpad_down", "down");
  h.controller("dpad_up", "down");
  h.controller("y", "down");
  assert.equal(h.sent.length, 2, "navigation must not send actions");
}

{
  const h = harness();
  h.setState(coopState);
  let clicked = 0;
  h.document.activeElement = { tagName: "BUTTON", click() { clicked += 1; } };
  h.controller("a", "down");
  assert.equal(clicked, 1, "A activates the focused button");
  h.keydown("Enter");
  assert.equal(clicked, 2, "Enter activates the focused button");
}

{
  const h = harness();
  h.setState(coopState);
  const writes = h.root.writes;
  assert.equal(h.setState(JSON.stringify(coopState)), true);
  assert.equal(h.root.writes, writes, "identical state must not repaint");
  assert.equal(h.setState("{not json"), false);
  assert.equal(h.setState([1, 2]), true);
  assert.match(h.root.innerHTML, /Quests are not available on this server/);
}

{
  const h = harness();
  h.setState({ enabled: true, mode: "coop-story", inFaction: false, quests: [{ quest: questKey, name: "Q", stage: 1, scope: "shared", friends: [] }], notices: [] });
  assert.match(h.root.innerHTML, /data-scope="party" aria-pressed="false" disabled>Party</);
  assert.match(h.root.innerHTML, /Join a faction to share a quest with a party/);
  assert.match(h.root.innerHTML, /data-scope="solo" aria-pressed="false">Go solo</);

  h.setState({ enabled: true, mode: "coop-story", inFaction: true, quests: [{ quest: questKey, name: "Q", stage: 1, scope: "shared", friends: [] }], notices: [] });
  assert.match(h.root.innerHTML, /data-scope="party" aria-pressed="false">Party</);
  assert.doesNotMatch(h.root.innerHTML, /Join a faction/);
  h.click({ action: "setScope", quest: questKey, scope: "party" });
  assert.deepEqual(plain(h.sent), [{ action: "setScope", quest: questKey, scope: "party" }]);

  h.setState({ enabled: true, mode: "coop-story", inFaction: false, quests: [{ quest: questKey, name: "Q", stage: 1, scope: "party", friends: [] }], notices: [] });
  assert.match(h.root.innerHTML, /quest-badge is-party">PARTY/);
  assert.match(h.root.innerHTML, /is-pressed" data-action="setScope" data-quest="1A2B3C:Fallout4\.esm" data-scope="party" aria-pressed="true">Party</);
  assert.equal(h.model.scopeOf({ scope: "PARTY" }), "party");
}

{
  const h = harness();
  assert.equal(h.model.stageText(0), "—");
  assert.equal(h.model.stageText(""), "—");
  assert.equal(h.model.stageText(12), "STAGE 12");
  assert.equal(h.model.scopeOf({}), "shared");
  assert.equal(h.model.scopeOf({ scope: "HELD" }), "held");
  assert.equal(h.model.scopeOf({ scope: "nonsense" }), "shared");
  assert.equal(h.model.modeLabel("coop-story"), "CO-OP STORY");
  assert.equal(h.model.modeLabel(""), "UNKNOWN MODE");
}

{
  const h = harness();
  h.setState({ enabled: true, mode: "coop-story", quests: [{ quest: questKey, name: "<img src=x onerror=1>", stage: 2, scope: "shared", friends: [{ playerId: "3", name: "\"><b>", stage: 2 }] }], notices: [] });
  assert.doesNotMatch(h.root.innerHTML, /<img src=x/);
  assert.match(h.root.innerHTML, /&lt;img src=x onerror=1&gt;/);
  assert.match(h.root.innerHTML, /&quot;&gt;&lt;b&gt;/);
  assert.doesNotMatch(h.root.innerHTML, /<b>/);
}

{
  const h = harness();
  h.setState(coopState);
  const friendRows = h.root.innerHTML.match(/<li class="quest-friend">[\s\S]*?<\/li>/g) || [];
  assert.equal(friendRows.length, 2, "both friends render");
  for (const row of friendRows) {
    assert.doesNotMatch(row, /<button/, "a friend row is read-only");
  }
  assert.match(h.root.innerHTML, /quest-friend__name">Ana</);
  assert.doesNotMatch(h.root.innerHTML, /joinFriend|data-player-id/);
  assert.match(h.root.innerHTML, /<h2>QUESTS<\/h2>/);

  h.setState({ enabled: true, mode: "coop-story", quests: [{ quest: questKey, name: "Q", stage: 1, scope: "shared", friends: [{ id: "7", stage: 10 }] }], notices: [] });
  assert.match(h.root.innerHTML, /quest-friend__name">Player 7</);
}

{
  const h = harness();
  h.setState(coopState);
  assert.match(h.root.innerHTML, /is-pressed" data-action="setScope" data-quest="1A2B3C:Fallout4\.esm" data-scope="shared" aria-pressed="true">Share</);
  assert.match(h.root.innerHTML, /data-scope="solo" aria-pressed="false">Go solo</);
  h.click({ action: "select", quest: "000800:CommonwealthOnline.esp" });
  assert.match(h.root.innerHTML, /data-quest="000800:CommonwealthOnline\.esp" data-scope="shared" aria-pressed="false" disabled>Share</);
  assert.match(h.root.innerHTML, /data-quest="000800:CommonwealthOnline\.esp" data-scope="solo" aria-pressed="false" disabled>Go solo</);
  h.click({ action: "select", quest: questKey });

  h.click({ action: "setScope", quest: questKey, scope: "solo" });
  assert.match(h.root.innerHTML, /data-scope="solo" aria-pressed="false" aria-disabled="true">Updating\.\.\.</);
  assert.match(h.root.innerHTML, /data-scope="shared" aria-pressed="true" aria-disabled="true">Share</);
  assert.equal(h.timerCount(), 1, "a pending row arms one timeout");
  const sentBefore = h.sent.length;
  h.click({ action: "setScope", quest: questKey, scope: "shared" });
  assert.equal(h.sent.length, sentBefore, "a pending row ignores further scope clicks");
  h.runTimers();
  assert.doesNotMatch(h.root.innerHTML, /Updating|aria-disabled/, "the pending row clears after the timeout");

  h.click({ action: "setScope", quest: questKey, scope: "solo" });
  const soloState = { ...coopState, quests: [{ ...quests[0], scope: "solo" }, quests[1]] };
  h.setState(soloState);
  assert.doesNotMatch(h.root.innerHTML, /Updating/, "the next state push clears the pending row");
  assert.match(h.root.innerHTML, /data-scope="solo" aria-pressed="true">Go solo</);
  assert.equal(h.timerCount(), 0, "the state push cancels the timeout");
}

{
  const h = harness();
  h.setState(coopState);
  h.panel().scrollTop = 140;
  h.detail().scrollTop = 60;
  const solo = h.root.querySelectorAll("button[data-action]").find((button) => button.dataset.scope === "solo" && button.dataset.quest === questKey);
  h.document.activeElement = solo;
  h.setState({ ...coopState, notices: [{ text: "Ana joined", time: "12:06" }] });
  assert.equal(h.panel().scrollTop, 140, "a state push keeps the index scroll position");
  assert.equal(h.detail().scrollTop, 60, "a state push keeps the detail scroll position");
  const restored = h.focused[h.focused.length - 1];
  assert.equal(restored.dataset.quest, questKey, "a state push keeps focus on the same quest");
  assert.equal(restored.dataset.scope, "solo", "a state push keeps focus on the same control");

  const share = h.root.querySelectorAll("button[data-action]").find((button) => button.dataset.scope === "shared" && button.dataset.quest === questKey);
  h.document.activeElement = share;
  h.click({ action: "setScope", quest: questKey, scope: "shared" });
  assert.equal(h.focused[h.focused.length - 1].dataset.scope, "shared", "the clicked control keeps focus while pending");
}

{
  const h = harness();
  h.setState(coopState);
  assert.match(h.root.innerHTML, /<ul class="quest-notices" aria-live="polite">/);
  const indexHtml = fs.readFileSync(path.join(__dirname, "index.html"), "utf8");
  assert.doesNotMatch(indexHtml, /<main[^>]*aria-live/, "the whole panel must not be a live region");
}

{
  const h = harness();
  const heldKey = "01CC2A:fallout4.esm";
  const nextKey = "0229E5:fallout4.esm";
  const unreachedKey = "0229EA:fallout4.esm";
  h.setState({
    enabled: true,
    mode: "coop-story",
    inFaction: true,
    quests: [
      { quest: heldKey, name: "Out of Time", stage: 10, scope: "shared", friends: [] },
      { quest: nextKey, name: "Jewel of the Commonwealth", stage: 0, scope: "shared", friends: [], nextStep: true }
    ],
    notices: []
  });
  const html = h.root.innerHTML;
  assert.match(html, /Out of Time/);
  assert.equal((html.match(/data-action="setScope"/g) || []).length, 3, "only the selected quest renders scope controls");
  h.click({ action: "select", quest: nextKey });
  assert.doesNotMatch(h.root.innerHTML, /data-action="setScope"/, "the next-step hint must carry no start/advance control");
  assert.doesNotMatch(h.root.innerHTML, /data-action="joinFriend"/);
  assert.match(h.root.innerHTML, /quest-badge is-next">NEXT/);
  assert.match(h.root.innerHTML, /Finish the previous story quest to unlock this step/);
  assert.doesNotMatch(html, /0229EA:fallout4\.esm/, "an unreached quest is never listed");
  void unreachedKey;
}

const pauseMenuRoot = path.join(__dirname, "..", "pause-menu");
const sharedRoot = path.join(__dirname, "..", "shared");

function makeElement(overrides) {
  const target = Object.assign({
    hidden: false,
    textContent: "",
    innerHTML: "",
    tabIndex: -1,
    style: { display: "", setProperty() {} },
    dataset: {},
    classList: { add() {}, remove() {}, contains() { return false; }, toggle() {} },
    addEventListener() {},
    removeEventListener() {},
    setAttribute() {},
    getAttribute() { return null; },
    removeAttribute() {},
    appendChild(child) { return child; },
    insertBefore(child) { return child; },
    removeChild(child) { return child; },
    remove() {},
    focus() {},
    blur() {},
    click() {},
    contains() { return false; },
    querySelector() { return null; },
    querySelectorAll() { return []; },
    getBoundingClientRect() { return { left: 0, top: 0, width: 0, height: 0 }; }
  }, overrides || {});

  return new Proxy(target, {
    get(object, key) {
      if (key in object) {
        return object[key];
      }
      if (typeof key === "symbol") {
        return undefined;
      }
      return function () {};
    },
    set(object, key, value) {
      object[key] = value;
      return true;
    }
  });
}

function makeMenuItem(action, hidden) {
  const item = makeElement({ hidden: !!hidden, selected: false });
  item.action = action;
  item.getAttribute = (name) => (name === "data-action" ? action : null);
  item.classList = {
    add() {},
    remove() {},
    contains() { return false; },
    toggle(name, on) {
      if (name === "is-selected") {
        item.selected = !!on;
      }
    }
  };
  return item;
}

function pauseMenuHarness() {
  const menuItems = [
    makeMenuItem("resume"),
    makeMenuItem("map"),
    makeMenuItem("social"),
    makeMenuItem("quests"),
    makeMenuItem("server"),
    makeMenuItem("settings"),
    makeMenuItem("help"),
    makeMenuItem("quit")
  ];
  const questsItem = menuItems[3];
  const menuListeners = {};
  const menu = makeElement({
    querySelectorAll(selector) {
      return selector === ".menu-item" ? menuItems.slice() : [];
    },
    querySelector(selector) {
      return selector === '.menu-item[data-action="quests"]' ? questsItem : null;
    },
    addEventListener(type, fn) {
      menuListeners[type] = fn;
    }
  });

  const elements = { menu };
  const signalClasses = new Set();
  const panelClasses = new Set();
  const recordingClassList = (store) => ({
    add(name) { store.add(name); },
    remove(...names) { names.forEach((name) => store.delete(name)); },
    contains(name) { return store.has(name); },
    toggle(name, on) { if (on) { store.add(name); } else { store.delete(name); } }
  });
  elements.serverSignalBars = makeElement({ classList: recordingClassList(signalClasses) });
  elements.serverPanel = makeElement({ classList: recordingClassList(panelClasses) });
  const document = {
    readyState: "complete",
    documentElement: makeElement(),
    activeElement: null,
    getElementById(id) {
      if (!elements[id]) {
        elements[id] = makeElement();
      }
      return elements[id];
    },
    querySelector(selector) {
      return selector === ".left-panel" ? makeElement() : null;
    },
    querySelectorAll() {
      return [];
    },
    addEventListener() {},
    createElement() {
      return makeElement();
    }
  };

  const actions = [];
  const settingsController = {
    shown: 0,
    hidden: 0,
    show() { this.shown += 1; },
    hide() { this.hidden += 1; }
  };
  const window = {
    innerWidth: 1920,
    innerHeight: 1080,
    addEventListener() {},
    setTimeout: () => 0,
    clearTimeout: () => {},
    coAction(payload) {
      actions.push(JSON.parse(payload));
    },
    CO_PromptGlyphs: { hydrate() {} },
    CO_QuitSaveStatus: { paint() {} },
    CO_ServerPanel: { init() {}, setInfo() {}, syncMapVisibility() {} },
    CO_VoiceSettings: { attach() {}, forwardSetting: () => false },
    CO_Settings: { create: () => settingsController }
  };

  const context = {
    window,
    document,
    console: { log() {}, warn() {}, error() {} },
    JSON,
    String,
    Number,
    Object,
    Array,
    Math,
    Error,
    isFinite,
    parseInt,
    getComputedStyle: () => ({ getPropertyValue: () => "#c9a84a" }),
    Image: function () { return makeElement(); },
    requestAnimationFrame: () => 0,
    setTimeout: () => 0,
    clearTimeout: () => {}
  };
  vm.createContext(context);
  for (const file of ["prompt-glyphs.js", "server-panel.js", "script.js"]) {
    const full = file === "prompt-glyphs.js" ? path.join(sharedRoot, file) : path.join(pauseMenuRoot, file);
    vm.runInContext(fs.readFileSync(full, "utf8"), context, { filename: full });
  }

  const selectedAction = () => {
    const item = menuItems.find((entry) => entry.selected);
    return item ? item.action : null;
  };

  return {
    actions,
    settingsController,
    questsItem,
    menuItems,
    signalClasses,
    panelClasses,
    window,
    element(id) {
      return document.getElementById(id);
    },
    selectedAction,
    itemByAction(action) {
      return menuItems.find((entry) => entry.action === action);
    },
    showMenu() {
      window.onPauseMenuShown();
    },
    press(button) {
      window.handleControllerInput(button, "down");
    },
    cycle(count) {
      const seen = [];
      for (let index = 0; index < count; index += 1) {
        window.handleControllerInput("dpad_down", "down");
        seen.push(selectedAction());
      }
      return seen;
    },
    click(item) {
      menuListeners.click({ target: { closest: () => item } });
    }
  };
}

{
  const h = pauseMenuHarness();
  assert.equal(h.questsItem.hidden, false, "QUESTS is part of the menu before any plugin registration");
  h.showMenu();
  assert.deepEqual(h.cycle(8), ["resume", "map", "social", "quests", "server", "settings", "help", "quit"]);

  const beforeOpen = h.actions.length;
  h.click(h.questsItem);
  assert.deepEqual(plain(h.actions[beforeOpen]), { action: "openView", view: "quests" });
}

{
  const h = pauseMenuHarness();
  h.showMenu();
  h.cycle(8);
  assert.equal(h.selectedAction(), "quit");
  h.press("a");
  assert.equal(h.selectedAction(), "quit", "QUIT_INDEX must track the live menu list");

  h.showMenu();
  h.cycle(6);
  assert.equal(h.selectedAction(), "settings");
  h.press("a");
  assert.equal(h.settingsController.shown, 1, "activating SETTINGS opens the settings panel");
  h.click(h.itemByAction("settings"));
  assert.equal(h.settingsController.hidden, 0, "clicking the selected SETTINGS item must not exit settings mode");
  h.click(h.itemByAction("help"));
  assert.equal(h.settingsController.hidden, 1, "clicking another item exits settings mode");
}

{
  const h = pauseMenuHarness();
  h.window.setCurrentServerInfo({ connected: true, name: "Settlers", players: 3, maxPlayers: 16, ping: 42 });
  assert.equal(h.element("serverName").textContent, "Settlers");
  assert.equal(h.element("serverPlayers").textContent, "3 / 16");
  assert.equal(h.element("serverPing").textContent, "42 ms");
  assert.deepEqual([...h.signalClasses], ["signal-bars--good"]);
  assert.equal(h.panelClasses.has("is-offline"), false);

  h.window.setCurrentServerInfo({ connected: false });
  assert.equal(h.element("serverName").textContent, "—");
  assert.equal(h.element("serverPlayers").textContent, "—");
  assert.deepEqual([...h.signalClasses], ["signal-bars--unknown"]);
  assert.equal(h.panelClasses.has("is-offline"), true);

  h.window.setCurrentServerInfo({ connected: true, host: "fallout.local", players: 0, ping: 0 });
  assert.equal(h.element("serverName").textContent, "fallout.local");
  assert.equal(h.element("serverPlayers").textContent, "1");
  assert.equal(h.element("serverPing").textContent, "—");
  assert.deepEqual([...h.signalClasses], ["signal-bars--unknown"]);
}

{
  const rendered = new Set(
    (harness().model.renderState(coopState).match(/data-action="([a-zA-Z]+)"/g) || [])
      .map((match) => match.slice('data-action="'.length, -1))
  );
  assert.ok(rendered.size > 0, "the quest window must render action buttons");
  for (const action of rendered) {
    assert.ok(
      ["select", "setScope", "refresh", "close"].includes(action),
      `rendered action ${action} has no window-to-plugin producer`
    );
  }
}

{
  const rendered = harness().model.renderState(coopState);
  assert.match(
    rendered,
    /<header class="quest-head">[\s\S]*data-action="close"/,
    "the quest header must carry a Close control, so the window can be left without scrolling the quest list"
  );
  const styles = fs.readFileSync(path.join(__dirname, "styles.css"), "utf8");
  assert.match(
    styles,
    /\.quest-head\s*\{[^}]*position:\s*sticky/,
    "the quest header must stay visible while the quest list scrolls"
  );
}

{
  const questView = fs.readFileSync(path.join(__dirname, "..", "..", "..", "..", "src", "F4TQuestView.cpp"), "utf8");
  assert.doesNotMatch(questView, /setQuestMenuAvailable/, "the window reports availability instead of toggling the row");
  assert.match(
    questView,
    /SetViewOwnsEscape\(handle, g_viewState\.IsDesiredVisible\(\) && g_viewState\.IsCurrentlyFocused\(\)\)/,
    "Escape must be routed to the focused quest window, so Escape closes the window instead of being swallowed"
  );
  const pauseHtml = fs.readFileSync(path.join(pauseMenuRoot, "index.html"), "utf8");
  assert.match(pauseHtml, /data-action="quests">/);
  assert.doesNotMatch(pauseHtml, /data-action="quests"[^>]*\shidden/);
  const pauseScript = fs.readFileSync(path.join(pauseMenuRoot, "script.js"), "utf8");
  assert.doesNotMatch(pauseScript, /setQuestMenuAvailable/);
}

console.log("quests window regression: all checks passed");
