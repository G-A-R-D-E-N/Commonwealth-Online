const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const keyboardSource = fs.readFileSync(__dirname + "/../browser/components/keyboard.js", "utf8");
const membersSource = fs.readFileSync(__dirname + "/faction.members.js", "utf8");
const buildSource = fs.readFileSync(__dirname + "/faction.build.js", "utf8");
const ownershipSource = fs.readFileSync(__dirname + "/faction.ownership.js", "utf8");
const browseSource = fs.readFileSync(__dirname + "/faction.browse.js", "utf8");
const focusSource = fs.readFileSync(__dirname + "/faction.focus.js", "utf8");
const source = fs.readFileSync(__dirname + "/faction.js", "utf8");

function harness() {
  const listeners = { root: {}, document: {} };
  const sent = [];
  const keyboardHost = {
    innerHTML: "",
    classList: {
      hidden: false,
      add() { this.hidden = true; },
      remove() { this.hidden = false; }
    }
  };
  let html = "";
  const root = {
    writes: 0,
    failQuery: false,
    elements: {},
    onWrite: null,
    get innerHTML() {
      return html;
    },
    set innerHTML(value) {
      html = value;
      this.writes += 1;
      this.elements = {};
      if (typeof this.onWrite === "function") {
        this.onWrite(this);
      }
    },
    addEventListener(type, fn) {
      listeners.root[type] = fn;
    },
    querySelector(selector) {
      if (this.failQuery && selector !== ".co-keyboard") {
        throw new SyntaxError("bad selector");
      }
      if (selector === ".co-keyboard") {
        return keyboardHost;
      }
      return this.elements[selector] || null;
    },
    querySelectorAll() {
      return [];
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
      return id === "coFaction" ? root : null;
    },
    addEventListener(type, fn) {
      listeners.document[type] = fn;
    }
  };
  const window = {
    innerWidth: 1920,
    coFactionAction(payload) {
      sent.push(JSON.parse(payload));
    }
  };
  vm.runInNewContext(keyboardSource + "\n" + membersSource + "\n" + buildSource + "\n" + ownershipSource + "\n" + browseSource + "\n" + focusSource + "\n" + source, { document, window, JSON, String, Number, Object, Array, Math, Error });
  const field = (id, value) => {
    const element = {
      id,
      tagName: "INPUT",
      value,
      selectionStart: value.length,
      selectionEnd: value.length,
      dispatchEvent() {
        listeners.root.input({ target: element });
        return true;
      }
    };
    return element;
  };
  return {
    root,
    document,
    sent,
    keyboardHost,
    keyboard: window.CO_Keyboard,
    model: window.coFactionModel,
    setState: window.coFactionSetState,
    controller: window.handleControllerInput,
    field,
    click(data) {
      listeners.root.click({ target: { closest: () => ({ dataset: data }) } });
    },
    input(id, value) {
      listeners.root.input({ target: { id, value } });
    },
    keydown(key) {
      let prevented = false;
      listeners.document.keydown({ key, preventDefault() { prevented = true; } });
      return prevented;
    }
  };
}

const member = { profileId: "p1", playerId: 3, name: "Nate", online: true, leader: true };
const recruit = { profileId: "p2", playerId: 7, name: "Codsworth", online: false, leader: false };
const faction = { factionId: "f-1", name: "Minutemen Remnant", tag: "MMR", leaderPlayerId: 3, members: [member, recruit] };
const roster = [
  { playerId: 3, name: "Nate" },
  { playerId: 5, name: "Piper", tag: "CA" },
  { playerId: 7, name: "Codsworth" }
];
const invites = [{ inviteId: "i-4", factionName: "Atom Cats", factionTag: "CAT", fromName: "Zeke", expiresInSeconds: 42 }];
const notInFaction = { enabled: true, localPlayerId: 9, faction: null, pendingInvites: invites, roster, lastResult: null };
const leaderState = { enabled: true, localPlayerId: 3, faction, pendingInvites: [], roster, lastResult: null };
const memberState = { enabled: true, localPlayerId: 7, faction, pendingInvites: [], roster, lastResult: null };
const onlineRecruit = { profileId: "p2", playerId: 7, name: "Codsworth", online: true, leader: false };
const promoteState = { enabled: true, localPlayerId: 3, faction: { ...faction, members: [member, onlineRecruit] }, pendingInvites: [], roster, lastResult: null };

function plain(value) {
  return JSON.parse(JSON.stringify(value));
}

module.exports = { assert, fs, vm, keyboardSource, membersSource, buildSource, ownershipSource, browseSource, focusSource, source, harness, member, recruit, faction, roster, invites, notInFaction, leaderState, memberState, onlineRecruit, promoteState, plain };
