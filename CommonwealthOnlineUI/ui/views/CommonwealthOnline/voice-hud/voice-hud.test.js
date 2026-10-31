const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const source = fs.readFileSync(__dirname + "/voice-hud.js", "utf8");

function fakeElement(tagName) {
  return {
    tagName,
    className: "",
    hidden: false,
    children: [],
    appendChild(child) {
      this.children.push(child);
    },
    set textContent(value) {
      this.text = String(value);
      if (value === "") {
        this.children = [];
      }
    },
    get textContent() {
      return this.text === undefined ? "" : this.text;
    }
  };
}

function load(withElements) {
  const elements = {
    coVoiceMic: fakeElement("p"),
    coVoiceSpeakers: fakeElement("ol"),
    coVoicePending: fakeElement("p")
  };
  const window = {};
  const document = {
    createElement: fakeElement,
    getElementById(id) {
      return withElements ? elements[id] || null : null;
    }
  };
  vm.runInNewContext(source, { document, window });
  return { window, elements };
}

const plain = (value) => JSON.parse(JSON.stringify(value));
const names = (hud) => plain(hud.view().speakers);

const { window } = load(false);
const createHud = window.coVoiceHudModel.createHud;

{
  const truth = [
    [true, true, true],
    [true, false, false],
    [false, true, false],
    [false, false, false]
  ];
  for (const [active, micHot, visible] of truth) {
    const hud = createHud();
    hud.apply({ kind: "status", active, micHot });
    assert.equal(hud.view().micVisible, visible, `active=${active} micHot=${micHot} gives micVisible=${visible}`);
  }
  const missing = createHud();
  missing.apply({ kind: "status", active: true });
  assert.equal(missing.view().micVisible, false, "a missing micHot counts as false");
  const notTrue = createHud();
  notTrue.apply({ kind: "status", active: "yes", micHot: true });
  assert.equal(notTrue.view().micVisible, false, "active must be exactly true");
  const loose = createHud();
  loose.apply({ kind: "status", active: true, micHot: "yes" });
  assert.equal(loose.view().micVisible, false, "micHot must be exactly true");
  loose.apply({ kind: "status", active: true, micHot: 1 });
  assert.equal(loose.view().micVisible, false, "micHot 1 is not true");
  const flipped = createHud();
  flipped.apply({ kind: "status", active: true, micHot: true });
  flipped.apply({ kind: "status", active: true, micHot: false });
  assert.equal(flipped.view().micVisible, false, "the last status event decides the mic indicator");
}

{
  const hud = createHud();
  hud.apply({ kind: "status", active: true, micHot: false });
  hud.apply({ kind: "players", players: [{ playerId: 7, name: "Codsworth" }, { playerId: 9, name: "Hancock" }] });
  hud.apply({ kind: "speakers", playerIds: [9, 4, 7] });
  assert.deepEqual(names(hud), ["Hancock", "Player 4", "Codsworth"], "speakers keep their order and fall back to Player <id>");
  hud.apply({ kind: "speakers", playerIds: [-1, 1.5, "7", null, 7] });
  assert.deepEqual(names(hud), ["Codsworth"], "ids that are not non-negative integers are skipped");
  hud.apply({ kind: "speakers", playerIds: "7" });
  assert.deepEqual(names(hud), ["Codsworth"], "a speakers event without an array keeps the last list");
}

{
  const hud = createHud();
  hud.apply({ kind: "status", active: true, micHot: false, pending: true });
  assert.equal(hud.view().pending, true, "pending shows while a settings change is unacknowledged");
  hud.apply({ kind: "status", active: true, micHot: false, pending: false });
  assert.equal(hud.view().pending, false);
  hud.apply({ kind: "status", active: false, micHot: false, pending: true });
  assert.equal(hud.view().pending, false, "pending is hidden while voice is inactive");
  hud.apply({ kind: "status", active: true, micHot: false });
  assert.equal(hud.view().pending, false, "a missing pending counts as false");
  hud.apply({ kind: "status", active: true, micHot: false, pending: "yes" });
  assert.equal(hud.view().pending, false, "pending must be exactly true");
  hud.apply({ kind: "status", active: true, micHot: false, pending: 1 });
  assert.equal(hud.view().pending, false, "pending 1 is not true");
}

{
  const hud = createHud();
  hud.apply({ kind: "status", active: true, micHot: true, pending: true });
  hud.apply({ kind: "players", players: [{ playerId: 7, name: "Codsworth" }] });
  hud.apply({ kind: "speakers", playerIds: [7] });
  hud.apply({ kind: "status", active: false, micHot: true, pending: true });
  assert.deepEqual(plain(hud.view()), { micVisible: false, speakers: [], pending: false }, "inactive hides everything");
  hud.apply({ kind: "speakers", playerIds: [7] });
  assert.deepEqual(names(hud), [], "speakers received while inactive stay hidden");
  hud.apply({ kind: "status", active: true, micHot: false });
  assert.deepEqual(names(hud), [], "a cleared speakers list is not replayed after reactivation");
}

{
  const stale = createHud();
  stale.apply({ kind: "status", active: true, micHot: false });
  stale.apply({ kind: "players", players: [{ playerId: 7, name: "Codsworth" }] });
  stale.apply({ kind: "status", active: false });
  stale.apply({ kind: "status", active: true, micHot: false });
  stale.apply({ kind: "speakers", playerIds: [7] });
  assert.deepEqual(names(stale), ["Player 7"], "a players list from before an inactive period is not used after reactivation");
  stale.apply({ kind: "status", active: false });
  stale.apply({ kind: "players", players: [{ playerId: 7, name: "Codsworth" }] });
  stale.apply({ kind: "status", active: true, micHot: false });
  stale.apply({ kind: "speakers", playerIds: [7] });
  assert.deepEqual(names(stale), ["Codsworth"], "a players list received while inactive is kept until an inactive status clears it");
}

{
  const hud = createHud();
  hud.apply({ kind: "status", active: true, micHot: true });
  hud.apply({ kind: "players", players: [{ playerId: 7, name: "Codsworth" }] });
  hud.apply({ kind: "speakers", playerIds: [7] });
  const malformed = [
    null,
    undefined,
    "status",
    5,
    [],
    {},
    { kind: "unknown" },
    { kind: "status" },
    { kind: "status", active: "yes", micHot: false },
    { kind: "players", players: "x" },
    { kind: "speakers", playerIds: null }
  ];
  for (const event of malformed) {
    assert.doesNotThrow(() => hud.apply(event), `malformed ${JSON.stringify(event)} must not throw`);
  }
  assert.deepEqual(plain(hud.view()), { micVisible: true, speakers: ["Codsworth"], pending: false }, "malformed events change nothing");
  hud.apply({ kind: "speakers", playerIds: [2, 3, 7] });
  hud.apply({ kind: "players", players: [null, { playerId: "1", name: "x" }, { playerId: 2 }, { playerId: 3, name: 4 }, { playerId: 7, name: "Codsworth" }] });
  assert.deepEqual(names(hud), ["Player 2", "Player 3", "Codsworth"], "invalid players entries are dropped and the valid one still resolves");
}

{
  const hud = createHud();
  hud.apply({ kind: "status", active: true, micHot: true });
  hud.apply({ kind: "players", players: [{ playerId: 7, name: "Codsworth" }] });
  hud.apply({ kind: "speakers", playerIds: [7] });
  hud.apply({ kind: "settings", values: { "voice.micMuted": true } });
  hud.apply({ kind: "keyCapture", target: "voice.pushToTalkKey", state: "captured", name: "V", vk: 86 });
  hud.apply({ kind: "level", dbfs: -30 });
  assert.equal(hud.view().micVisible, true, "settings, keyCapture and level events are ignored");
  assert.deepEqual(names(hud), ["Codsworth"], "settings, keyCapture and level events keep the speaker list");
}

{
  const { window: live, elements } = load(true);
  live.coVoiceEvent({ kind: "status", active: true, micHot: true, pending: true });
  live.coVoiceEvent({ kind: "players", players: [{ playerId: 3, name: '<img src=x onerror="alert(1)"><script>x</script>' }] });
  live.coVoiceEvent({ kind: "speakers", playerIds: [3, 8] });
  assert.equal(elements.coVoiceMic.hidden, false, "the mic indicator shows while the mic is hot");
  assert.equal(elements.coVoicePending.hidden, false, "the syncing line shows while pending");
  assert.equal(elements.coVoiceSpeakers.children.length, 2);
  assert.equal(elements.coVoiceSpeakers.children[0].textContent, '<img src=x onerror="alert(1)"><script>x</script>', "a player name is set as text");
  assert.equal(elements.coVoiceSpeakers.children[1].textContent, "Player 8");
  live.coVoiceEvent({ kind: "status", active: false, micHot: true, pending: true });
  assert.equal(elements.coVoiceMic.hidden, true, "the mic indicator hides when voice is inactive");
  assert.equal(elements.coVoicePending.hidden, true);
  assert.equal(elements.coVoiceSpeakers.children.length, 0, "the speaker list empties when voice is inactive");
}

{
  const markup = fs.readFileSync(__dirname + "/index.html", "utf8");
  const ids = [...source.matchAll(/getElementById\("(\w+)"\)/g)].map((match) => match[1]);
  assert.equal(ids.length, 3, "voice-hud.js looks up three elements");
  for (const id of ids) {
    assert.ok(markup.includes(`id="${id}"`), `index.html has #${id}, which voice-hud.js paints`);
  }
  const mic = /<p[^>]*id="coVoiceMic"[^>]*>([\s\S]*?)<\/p>/.exec(markup);
  assert.match(mic[1], /<svg[\s>]/, "coVoiceMic holds an svg glyph");
  assert.match(mic[1], />\s*Mic on\s*</, "coVoiceMic says Mic on");
  const pending = /<p[^>]*id="coVoicePending"[^>]*>([^<]*)<\/p>/.exec(markup);
  assert.equal(pending[1], "Syncing voice settings", "coVoicePending carries the syncing text");
}

{
  const { window: bare } = load(false);
  assert.doesNotThrow(() => bare.coVoiceEvent({ kind: "status", active: true, micHot: true }), "events without a DOM are ignored");
}

console.log("Voice HUD regression passed");
