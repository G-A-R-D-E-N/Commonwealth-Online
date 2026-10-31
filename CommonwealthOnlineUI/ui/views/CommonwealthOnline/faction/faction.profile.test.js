const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const keyboardSource = fs.readFileSync(__dirname + "/../browser/components/keyboard.js", "utf8");
const membersSource = fs.readFileSync(__dirname + "/faction.members.js", "utf8");
const focusSource = fs.readFileSync(__dirname + "/faction.focus.js", "utf8");
const source = fs.readFileSync(__dirname + "/faction.js", "utf8");

function harness() {
  const listeners = { root: {}, document: {} };
  const sent = [];
  const buttonCache = new Map();
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
  vm.runInNewContext(keyboardSource + "\n" + membersSource + "\n" + focusSource + "\n" + source, { document, window, JSON, String, Number, Object, Array, Math, Error });
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
    buttons() {
      return (root.innerHTML.match(/<button[^>]*>/g) || [])
        .filter((tag) => /data-op=/.test(tag) && !/disabled/.test(tag))
        .map((tag) => {
          const op = tag.match(/data-op="([^"]+)"/)[1];
          const color = (tag.match(/data-color="([^"]+)"/) || [])[1];
          const tab = (tag.match(/data-tab="([^"]+)"/) || [])[1];
          const key = `${op}|${color}|${tab}`;
          if (!buttonCache.has(key)) {
            const element = {
              tagName: "BUTTON",
              dataset: { op, ...(color === undefined ? {} : { color }), ...(tab === undefined ? {} : { tab }) },
              focus() { document.activeElement = element; },
              click() { listeners.root.click({ target: { closest: () => element } }); }
            };
            buttonCache.set(key, element);
          }
          return buttonCache.get(key);
        });
    },
    keydown(key) {
      let prevented = false;
      listeners.document.keydown({ key, preventDefault() { prevented = true; } });
      return prevented;
    }
  };
}

const member = { profileId: "p1", playerId: 3, name: "Nate", online: true, leader: true, officer: false };
const recruit = { profileId: "p2", playerId: 7, name: "Codsworth", online: true, leader: false, officer: false };
const officerRecruit = { ...recruit, officer: true };
const profileFaction = {
  factionId: "f-1",
  name: "Minutemen Remnant",
  tag: "MMR",
  description: "Holding the line.",
  color: "#3ECF4A",
  leaderPlayerId: 3,
  members: [member, recruit]
};
const roster = [{ playerId: 3, name: "Nate" }, { playerId: 9, name: "Piper" }];
const leaderState = { enabled: true, localPlayerId: 3, faction: profileFaction, pendingInvites: [], roster, lastResult: null };
const memberState = { enabled: true, localPlayerId: 7, faction: profileFaction, pendingInvites: [], roster, lastResult: null };
const officerState = {
  enabled: true,
  localPlayerId: 7,
  faction: { ...profileFaction, members: [member, officerRecruit] },
  pendingInvites: [],
  roster,
  lastResult: null
};
const notInFaction = { enabled: true, localPlayerId: 9, faction: null, pendingInvites: [], roster, lastResult: null };

function plain(value) {
  return JSON.parse(JSON.stringify(value));
}

{
  const h = harness();
  h.setState({ ...leaderState, faction: { ...profileFaction, description: '<script>alert("x")</script>' } });
  assert.match(h.root.innerHTML, /<p class="faction-desc">&lt;script&gt;alert\(&quot;x&quot;\)&lt;\/script&gt;<\/p>/, "the leader must see the description escaped");
  assert.doesNotMatch(h.root.innerHTML, /<script>alert/, "a description must not inject markup");
}

{
  const h = harness();
  h.setState({ ...memberState, faction: { ...profileFaction, description: '<img src=x onerror="alert(1)">' } });
  assert.match(h.root.innerHTML, /<p class="faction-desc">&lt;img src=x onerror=&quot;alert\(1\)&quot;&gt;<\/p>/, "a member must see the description escaped");
  assert.doesNotMatch(h.root.innerHTML, /<img /, "a member description must not inject markup");
}

{
  const h = harness();
  h.setState(memberState);
  h.click({ op: "mine-tab", tab: "profile" });
  const html = h.root.innerHTML;
  assert.ok(html.indexOf("Minutemen Remnant") < html.indexOf('<p class="faction-desc">Holding the line.</p>'), "the description must sit under the faction name");
  assert.match(html, /<span class="faction-swatch" style="background:#3ECF4A"><\/span>/, "a valid colour shows the swatch");
}

{
  for (const color of ["#3ecf4a", "red", "#3ECF4", "#3ECF4AFF", "", "#3ECF4A;position:fixed", undefined]) {
    const h = harness();
    h.setState({ ...memberState, faction: { ...profileFaction, color } });
    assert.doesNotMatch(h.root.innerHTML, /faction-swatch/, `colour ${JSON.stringify(color)} must not show a swatch`);
  }
}

{
  const h = harness();
  h.setState(memberState);
  h.click({ op: "mine-tab", tab: "profile" });
  assert.doesNotMatch(h.root.innerHTML, /coFactionDescription|coFactionMotd|pick-color|data-op="setProfile"|data-op="clear-color"/, "a member must not see the profile edit controls");
  assert.doesNotMatch(h.root.innerHTML, /FACTION PROFILE/, "a member must not see the profile edit section");
}

{
  const h = harness();
  h.setState(officerState);
  assert.doesNotMatch(h.root.innerHTML, /coFactionDescription|coFactionMotd|pick-color|data-op="setProfile"|data-op="clear-color"/, "an officer must not see the profile edit controls");
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "profile" });
  assert.match(h.root.innerHTML, /id="coFactionDescription" type="text" maxlength="200"/, "the leader gets a description input");
  assert.equal((h.root.innerHTML.match(/data-op="pick-color"/g) || []).length, 8, "the leader gets 8 palette buttons");
  assert.match(h.root.innerHTML, /<button[^>]*data-color="#27AE60"[^>]*aria-label="Colour #27AE60"/, "each palette button carries its colour");
  assert.match(h.root.innerHTML, /data-op="clear-color">Clear colour<\/button>/, "the leader gets a Clear colour option");
  assert.match(h.root.innerHTML, /data-op="setProfile">Save<\/button>/, "the leader gets a Save button with data-op setProfile");
}

{
  const h = harness();
  h.setState(notInFaction);
  assert.doesNotMatch(h.root.innerHTML, /faction-desc|faction-motd|faction-swatch|FACTION PROFILE/, "no faction means no profile at all");
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "profile" });
  h.click({ op: "setProfile" });
  assert.deepEqual(plain(h.sent), [{ op: "setProfile", description: "Holding the line.", motd: "", color: "#3ECF4A" }], "an untouched Save sends the faction's current profile");
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "profile" });
  h.input("coFactionDescription", "  Holding the line  ");
  h.click({ op: "pick-color", color: "#27AE60" });
  h.click({ op: "setProfile" });
  assert.deepEqual(plain(h.sent), [{ op: "setProfile", description: "Holding the line", motd: "", color: "#27AE60" }], "Save must trim the description and send the picked colour");
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "profile" });
  h.click({ op: "pick-color", color: "#C0392B" });
  h.click({ op: "clear-color" });
  assert.doesNotMatch(h.root.innerHTML, /is-selected/, "Clear colour deselects the palette");
  h.click({ op: "setProfile" });
  assert.deepEqual(plain(h.sent), [{ op: "setProfile", description: "Holding the line.", motd: "", color: "" }], "Clear colour then Save sends an empty colour");
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "profile" });
  h.input("coFactionDescription", "Café");
  h.click({ op: "setProfile" });
  assert.deepEqual(h.sent, [], "a non-ASCII description must send nothing");
  assert.match(h.root.innerHTML, /Use up to 200 plain characters, no quotes or backslashes\./, "a refused description must say why");
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "profile" });
  h.input("coFactionDescription", "x".repeat(201));
  h.click({ op: "setProfile" });
  assert.deepEqual(h.sent, [], "a description over 200 characters must send nothing");
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "profile" });
  h.input("coFactionDescription", "Typed but unsaved");
  h.setState({ ...leaderState, lastResult: { op: "invite", ok: true } });
  assert.match(h.root.innerHTML, /value="Typed but unsaved"/, "an unsaved description survives a re-render that leaves the profile alone");
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "profile" });
  h.input("coFactionDescription", "Typed");
  h.setState({ ...leaderState, faction: { ...profileFaction, description: "Saved text", color: "#112233" } });
  assert.match(h.root.innerHTML, /value="Saved text"/, "a server profile change replaces the unsaved draft");
  assert.match(h.root.innerHTML, /class="faction-swatch" style="background:#112233"/, "a server colour change replaces the colour draft");
  h.click({ op: "setProfile" });
  assert.deepEqual(plain(h.sent), [{ op: "setProfile", description: "Saved text", motd: "", color: "#112233" }], "the server colour is what an untouched Save sends");
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "profile" });
  h.input("coFactionDescription", "Enter saves");
  h.click({ op: "pick-color", color: "#8E44AD" });
  h.document.activeElement = h.field("coFactionDescription", "Enter saves");
  h.keydown("Enter");
  assert.deepEqual(plain(h.sent), [{ op: "setProfile", description: "Enter saves", motd: "", color: "#8E44AD" }], "Enter in the description saves the profile");
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "profile" });
  h.click({ op: "pick-color", color: "#27AE60" });
  const picked = h.root.innerHTML.match(/<button[^>]*data-color="#27AE60"[^>]*>/)[0];
  assert.match(picked, /is-selected/, "the picked colour is marked selected");
  assert.match(picked, /aria-pressed="true"/, "the picked colour is aria-pressed");
  assert.equal((h.root.innerHTML.match(/is-selected/g) || []).length, 1, "only the picked colour is selected");
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "profile" });
  assert.doesNotMatch(h.root.innerHTML, /is-selected/, "an off-palette colour selects no palette button");
  assert.match(h.root.innerHTML, /class="faction-swatch" style="background:#3ECF4A"/, "an off-palette colour still shows its swatch");
  h.click({ op: "setProfile" });
  assert.deepEqual(plain(h.sent), [{ op: "setProfile", description: "Holding the line.", motd: "", color: "#3ECF4A" }], "an untouched Save keeps an off-palette colour");
}

{
  for (const color of ["not-a-colour", "#c0392b", "#C039", "", undefined]) {
    const h = harness();
    h.setState(leaderState);
    h.click({ op: "mine-tab", tab: "profile" });
    h.click({ op: "pick-color", color });
    h.click({ op: "setProfile" });
    assert.deepEqual(plain(h.sent), [{ op: "setProfile", description: "Holding the line.", motd: "", color: "#3ECF4A" }], `pick-color ${JSON.stringify(color)} must be ignored`);
  }
}

{
  const h = harness();
  h.setState({ ...leaderState, lastResult: { op: "setProfile", ok: true } });
  assert.match(h.root.innerHTML, /Faction profile updated\./, "a successful setProfile shows the result text");
  assert.equal(h.model.reasonText({ op: "setProfile", ok: true }), "Faction profile updated.");
}

{
  const h = harness();
  h.setState({ ...leaderState, lastResult: { op: "setProfile", ok: false, reason: "notLeader" } });
  assert.match(h.root.innerHTML, /Only the faction leader can do that\./, "a refused setProfile shows the server reason");
}

for (const description of ['Say "hi"', "back\\slash"]) {
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "profile" });
  h.input("coFactionDescription", description);
  h.click({ op: "setProfile" });
  assert.deepEqual(h.sent, [], `a description with ${JSON.stringify(description)} must send nothing`);
  assert.match(h.root.innerHTML, /Use up to 200 plain characters, no quotes or backslashes\./, "a refused description must say why");
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "profile" });
  h.document.querySelectorAll = () => h.buttons();
  const focusOn = (match) => {
    for (let step = 0; step < 30 && !(h.document.activeElement && match(h.document.activeElement)); step += 1) {
      h.controller("dpad_down", "down");
    }
    assert.ok(h.document.activeElement && match(h.document.activeElement), "dpad must reach the target button");
  };
  focusOn((el) => el.dataset.color === "#27AE60");
  h.controller("a", "down");
  const picked = h.root.innerHTML.match(/<button[^>]*data-color="#27AE60"[^>]*>/)[0];
  assert.match(picked, /is-selected/, "A on a focused palette button selects that colour");
  focusOn((el) => el.dataset.op === "setProfile");
  h.controller("a", "down");
  assert.deepEqual(plain(h.sent), [{ op: "setProfile", description: "Holding the line.", motd: "", color: "#27AE60" }], "the controller must pick a colour and send Save");
}

{
  const h = harness();
  h.setState({ ...memberState, faction: { ...profileFaction, motd: "Raid at dusk." } });
  assert.match(h.root.innerHTML, /<p class="faction-label">ABOUT<\/p><p class="faction-desc">Holding the line\.<\/p>/, "the description must carry an ABOUT label");
  assert.match(h.root.innerHTML, /<section class="faction-motd-block"[^>]*><p class="faction-label">MESSAGE OF THE DAY<\/p><p class="faction-motd">Raid at dusk\.<\/p><\/section>/, "a member must see the MOTD as a labelled block");
  assert.doesNotMatch(h.root.innerHTML, /coFactionMotd/, "a member must not see the MOTD input");
}

for (const motd of ["", undefined]) {
  const h = harness();
  h.setState({ ...memberState, faction: { ...profileFaction, motd } });
  assert.doesNotMatch(h.root.innerHTML, /MESSAGE OF THE DAY|faction-motd/, `motd ${JSON.stringify(motd)} must show no MOTD block`);
}

{
  const h = harness();
  h.setState({ ...memberState, faction: { ...profileFaction, motd: '<script>alert("x")</script>' } });
  assert.match(h.root.innerHTML, /<p class="faction-motd">&lt;script&gt;alert\(&quot;x&quot;\)&lt;\/script&gt;<\/p>/, "a member must see the MOTD escaped");
  assert.doesNotMatch(h.root.innerHTML, /<script>alert/, "a MOTD must not inject markup");
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "profile" });
  assert.match(h.root.innerHTML, /<span>Message of the day<\/span><textarea id="coFactionMotd" rows="7" maxlength="1000"/, "the leader gets a MOTD text block labelled Message of the day");
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "profile" });
  h.input("coFactionMotd", "  Raid at dusk  ");
  h.click({ op: "setProfile" });
  assert.deepEqual(plain(h.sent), [{ op: "setProfile", description: "Holding the line.", motd: "Raid at dusk", color: "#3ECF4A" }], "Save must trim the MOTD and send it with the description");
}

for (const motd of ['Say "hi"', "back\\slash", "Café", "x".repeat(1001)]) {
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "profile" });
  h.input("coFactionMotd", motd);
  h.click({ op: "setProfile" });
  assert.deepEqual(h.sent, [], `a MOTD ${JSON.stringify(motd)} must send nothing`);
  assert.match(h.root.innerHTML, /The message of the day takes up to 1000 plain characters, no quotes or backslashes\./, "a refused MOTD must say why");
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "profile" });
  h.input("coFactionMotd", "Typed motd");
  h.setState({ ...leaderState, lastResult: { op: "invite", ok: true } });
  assert.match(h.root.innerHTML, /id="coFactionMotd"[^>]*>Typed motd<\/textarea>/, "an unsaved MOTD survives a re-render that leaves the profile alone");
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "profile" });
  h.input("coFactionMotd", "Typed");
  h.setState({ ...leaderState, faction: { ...profileFaction, motd: "Server motd" } });
  assert.match(h.root.innerHTML, /id="coFactionMotd"[^>]*>Server motd<\/textarea>/, "a server MOTD change replaces the unsaved draft");
  h.click({ op: "setProfile" });
  assert.deepEqual(plain(h.sent), [{ op: "setProfile", description: "Holding the line.", motd: "Server motd", color: "#3ECF4A" }], "the server MOTD is what an untouched Save sends");
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "profile" });
  h.input("coFactionMotd", "Enter saves motd");
  h.document.activeElement = h.field("coFactionMotd", "Enter saves motd");
  h.keydown("Enter");
  assert.deepEqual(plain(h.sent), [{ op: "setProfile", description: "Holding the line.", motd: "Enter saves motd", color: "#3ECF4A" }], "Enter in the MOTD saves the profile");
}

{
  const h = harness();
  h.setState({ ...leaderState, lastResult: { op: "setProfile", ok: false, reason: "invalidMotd" } });
  assert.match(h.root.innerHTML, /That message of the day is not allowed\./, "a refused MOTD shows the server reason");
}

console.log("Faction profile regression passed");
