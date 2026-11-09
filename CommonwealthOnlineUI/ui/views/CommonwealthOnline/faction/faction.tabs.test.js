const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const dir = __dirname;
const read = (name) => fs.readFileSync(dir + "/" + name, "utf8");

const listeners = {};
const byId = {};

function fakeElement(tagName) {
  const element = {
    tagName: String(tagName || "").toUpperCase(),
    children: [],
    dataset: {},
    attrs: {},
    listeners: {},
    hidden: false,
    text: "",
    focusable: [],
    parentNode: null,
    style: {},
    _id: "",
    get id() {
      return this._id;
    },
    set id(value) {
      this._id = String(value);
      if (this._id) byId[this._id] = this;
    },
    get innerHTML() {
      return this.text;
    },
    set innerHTML(value) {
      this.text = String(value);
      if (value === "") this.children = [];
    },
    get textContent() {
      return this.text;
    },
    set textContent(value) {
      this.text = String(value);
    },
    setAttribute(name, value) {
      this.attrs[name] = String(value);
    },
    getAttribute(name) {
      return Object.prototype.hasOwnProperty.call(this.attrs, name) ? this.attrs[name] : null;
    },
    appendChild(child) {
      this.children.push(child);
      child.parentNode = this;
      return child;
    },
    insertBefore(child, reference) {
      const at = this.children.indexOf(reference);
      this.children.splice(at < 0 ? this.children.length : at, 0, child);
      child.parentNode = this;
      return child;
    },
    addEventListener(type, fn) {
      this.listeners[type] = fn;
    },
    querySelector() {
      return null;
    },
    querySelectorAll() {
      return this.focusable;
    },
    closest(selector) {
      return selector === "[role=tab]" && this.attrs.role === "tab" ? this : null;
    },
    focus() {
      document.activeElement = this;
    },
    click() {
      if (this.listeners.click) this.listeners.click({ target: this });
    }
  };
  return element;
}

const body = fakeElement("body");
const nav = fakeElement("nav");
const coFaction = fakeElement("main");
coFaction.id = "coFaction";
const coVoice = fakeElement("section");
coVoice.id = "coVoice";
coVoice.hidden = true;
const coTabFaction = fakeElement("button");
coTabFaction.id = "coTabFaction";
coTabFaction.setAttribute("role", "tab");
coTabFaction.setAttribute("aria-selected", "true");
const coTabVoice = fakeElement("button");
coTabVoice.id = "coTabVoice";
coTabVoice.setAttribute("role", "tab");
coTabVoice.setAttribute("aria-selected", "false");
nav.appendChild(coTabFaction);
nav.appendChild(coTabVoice);
body.appendChild(coFaction);
body.appendChild(coVoice);

const factionButtons = [fakeElement("button"), fakeElement("button")];
coFaction.focusable = factionButtons;
const voiceButtons = [fakeElement("button")];
coVoice.focusable = voiceButtons;

const document = {
  readyState: "complete",
  activeElement: null,
  documentElement: { style: { setProperty() {} } },
  createElement(tagName) {
    return fakeElement(tagName);
  },
  getElementById(id) {
    return byId[id] || null;
  },
  querySelector(selector) {
    return selector === ".faction-tabs" ? nav : null;
  },
  querySelectorAll() {
    return [];
  },
  addEventListener(type, fn) {
    listeners[type] = fn;
  }
};

const window = { innerWidth: 1920, addEventListener() {} };
const context = vm.createContext({ document, window, console, Array, Math, Number, Object, String, JSON });

for (const source of [
  "faction.members.js",
  "faction.focus.js",
  "faction.js",
  "faction.tabs.js",
  "faction.friends.js"
]) {
  vm.runInContext(read(source), context);
}

const tabs = window.coFactionTabs;
assert.ok(tabs, "the tab module must publish itself");
const panelOf = { coTabFaction: coFaction, coTabFriends: byId.coFriends, coTabVoice: coVoice };

assert.equal(tabs.active(), "coTabFaction", "the Social window starts on Faction");
assert.equal(coFaction.hidden, false);
assert.equal(panelOf.coTabFriends.hidden, true, "the Friends panel starts hidden");
assert.equal(coVoice.hidden, true, "the Voice panel starts hidden");

const friendsButtons = [fakeElement("button"), fakeElement("button"), fakeElement("button")];
panelOf.coTabFriends.focusable = friendsButtons;

function selectedTab() {
  return ["coTabFaction", "coTabFriends", "coTabVoice"].filter(
    (id) => byId[id].getAttribute("aria-selected") === "true"
  );
}

window.handleControllerInput("rb", "down");
assert.equal(tabs.active(), "coTabFriends", "rb reaches the Friends tab instead of skipping to Voice");
assert.equal(panelOf.coTabFriends.hidden, false, "the Friends panel is shown by the controller");
assert.equal(coFaction.hidden, true, "the Faction panel is hidden while Friends is shown");
assert.equal(coVoice.hidden, true, "the Voice panel stays hidden while Friends is shown");
assert.deepEqual(selectedTab(), ["coTabFriends"], "exactly one tab is selected");
assert.equal(document.activeElement, friendsButtons[0], "switching tabs focuses the first Friends control");

document.activeElement = null;
window.handleControllerInput("dpad_down", "down");
assert.equal(document.activeElement, friendsButtons[0], "the controller reaches the Friends controls");
window.handleControllerInput("dpad_down", "down");
assert.equal(document.activeElement, friendsButtons[1], "dpad down walks the Friends controls");
window.handleControllerInput("dpad_up", "down");
assert.equal(document.activeElement, friendsButtons[0], "dpad up walks back");

assert.equal(tabs.focusables().length, 3, "focusables() are the visible panel's controls");
tabs.show("coTabFaction");
assert.equal(tabs.focusables().length, 2, "focusables() follow the selected tab");

window.handleControllerInput("lb", "down");
assert.equal(tabs.active(), "coTabVoice", "lb from Faction wraps backwards to Voice");
assert.equal(coVoice.hidden, false);
assert.equal(coFaction.hidden, true);
assert.equal(panelOf.coTabFriends.hidden, true, "Voice never opens on top of Friends");
window.handleControllerInput("rb", "down");
assert.equal(tabs.active(), "coTabFaction", "rb from Voice wraps to Faction");
window.handleControllerInput("rb", "down");
assert.equal(tabs.active(), "coTabFriends", "rb then reaches Friends");
window.handleControllerInput("rb", "down");
assert.equal(tabs.active(), "coTabVoice", "rb leaves Friends for Voice");

assert.equal(
  [coFaction.hidden, panelOf.coTabFriends.hidden, coVoice.hidden].filter((hidden) => !hidden).length,
  1,
  "one panel is visible at a time"
);

nav.listeners.click({ target: byId.coTabFriends });
assert.equal(tabs.active(), "coTabFriends", "clicking the Friends tab selects it");
assert.equal(panelOf.coTabFriends.hidden, false);
assert.equal(coVoice.hidden, true);
nav.listeners.click({ target: byId.coTabFaction });
assert.equal(tabs.active(), "coTabFaction", "clicking the Faction tab selects it");
assert.equal(panelOf.coTabFriends.hidden, true);
nav.listeners.click({ target: byId.coTabVoice });
assert.equal(panelOf.coTabFriends.hidden, true, "a Voice click never leaves Friends visible");

const friendsState = JSON.stringify({
  supported: true,
  friends: [{ friendId: "f1", name: "Alice", online: true }],
  pendingIncoming: [{ requestId: "fr-1", displayName: "Carol" }],
  pendingOutgoingPlayerIds: []
});
tabs.show("coTabFaction");
window.coFactionSetState({ enabled: true, friends: friendsState });
assert.equal(panelOf.coTabFriends.hidden, true, "a hidden panel stays hidden on a state push");
window.handleControllerInput("rb", "down");
assert.equal(tabs.active(), "coTabFriends");
assert.doesNotMatch(panelOf.coTabFriends.text, /does not support friends/, "a string friends payload is parsed, not dropped");
assert.match(panelOf.coTabFriends.text, /Alice/, "the controller cycle repaints the Friends panel");
assert.match(panelOf.coTabFriends.text, /Carol wants to add you as a friend/, "a request that arrived while hidden is drawn on switch");
window.coFactionSetState({ enabled: true, friends: JSON.stringify({ supported: true, friends: [{ friendId: "f2", name: "Bob", online: false }] }) });
assert.doesNotMatch(panelOf.coTabFriends.text, /Alice/, "a state push repaints the panel while it is open");
assert.match(panelOf.coTabFriends.text, /Bob/);

assert.doesNotMatch(read("faction.friends.js"), /selectOthers|selectFriends/, "the tab state has one owner");
assert.doesNotMatch(read("faction.js"), /coVoiceToggleTab/, "the controller cycle must not bypass the tab module");

console.log("faction.tabs.test: all checks passed.");
