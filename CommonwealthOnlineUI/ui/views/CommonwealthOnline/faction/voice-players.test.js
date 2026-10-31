const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const membersSource = fs.readFileSync(__dirname + "/faction.members.js", "utf8");
const focusSource = fs.readFileSync(__dirname + "/faction.focus.js", "utf8");
const factionSource = fs.readFileSync(__dirname + "/faction.js", "utf8");
const playersSource = fs.readFileSync(__dirname + "/voice-players.js", "utf8");
const markup = fs.readFileSync(__dirname + "/index.html", "utf8");

const plain = (value) => JSON.parse(JSON.stringify(value));

function descendants(node) {
  return node.children.reduce((all, child) => all.concat([child], descendants(child)), []);
}

function fakeElement(id, tagName) {
  return {
    id,
    tagName: tagName.toUpperCase(),
    className: "",
    placeholder: "",
    value: "",
    disabled: false,
    hidden: false,
    scrollTop: 0,
    scrollHeight: 0,
    attrs: {},
    dataset: {},
    listeners: {},
    children: [],
    classList: { toggle() {} },
    setAttribute(name, value) {
      this.attrs[name] = String(value);
    },
    addEventListener(type, fn) {
      this.listeners[type] = (this.listeners[type] || []).concat([fn]);
    },
    appendChild(child) {
      this.children.push(child);
      return child;
    },
    focus() {
      document.activeElement = this;
    },
    click() {
      (this.listeners.click || []).forEach((fn) => fn({ type: "click" }));
    },
    querySelectorAll() {
      return descendants(this).filter((node) => node.tagName === "BUTTON");
    },
    querySelector(selector) {
      const wanted = /"([^"]*)"/.exec(selector)[1];
      return descendants(this).find((node) => node.dataset.voiceKey === wanted) || null;
    },
    set textContent(value) {
      this.text = String(value);
      if (value === "") {
        if (descendants(this).includes(document.activeElement)) {
          document.activeElement = body;
        }
        this.children = [];
      }
    },
    get textContent() {
      return this.text === undefined ? "" : this.text;
    }
  };
}

const pageIds = {
  coTabFaction: "button",
  coTabVoice: "button",
  coFaction: "main",
  coVoice: "section",
  coVoiceStatus: "p",
  coVoiceList: "ol"
};
const byId = {};
for (const id of Object.keys(pageIds)) {
  byId[id] = fakeElement(id, pageIds[id]);
}
byId.coVoice.appendChild(byId.coVoiceStatus);
byId.coVoice.appendChild(byId.coVoiceList);
byId.coVoice.hidden = true;

const body = { tagName: "BODY", dataset: {}, children: [] };
const document = {
  readyState: "complete",
  activeElement: null,
  documentElement: { style: { setProperty() {} } },
  listeners: {},
  createElement(tagName) {
    return fakeElement("", tagName);
  },
  getElementById(id) {
    return byId[id] || null;
  },
  addEventListener(type, fn) {
    this.listeners[type] = fn;
  }
};

const sent = [];
const warnings = [];
const window = {
  innerWidth: 1920,
  addEventListener() {},
  coVoiceAction(json) {
    sent.push(json);
  }
};
const context = vm.createContext({
  document,
  window,
  console: { warn(message) { warnings.push(message); } },
  Array,
  Math,
  Number,
  Object,
  String,
  JSON
});
vm.runInContext(membersSource, context);
vm.runInContext(focusSource, context);
vm.runInContext(factionSource, context);
vm.runInContext(playersSource, context);

const model = window.coVoicePlayers;
const key = (name) => ({ key: name, preventDefault() {} });

function rowItems() {
  return byId.coVoiceList.children;
}

function rowName(item) {
  return item.children.find((child) => child.className === "faction-row__name").textContent;
}

function rowButtons(item) {
  return item.children.filter((child) => child.tagName === "BUTTON");
}

function speakingText(item) {
  return item.children.find((child) => child.className === "voice-speaking").textContent;
}

function statusText() {
  return byId.coVoiceStatus.textContent;
}

function emit(event) {
  window.coVoiceEvent(event);
}

const twoPlayers = [
  { playerId: 3, name: "Nate", muted: false, volume: 50 },
  { playerId: 7, name: "Codsworth", muted: true, volume: 100 }
];

{
  assert.equal(JSON.stringify(model.muteAction({ playerId: 3, muted: false })), '{"op":"mutePlayer","playerId":3,"muted":true}', "mute sends the inverse of the current state");
  assert.equal(JSON.stringify(model.muteAction({ playerId: 7, muted: true })), '{"op":"mutePlayer","playerId":7,"muted":false}');
  assert.equal(JSON.stringify(model.volumeAction({ playerId: 3, volume: 50 }, 10)), '{"op":"setPlayerVolume","playerId":3,"volume":60}');
  assert.equal(JSON.stringify(model.volumeAction({ playerId: 3, volume: 50 }, -10)), '{"op":"setPlayerVolume","playerId":3,"volume":40}');
  assert.equal(JSON.stringify(model.volumeAction({ playerId: 3, volume: 95 }, 10)), '{"op":"setPlayerVolume","playerId":3,"volume":100}', "a step past 100 is clamped to 100");
  assert.equal(JSON.stringify(model.volumeAction({ playerId: 3, volume: 5 }, -10)), '{"op":"setPlayerVolume","playerId":3,"volume":0}', "a step below 0 is clamped to 0");
  assert.equal(model.volumeAction({ playerId: 3, volume: 100 }, 10), null, "no volume send at 100");
  assert.equal(model.volumeAction({ playerId: 3, volume: 0 }, -10), null, "no volume send at 0");
}

{
  const players = model.createPlayers();
  assert.deepEqual(plain(players.view()), { active: false, rows: [] }, "nothing is shown before a status event");
  players.apply({ kind: "status", active: true });
  players.apply({ kind: "players", players: twoPlayers });
  players.apply({ kind: "speakers", playerIds: [7] });
  assert.deepEqual(plain(players.view().rows), [
    { playerId: 3, name: "Nate", muted: false, volume: 50, speaking: false },
    { playerId: 7, name: "Codsworth", muted: true, volume: 100, speaking: true }
  ], "rows follow the players event and mark the speaker");
  players.apply({ kind: "status", active: false });
  assert.deepEqual(plain(players.view()), { active: false, rows: [] }, "an inactive status shows no rows");
  players.apply({ kind: "status", active: true });
  assert.deepEqual(plain(players.view().rows), [], "rows are not replayed after an inactive status");
  players.apply({ kind: "status", active: false });
  players.apply({ kind: "players", players: twoPlayers });
  players.apply({ kind: "speakers", playerIds: [7] });
  players.apply({ kind: "status", active: true });
  assert.deepEqual(plain(players.view().rows.map((row) => row.name)), ["Nate", "Codsworth"], "players received while inactive are kept until an inactive status clears them");
  players.apply({ kind: "players", players: twoPlayers });
  assert.deepEqual(plain(players.view().rows.map((row) => row.speaking)), [false, false], "speakers received while inactive are not kept");
  players.apply({ kind: "status", active: true });
  players.apply({ kind: "players", players: [null, { playerId: "1", name: "x", muted: false, volume: 50 }, { playerId: 2, name: "x", volume: 101 }, { playerId: -1, name: "Neg", volume: 10 }, { playerId: 1.5, name: "Frac", volume: 10 }, { playerId: 4, name: "Ok", volume: 30 }, { playerId: 5, name: 9, volume: 20 }] });
  assert.deepEqual(plain(players.view().rows.map((row) => row.name)), ["Ok", "Player 5"], "malformed entries are skipped and a non-string name falls back");
  players.apply({ kind: "status", active: null });
  assert.equal(players.view().active, true, "a status whose active is not a boolean is ignored");
  for (const event of [null, "players", 4, [], { kind: "speakers", playerIds: "3" }, { kind: "players", players: null }, { kind: "settings", values: {} }, { kind: "level", dbfs: -20 }]) {
    assert.doesNotThrow(() => players.apply(event));
  }
  assert.deepEqual(plain(players.view().rows.map((row) => row.name)), ["Ok", "Player 5"], "malformed and ignored events change nothing");
}

emit({ kind: "status", active: true, micHot: false, pending: false, code: "ready" });
emit({ kind: "players", players: twoPlayers });
assert.equal(rowItems().length, 2, "one row per remote player");
assert.equal(rowName(rowItems()[0]), "Nate");
assert.equal(byId.coVoiceStatus.textContent, "", "no status text while rows exist");
for (const event of [null, undefined]) {
  assert.doesNotThrow(() => emit(event), "a null or undefined event is ignored");
}
assert.equal(rowItems().length, 2, "a null or undefined event leaves the rows alone");

{
  emit({ kind: "players", players: [{ playerId: 3, name: '<script>alert("x")</script>', muted: false, volume: 50 }, { playerId: 7, name: `Ni"ck'<b>`, muted: true, volume: 100 }] });
  assert.equal(rowName(rowItems()[0]), '<script>alert("x")</script>', "a name with a script tag is set as text");
  assert.equal(rowName(rowItems()[1]), `Ni"ck'<b>`, "quotes and markup in a name are set as text");
}

{
  emit({ kind: "players", players: twoPlayers });
  const before = sent.length;
  rowButtons(rowItems()[0])[0].click();
  assert.equal(sent[sent.length - 1], '{"op":"mutePlayer","playerId":3,"muted":true}', "Mute sends the exact mutePlayer JSON");
  assert.equal(rowButtons(rowItems()[0])[0].textContent, "Mute", "no optimistic label change before the players event");
  emit({ kind: "players", players: [{ playerId: 3, name: "Nate", muted: true, volume: 50 }, twoPlayers[1]] });
  assert.equal(rowButtons(rowItems()[0])[0].textContent, "Unmute", "the label follows the next players event");
  rowButtons(rowItems()[0])[0].click();
  assert.equal(sent[sent.length - 1], '{"op":"mutePlayer","playerId":3,"muted":false}', "Unmute sends muted false");
  assert.equal(sent.length, before + 2);
}

{
  const bridge = window.coVoiceAction;
  delete window.coVoiceAction;
  const sentBefore = sent.length;
  const warnBefore = warnings.length;
  emit({ kind: "players", players: twoPlayers });
  rowButtons(rowItems()[0])[0].click();
  window.coVoiceAction = bridge;
  assert.equal(sent.length, sentBefore, "no action is sent without the bridge");
  assert.equal(warnings.length, warnBefore + 1, "a dropped action is reported once");
  assert.equal(warnings[warnings.length - 1], "coVoiceAction is not available; the voice action was dropped");
}

{
  emit({ kind: "players", players: twoPlayers });
  const minus = rowButtons(rowItems()[0])[1];
  const plus = rowButtons(rowItems()[0])[2];
  assert.equal(minus.textContent, "-");
  assert.equal(plus.textContent, "+");
  assert.equal(rowItems()[0].children.find((child) => child.className === "voice-volume").textContent, "50", "the volume number is shown");
  plus.click();
  assert.equal(sent[sent.length - 1], '{"op":"setPlayerVolume","playerId":3,"volume":60}', "+ steps the volume by ten");
  minus.click();
  assert.equal(sent[sent.length - 1], '{"op":"setPlayerVolume","playerId":3,"volume":40}', "- steps the volume down by ten");
  assert.equal(rowItems()[0].children.find((child) => child.className === "voice-volume").textContent, "50", "no optimistic volume change before the players event");

  const count = sent.length;
  emit({ kind: "players", players: [{ playerId: 3, name: "Nate", muted: false, volume: 100 }] });
  rowButtons(rowItems()[0])[2].click();
  assert.equal(sent.length, count, "+ at 100 sends nothing");

  emit({ kind: "players", players: [{ playerId: 3, name: "Nate", muted: false, volume: 95 }] });
  rowButtons(rowItems()[0])[2].click();
  assert.equal(sent[sent.length - 1], '{"op":"setPlayerVolume","playerId":3,"volume":100}', "a step from 95 clamps to 100");

  emit({ kind: "players", players: [{ playerId: 3, name: "Nate", muted: false, volume: 0 }] });
  const floor = sent.length;
  rowButtons(rowItems()[0])[1].click();
  assert.equal(sent.length, floor, "- at 0 sends nothing");
}

{
  emit({ kind: "players", players: twoPlayers });
  assert.equal(speakingText(rowItems()[0]), "", "no speaking marker before a speakers event");
  emit({ kind: "speakers", playerIds: [7] });
  assert.equal(speakingText(rowItems()[1]), "Speaking", "a speaking player has the marker");
  assert.equal(speakingText(rowItems()[0]), "", "other rows have no marker");
  emit({ kind: "speakers", playerIds: [] });
  assert.equal(speakingText(rowItems()[1]), "", "the marker clears with the speakers event");
}

{
  emit({ kind: "players", players: [] });
  assert.equal(statusText(), "No other players in the session.", "an empty session says so");
  assert.equal(rowItems().length, 0);
  emit({ kind: "status", active: false, micHot: false, pending: false, code: "idle" });
  assert.equal(statusText(), "Voice chat is not active.", "an inactive session says so");
  assert.equal(rowItems().length, 0, "no rows while inactive");
}

{
  const freshIds = {
    coVoiceStatus: fakeElement("coVoiceStatus", "p"),
    coVoiceList: fakeElement("coVoiceList", "ol")
  };
  const freshWindow = { addEventListener() {}, coVoiceAction() {} };
  const freshDocument = {
    readyState: "complete",
    activeElement: null,
    createElement(tagName) {
      return fakeElement("", tagName);
    },
    getElementById(id) {
      return freshIds[id] || null;
    },
    addEventListener() {}
  };
  vm.runInContext(playersSource, vm.createContext({ document: freshDocument, window: freshWindow, console, Array, Math, Number, Object, String, JSON }));
  freshWindow.coVoiceEvent({ kind: "status", active: false, micHot: false, pending: false, code: "idle" });
  assert.equal(freshIds.coVoiceStatus.textContent, "Voice chat is not active.", "a page whose first event is an inactive status shows the inactive text");
  assert.equal(freshIds.coVoiceList.children.length, 0, "a page whose first event is an inactive status has no rows");
}

{
  emit({ kind: "status", active: true, micHot: false, pending: false, code: "ready" });
  emit({ kind: "players", players: twoPlayers });
  window.handleControllerInput("rb", "down");
  assert.equal(byId.coVoice.hidden, false, "rb opens the Voice tab's players panel");
  assert.equal(byId.coFaction.hidden, true, "the Faction panel is hidden while the Voice tab is shown");
  assert.equal(byId.coTabVoice.attrs["aria-selected"], "true");
  assert.equal(byId.coTabFaction.attrs["aria-selected"], "false");

  const order = rowItems().reduce((all, item) => all.concat(rowButtons(item)), []);
  assert.equal(order.length, 6, "three buttons per player row");
  document.activeElement = null;
  window.handleControllerInput("dpad_down", "down");
  assert.equal(document.activeElement, order[0], "dpad down with no focus starts on the first row button");
  for (let index = 1; index < order.length; index += 1) {
    window.handleControllerInput("dpad_down", "down");
    assert.equal(document.activeElement, order[index], `dpad down reaches row button ${index}`);
  }
  window.handleControllerInput("dpad_down", "down");
  assert.equal(document.activeElement, order[0], "dpad down wraps from the last row button to the first");
  window.handleControllerInput("dpad_up", "down");
  assert.equal(document.activeElement, order[order.length - 1], "dpad up wraps from the first row button to the last");

  document.activeElement = order[0];
  document.listeners.keydown(key("ArrowDown"));
  assert.equal(document.activeElement, order[1], "the arrow key moves to the next row button");
  document.listeners.keydown(key("ArrowUp"));
  assert.equal(document.activeElement, order[0], "the arrow key moves back");

  document.activeElement = order[0];
  const count = sent.length;
  window.handleControllerInput("a", "down");
  assert.equal(sent[sent.length - 1], '{"op":"mutePlayer","playerId":3,"muted":true}', "A on a focused Mute button sends its mute action");
  assert.equal(sent.length, count + 1);
  document.activeElement = order[2];
  document.listeners.keydown(key("Enter"));
  assert.equal(sent[sent.length - 1], '{"op":"setPlayerVolume","playerId":3,"volume":60}', "Enter on a focused volume button sends its action");

}

{
  emit({ kind: "players", players: twoPlayers });
  const mute = rowButtons(rowItems()[0])[0];
  document.activeElement = mute;
  emit({ kind: "speakers", playerIds: [7] });
  assert.equal(rowButtons(rowItems()[0])[0], mute, "a speakers event keeps the Mute control node");
  assert.equal(document.activeElement, mute, "focus stays on the same row control");
  assert.equal(speakingText(rowItems()[1]), "Speaking", "the speaker marker updates in place");
  emit({ kind: "status", active: true, micHot: true, pending: false, code: "transmitting" });
  assert.equal(rowButtons(rowItems()[0])[0], mute, "a status event that keeps the session active keeps the row");
  const before = sent.length;
  mute.click();
  assert.equal(sent[sent.length - 1], '{"op":"mutePlayer","playerId":3,"muted":true}', "the kept Mute control still sends its action");
  assert.equal(sent.length, before + 1);
  window.handleControllerInput("dpad_down", "down");
  assert.equal(document.activeElement, rowButtons(rowItems()[0])[1], "dpad down continues from the kept focus");
}

{
  window.handleControllerInput("lb", "down");
  assert.equal(byId.coFaction.hidden, false, "lb from the Voice tab returns to the Faction panel");
  assert.equal(byId.coVoice.hidden, true);
  assert.equal(byId.coTabFaction.attrs["aria-selected"], "true");
  window.handleControllerInput("rb", "down");
  assert.equal(byId.coVoice.hidden, false, "rb from the Faction panel goes to the Voice tab");
  assert.equal(byId.coFaction.hidden, true);
}

{
  emit({ kind: "status", active: true, micHot: false, pending: false, code: "ready" });
  emit({ kind: "players", players: twoPlayers });
  const mute = rowButtons(rowItems()[0])[0];
  emit({ kind: "speakers", playerIds: [7] });
  emit({ kind: "level", dbfs: -20 });
  emit({ kind: "settings", values: { "voice.micMuted": true } });
  emit({ kind: "keyCapture", target: "voice.pushToTalkKey", state: "cancelled", name: "", vk: 0 });
  assert.equal(rowButtons(rowItems()[0])[0], mute, "level, settings and keyCapture events keep the row controls in place");
  assert.equal(speakingText(rowItems()[1]), "Speaking", "level, settings and keyCapture events keep the speaker marker");
  emit({ kind: "players", players: twoPlayers });
  assert.equal(speakingText(rowItems()[1]), "Speaking", "the speaker marker survives the next players repaint");
}

{
  emit({ kind: "players", players: twoPlayers });
  document.activeElement = rowButtons(rowItems()[0])[2];
  emit({ kind: "players", players: twoPlayers });
  assert.equal(document.activeElement, rowButtons(rowItems()[0])[2], "a players repaint keeps focus on the same volume control");
}

{
  assert.match(markup, /<nav class="faction-tabs"[^>]*>[\s\S]*id="coTabFaction"[\s\S]*id="coTabVoice"[\s\S]*<\/nav>/, "the Voice tab sits after the Faction tab in the tab bar");
  assert.ok(markup.includes('<button type="button" class="faction-tab" id="coTabVoice" role="tab" aria-selected="false">Voice</button>'), "the Voice tab keeps its type, class, id and tab role");
  assert.match(markup, /<section class="faction" id="coVoice" hidden>/, "the players panel starts hidden");
  const ids = [...new Set([...playersSource.matchAll(/getElementById\("(\w+)"\)/g)].map((match) => match[1]))];
  assert.equal(ids.length, 6, "voice-players.js looks up its status, list and the two tab-switch elements");
  for (const id of ids) {
    assert.ok(markup.includes(`id="${id}"`), `index.html has #${id}, which voice-players.js paints`);
  }
}

{
  const detached = { readyState: "complete", getElementById() { return null; }, addEventListener() {} };
  const bare = { addEventListener() {} };
  const bareContext = vm.createContext({ document: detached, window: bare, console, Array, Math, Number, Object, String, JSON });
  vm.runInContext(playersSource, bareContext);
  assert.doesNotThrow(() => bare.coVoiceEvent({ kind: "players", players: twoPlayers }), "events without a DOM are ignored");
}

console.log("Voice players regression passed");
