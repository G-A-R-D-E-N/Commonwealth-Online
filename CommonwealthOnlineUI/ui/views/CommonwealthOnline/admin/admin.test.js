const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const source = fs.readFileSync(path.join(__dirname, "admin.js"), "utf8");

class FakeNode {
  constructor(tagName, onFocus) {
    this.tagName = tagName.toUpperCase();
    this.attributes = {};
    this.children = [];
    this.listeners = {};
    this.ownText = "";
    this.value = "";
    this.onFocus = onFocus;
  }

  setAttribute(name, value) {
    this.attributes[name] = String(value);
  }

  getAttribute(name) {
    return Object.prototype.hasOwnProperty.call(this.attributes, name) ? this.attributes[name] : null;
  }

  appendChild(child) {
    this.children.push(child);
    return child;
  }

  get textContent() {
    return this.ownText + this.children.map((child) => child.textContent).join("");
  }

  set textContent(value) {
    this.ownText = String(value);
    this.children = [];
  }

  addEventListener(type, fn) {
    (this.listeners[type] = this.listeners[type] || []).push(fn);
  }

  click() {
    (this.listeners.click || []).forEach((fn) => fn({ target: this, preventDefault() {} }));
  }

  focus() {
    this.onFocus(this);
  }

  blur() {
    this.onFocus(null);
  }

  walk() {
    return [this, ...this.children.flatMap((child) => child.walk())];
  }

  querySelectorAll() {
    return this.walk().filter((n) => (n.tagName === "BUTTON" || n.tagName === "INPUT") && n.getAttribute("disabled") === null);
  }
}

function makeEnv({ keyboard = null } = {}) {
  const sent = [];
  const clock = { now: 0 };
  const documentListeners = {};
  const root = new FakeNode("main");
  root.setAttribute("id", "coAdmin");
  const document = {
    readyState: "complete",
    activeElement: null,
    documentElement: { style: { setProperty() {} } },
    createElement: (tag) => new FakeNode(tag, (node) => {
      document.activeElement = node;
    }),
    getElementById: (id) => (id === "coAdmin" ? root : null),
    addEventListener(type, fn) {
      (documentListeners[type] = documentListeners[type] || []).push(fn);
    }
  };
  const sandbox = {
    document,
    console,
    JSON,
    Date: { now: () => clock.now },
    innerWidth: 1920,
    coAdminAction: (json) => sent.push(JSON.parse(json)),
    CO_Keyboard: keyboard
  };
  sandbox.window = sandbox;
  vm.createContext(sandbox);
  vm.runInContext(source, sandbox);

  const findAll = (predicate) => root.walk().filter(predicate);
  const byKey = (key) => {
    const node = findAll((n) => n.getAttribute("data-key") === key)[0];
    assert.ok(node, `no element keyed ${key}`);
    return node;
  };
  const button = (label) => {
    const node = findAll((n) => n.tagName === "BUTTON" && n.textContent === label)[0];
    assert.ok(node, `no button labelled ${label}`);
    return node;
  };
  return {
    sent,
    clock,
    root,
    document,
    sandbox,
    documentListeners,
    findAll,
    byKey,
    button,
    setState(raw) {
      sandbox.coAdminSetState(typeof raw === "string" ? raw : JSON.stringify(raw));
    },
    click(label) {
      button(label).click();
    },
    clickKey(key) {
      byKey(key).click();
    },
    type(input, value) {
      input.value = value;
      root.listeners.input.forEach((fn) => fn({ target: input }));
    },
    key(name) {
      (documentListeners.keydown || []).forEach((fn) => fn({ key: name, preventDefault() {} }));
    },
    controller(button, phase) {
      sandbox.handleControllerInput(button, phase);
    }
  };
}

const admin = {
  connected: true,
  isAdmin: true,
  serverTime: 1000,
  actions: ["status", "clients", "players", "bans", "kick", "ban", "revive", "kill", "voice_mute", "voice_unmute",
    "world_time", "world_weather", "world_mode", "factions", "faction_disband", "faction_promote", "faction_kick",
    "resources", "ensure", "stop", "refresh", "approve", "unban", "profile_unban", "admin_list", "admin_grant", "admin_revoke", "quest_mode", "map_public", "reports"],
  menus: ["players", "moderation", "world", "factions", "resources", "reports"],
  players: [
    { playerId: 3, profileId: "p3", name: "Nate" },
    { playerId: 5, profileId: "p5", name: "<img src=x onerror=alert(1)>" }
  ],
  data: {
    status: {
      server_name: "Test Server",
      max_players: 16,
      connected_clients: 1,
      pending_connections: 0,
      world_mode: "coop-story",
      friendly_fire: false,
      faction_friendly_fire: true,
      persistence_enabled: true
    },
    clients: {
      clients: [{ player_id: 3, address: "192.0.2.10", port: 7777, steam_id: "76561190000000003" }]
    },
    map_public: { enabled: false },
    factions: {
      factions: [{
        factionId: "f1",
        name: "Minutemen",
        tag: "MMR",
        description: "",
        motd: "",
        color: "",
        createdAt: 1,
        leaderProfileId: "p3",
        members: ["p3", "p5"],
        memberNames: { p3: "Nate", p5: "Piper" },
        officers: [],
        memberCount: 2
      }],
      enabled: true
    },
    resources: [
      { name: "alpha", version: "1.0", state: "running", detail: "", permissions: ["fs.read", "net"], approved: true },
      { name: "beta", version: "0.4", state: "stopped", detail: "", permissions: [], approved: false }
    ],
    bans: {
      bans: [
        { ip: "10.0.0.5", profileId: "p9", reason: "griefing", bannedAt: 1 },
        { ip: "", profileId: "p7", reason: "", bannedAt: 2 }
      ],
      steamBans: [{ steamId: "76561190000000009", reason: "cheating", bannedAt: 3 }]
    },
    admin_list: {
      admins: [
        { profileId: "host1", source: "host" },
        { profileId: "cfg1", source: "config" },
        { profileId: "gr1", source: "granted" }
      ]
    }
  },
  results: [
    { requestId: "r1", action: "kick", ok: false, message: "Player not found", data: null },
    { requestId: "r2", action: "revive", ok: true, message: "Revived", data: null }
  ]
};

{
  const env = makeEnv();
  env.setState({ connected: true, isAdmin: false, actions: [], menus: [] });
  assert.match(env.root.textContent, /Admin access is not available\./);
  assert.equal(env.findAll((n) => n.getAttribute("role") === "tab").length, 0);
  env.click("Close");
  assert.deepEqual(env.sent, [{ op: "close" }]);
}

{
  const env = makeEnv();
  env.setState({ connected: false, isAdmin: false });
  assert.match(env.root.textContent, /Not connected to a server\./);
}

{
  const env = makeEnv();
  env.setState({ connected: true, isAdmin: true, error: "State too large." });
  assert.match(env.root.textContent, /State too large\./);
  assert.doesNotMatch(env.root.textContent, /Not connected/);
  assert.equal(env.findAll((n) => n.getAttribute("role") === "tab").length, 0);
}

{
  const env = makeEnv();
  env.setState(admin);
  assert.deepEqual(env.sent, [], "opening the page sends nothing; the host requests the first tab once");
  env.click("Players");
  assert.deepEqual(env.sent, [{ op: "action", action: "players", params: {} }]);
  assert.equal(env.findAll((n) => n.tagName === "IMG").length, 0, "server names must never become markup");
  assert.match(env.root.textContent, /<img src=x onerror=alert\(1\)>/);
  env.button("Kick");
  env.button("Ban IP");
  env.button("Revive");
  env.button("Kill");
  env.button("Make admin");
  env.button("Mute");
  env.button("Unmute");
}

{
  const env = makeEnv();
  env.setState({ ...admin, actions: admin.actions.filter((action) => action !== "kill") });
  assert.equal(env.findAll((n) => n.tagName === "BUTTON" && n.textContent === "Kill").length, 0);
  assert.equal(env.findAll((n) => n.tagName === "BUTTON" && n.textContent === "Kick").length, 2);
}

{
  const env = makeEnv();
  env.setState(admin);
  env.type(env.byKey("reason"), "griefing");
  env.clickKey("kick-p3");
  assert.equal(env.sent.length, 0, "kick must not send before the confirm step");
  assert.equal(env.byKey("kick-p3").textContent, "Confirm kick Nate (ID 3)", "the confirm step names the target");
  env.clickKey("kick-p3");
  assert.deepEqual(env.sent[0], { op: "action", action: "kick", params: { playerId: 3, reason: "griefing" } });
  assert.equal(env.byKey("reason").value, "", "the reason does not carry over to the next player");
  env.clickKey("kick-p5");
  env.clickKey("kick-p5");
  assert.deepEqual(env.sent[1], { op: "action", action: "kick", params: { playerId: 5 } });
}

{
  const env = makeEnv();
  env.setState(admin);
  env.click("Scripts");
  const reads = () => env.sent.filter((s) => s.action === "resources").length;
  assert.equal(reads(), 1);
  const stopped = { requestId: "r4", action: "stop", ok: true, message: "Stopped", data: null };
  env.setState({ ...admin, results: [...admin.results, stopped] });
  assert.equal(reads(), 2, "a finished change re-reads the open tab");
  env.setState({ ...admin, serverTime: 1001, results: [...admin.results, stopped] });
  assert.equal(reads(), 2, "the same result does not read again");
  env.setState({ ...admin, results: [...admin.results, stopped, { requestId: "r5", action: "stop", ok: false, message: "No", data: null }] });
  assert.equal(reads(), 2, "a failed change does not read again");
  env.setState({ ...admin, results: [{ requestId: "r6", action: "refresh", ok: true, message: "Done", data: null }] });
  assert.equal(reads(), 2, "the rescan button already sends its own read");
}

{
  const env = makeEnv();
  const faction = { ...admin.data.factions.factions[0], officers: ["p5"] };
  env.setState({ ...admin, data: { ...admin.data, factions: { factions: [faction], enabled: true } } });
  env.click("Factions");
  assert.equal(env.byKey("promote:f1:p5").textContent, "Make leader", "an officer can be made leader");
  env.clickKey("promote:f1:p5");
  assert.equal(env.byKey("promote:f1:p5").textContent, "Confirm make Piper leader of Minutemen");
}

{
  const env = makeEnv();
  env.setState(admin);
  env.clickKey("ban-p3");
  assert.equal(env.sent.length, 0, "a ban must confirm first");
  assert.equal(env.byKey("ban-p3").textContent, "Confirm IP ban for Nate (ID 3)", "the confirm step names the target");
  env.clickKey("ban-p3");
  assert.deepEqual(env.sent[0], { op: "action", action: "ban", params: { playerId: 3 } });
}

{
  const env = makeEnv();
  env.setState(admin);
  env.clickKey("mute-p3");
  assert.equal(env.sent.length, 0, "Mute opens the duration choices and sends nothing");
  env.clickKey("mute-p3-1800");
  assert.deepEqual(env.sent[0], { op: "action", action: "voice_mute", params: { playerId: 3, seconds: 1800 } });
  env.clickKey("mute-p5");
  env.clickKey("mute-p5-0");
  assert.deepEqual(env.sent.at(-1), { op: "action", action: "voice_mute", params: { playerId: 5 } }, "No limit omits seconds");
  env.clickKey("mute-p3");
  env.clickKey("mute-cancel-p3");
  assert.equal(env.sent.length, 2, "Cancel closes the choices without sending");
  assert.equal(env.findAll((n) => n.getAttribute("data-key") === "mute-p3-300").length, 0);
}

{
  const env = makeEnv();
  env.setState(admin);
  env.clickKey("revive-p3");
  env.clickKey("revive-p3");
  assert.equal(env.sent.filter((s) => s.action === "revive").length, 1, "a double click sends once");
  env.clock.now = 600;
  env.clickKey("revive-p3");
  assert.equal(env.sent.filter((s) => s.action === "revive").length, 2, "the lockout ends after 600 ms");
}

{
  const env = makeEnv();
  env.setState(admin);
  env.clickKey("kick-p3");
  env.setState({ ...admin, players: [{ playerId: 3, profileId: "p8", name: "Reused" }] });
  assert.equal(env.byKey("kick-p8").textContent, "Kick", "a changed roster disarms the pending confirm");
  env.clickKey("kick-p8");
  assert.equal(env.sent.filter((s) => s.action === "kick").length, 0, "a reused playerId must arm again");
}

{
  const env = makeEnv();
  env.setState(admin);
  env.clickKey("kick-p3");
  env.click("Refresh");
  assert.equal(env.byKey("kick-p3").textContent, "Kick", "Refresh disarms the pending confirm");
}

{
  const env = makeEnv();
  env.setState(admin);
  env.clickKey("kick-p3");
  env.click("Close");
  assert.deepEqual(env.sent.at(-1), { op: "close" });
  env.setState({ ...admin, serverTime: 2000 });
  assert.equal(env.byKey("kick-p3").textContent, "Kick", "Close disarms the pending confirm");
}

{
  const env = makeEnv();
  env.setState(admin);
  env.clickKey("kick-p3");
  env.sandbox.coAdminReset();
  assert.equal(env.byKey("kick-p3").textContent, "Kick", "the host reset disarms the pending confirm");
}

{
  const env = makeEnv();
  env.setState(admin);
  env.click("World");
  const before = env.sent.length;
  env.click("Set weather");
  assert.equal(env.sent.length, before, "an empty weather form ID must not send");
  assert.match(env.root.textContent, /Enter an 8-digit hex weather form ID, or auto to clear the lock\./);
  env.sandbox.coAdminReset();
  assert.doesNotMatch(env.root.textContent, /Enter an 8-digit hex weather form ID, or auto to clear the lock\./, "the host reset clears the notice");
}

{
  const env = makeEnv();
  env.setState(admin);
  env.click("Bans & admins");
  env.sandbox.coAdminReset();
  assert.match(env.root.textContent, /Nate/, "the host reset returns the page to the Players tab");
  assert.doesNotMatch(env.root.textContent, /Connected clients/);
}

{
  const env = makeEnv();
  env.setState(admin);
  env.click("Bans & admins");
  assert.deepEqual(env.sent, [
    { op: "action", action: "bans", params: {} },
    { op: "action", action: "admin_list", params: {} },
    { op: "action", action: "clients", params: {} }
  ]);
  assert.match(env.root.textContent, /10\.0\.0\.5/);
  assert.match(env.root.textContent, /Profile p9 · Reason: griefing/);
  assert.match(env.root.textContent, /Profile p7/);
  assert.equal(env.findAll((n) => n.tagName === "BUTTON" && n.textContent === "Unban").length, 2, "every ban gets an Unban");
  assert.match(env.root.textContent, /Steam ID 76561190000000009/);
  assert.match(env.root.textContent, /Steam bans can only be lifted from the server console\./);
  assert.match(env.root.textContent, /Player 3/);
  assert.match(env.root.textContent, /192\.0\.2\.10:7777/);
}

{
  const env = makeEnv();
  env.setState(admin);
  env.click("Bans & admins");
  env.clickKey("unban:10.0.0.5");
  assert.deepEqual(env.sent.at(-1), { op: "action", action: "unban", params: { ip: "10.0.0.5" } });
  env.clickKey("unban-profile:p7");
  assert.deepEqual(env.sent.at(-1), { op: "action", action: "profile_unban", params: { profileId: "p7" } });
}

{
  const env = makeEnv();
  env.setState(admin);
  env.click("Bans & admins");
  assert.match(env.root.textContent, /host1/);
  assert.match(env.root.textContent, /cfg1/);
  assert.equal(env.findAll((n) => n.tagName === "BUTTON" && n.textContent === "Revoke").length, 1, "only granted admins get Revoke");
  const sentBefore = env.sent.length;
  env.clickKey("revoke:gr1");
  assert.equal(env.sent.length, sentBefore, "revoke must be armed first");
  assert.equal(env.byKey("revoke:gr1").textContent, "Confirm revoke admin gr1");
  env.clickKey("revoke:gr1");
  assert.deepEqual(env.sent.at(-1), { op: "action", action: "admin_revoke", params: { profileId: "gr1" } });
}

{
  const env = makeEnv();
  env.setState(admin);
  env.clickKey("admin-p3");
  env.clickKey("admin-p3");
  assert.deepEqual(env.sent.at(-1), { op: "action", action: "admin_grant", params: { playerId: 3 } });
}

{
  const env = makeEnv();
  env.setState(admin);
  for (const label of ["Players", "Bans & admins", "World", "Factions", "Scripts", "Reports"]) {
    env.click(label);
    assert.doesNotMatch(env.root.textContent, /[{}]|"[a-zA-Z_]+":/, `${label} shows no raw JSON`);
  }
}

{
  const env = makeEnv();
  env.setState({ ...admin, actions: admin.actions.filter((action) => !["unban", "profile_unban", "quest_mode"].includes(action)) });
  env.click("Bans & admins");
  assert.equal(env.findAll((n) => n.tagName === "BUTTON" && n.textContent === "Unban").length, 0);
  env.click("World");
  assert.equal(env.findAll((n) => (n.getAttribute("data-key") || "").startsWith("quest-")).length, 0);
}

{
  const env = makeEnv();
  env.setState({ ...admin, data: { ...admin.data, bans: { truncated: true, bytes: 123 } } });
  env.click("Bans & admins");
  assert.match(env.root.textContent, /Too large to display \(123 bytes\)\./);
  assert.doesNotMatch(env.root.textContent, /No bans\./);
}

{
  const env = makeEnv();
  env.setState({ ...admin, data: { ...admin.data, factions: { truncated: true, bytes: 456 } } });
  env.click("Factions");
  assert.match(env.root.textContent, /Too large to display \(456 bytes\)\./);
  assert.doesNotMatch(env.root.textContent, /No factions\./);
}

{
  const env = makeEnv();
  env.setState({ ...admin, data: { ...admin.data, resources: { truncated: true, bytes: 789 } } });
  env.click("Scripts");
  assert.match(env.root.textContent, /Too large to display \(789 bytes\)\./);
  assert.doesNotMatch(env.root.textContent, /No scripts\./);
}

{
  const env = makeEnv();
  env.setState(admin);
  env.click("World");
  assert.deepEqual(env.sent, [
    { op: "action", action: "status", params: {} },
    { op: "action", action: "map_public", params: {} }
  ]);
  assert.match(env.root.textContent, /Test Server/);
  assert.match(env.root.textContent, /1 of 16/);
  assert.match(env.root.textContent, /Co-op story/);
  assert.doesNotMatch(env.root.textContent, /[{}]|"[a-zA-Z_]+":/, "the World tab shows no raw JSON");
  env.clickKey("time-1200");
  assert.deepEqual(env.sent.at(-1), { op: "action", action: "world_time", params: { hhmm: "1200" } });
  env.type(env.byKey("weather"), "00000001");
  env.click("Set weather");
  assert.deepEqual(env.sent.at(-1), { op: "action", action: "world_weather", params: { weather: "00000001" } });
  env.clickKey("mode-free-roam");
  assert.equal(env.sent.filter((s) => s.action === "world_mode").length, 0, "a world mode change must confirm first");
  assert.equal(env.byKey("mode-free-roam").textContent, "Confirm set world mode to Free roam");
  env.clickKey("mode-free-roam");
  assert.deepEqual(env.sent.at(-1), { op: "action", action: "world_mode", params: { mode: "free-roam" } });
  env.clickKey("quest-faction-war");
  assert.deepEqual(env.sent.at(-1), { op: "action", action: "quest_mode", params: { mode: "faction-war" } });
  env.clickKey("quest-current");
  assert.deepEqual(env.sent.at(-1), { op: "action", action: "quest_mode", params: {} });
  env.click("Show everyone");
  assert.deepEqual(env.sent.slice(-2), [
    { op: "action", action: "map_public", params: { enabled: true } },
    { op: "action", action: "map_public", params: {} }
  ]);
}

{
  const env = makeEnv();
  env.setState({ ...admin, data: { ...admin.data, map_public: { enabled: true } } });
  env.click("World");
  assert.match(env.root.textContent, /Everyone can see every player on the map\./);
  env.click("Limit to own faction");
  assert.deepEqual(env.sent.slice(-2), [
    { op: "action", action: "map_public", params: { enabled: false } },
    { op: "action", action: "map_public", params: {} }
  ]);
}

{
  const env = makeEnv();
  env.setState(admin);
  env.click("Factions");
  assert.match(env.root.textContent, /Piper/, "string members resolve their name from memberNames");
  assert.match(env.root.textContent, /Leader/, "the leader is marked from leaderProfileId");
  assert.equal(env.findAll((n) => n.getAttribute("data-key") === "promote:f1:p3").length, 0, "the leader has no make-leader button");
  env.clickKey("disband:f1");
  assert.equal(env.sent.filter((s) => s.action === "faction_disband").length, 0);
  env.clickKey("disband:f1");
  assert.deepEqual(env.sent.at(-1), { op: "action", action: "faction_disband", params: { factionId: "f1" } });
  env.clickKey("promote:f1:p5");
  assert.equal(env.sent.filter((s) => s.action === "faction_promote").length, 0, "promote must confirm first");
  env.clickKey("promote:f1:p5");
  assert.deepEqual(env.sent.at(-1), { op: "action", action: "faction_promote", params: { factionId: "f1", profileId: "p5" } });
  env.clickKey("faction-kick:f1:p5");
  env.clickKey("faction-kick:f1:p5");
  assert.deepEqual(env.sent.at(-1), { op: "action", action: "faction_kick", params: { factionId: "f1", profileId: "p5" } });
}

{
  const env = makeEnv();
  env.setState(admin);
  env.click("Scripts");
  env.clickKey("ensure:alpha");
  assert.deepEqual(env.sent.at(-1), { op: "action", action: "ensure", params: { name: "alpha" } });
  env.clickKey("stop:alpha");
  assert.equal(env.sent.filter((s) => s.action === "stop").length, 0, "stop must confirm first");
  assert.equal(env.byKey("stop:alpha").textContent, "Confirm stop alpha");
  env.clickKey("stop:alpha");
  assert.deepEqual(env.sent.at(-1), { op: "action", action: "stop", params: { name: "alpha" } });
  env.clickKey("approve:alpha");
  assert.equal(env.sent.filter((s) => s.action === "approve").length, 0, "approve must confirm first");
  env.clickKey("approve:alpha");
  assert.deepEqual(env.sent.at(-1), { op: "action", action: "approve", params: { name: "alpha" } });
  env.click("Rescan scripts");
  assert.deepEqual(env.sent.slice(-2), [
    { op: "action", action: "refresh", params: {} },
    { op: "action", action: "resources", params: {} }
  ]);
}

{
  const env = makeEnv();
  env.setState(admin);
  env.click("Scripts");
  assert.match(env.root.textContent, /running 1\.0 · approved/);
  assert.match(env.root.textContent, /Permissions: fs\.read, net/);
  assert.match(env.root.textContent, /stopped 0\.4 · not approved/);
  assert.match(env.root.textContent, /Permissions: none/);
}

{
  const env = makeEnv();
  env.setState(admin);
  const failed = env.findAll((n) => n.getAttribute("class") === "admin-result is-fail");
  const ok = env.findAll((n) => n.getAttribute("class") === "admin-result is-ok");
  assert.equal(failed.length, 1);
  assert.equal(ok.length, 1);
  assert.equal(failed[0].textContent, "Kick: Player not found");
  assert.equal(ok[0].textContent, "Revived");
}

{
  const env = makeEnv();
  env.setState({ ...admin, results: [{ requestId: "r3", action: "world_mode", ok: true, message: "World mode set to free-roam.", data: null }] });
  assert.equal(env.findAll((n) => n.getAttribute("class") === "admin-result is-ok")[0].textContent, "World mode set to free-roam.", "a message that already names the action is not prefixed twice");
}

{
  const env = makeEnv();
  env.setState(admin);
  env.byKey("kick-p3").focus();
  env.clickKey("kick-p3");
  assert.equal(env.document.activeElement.getAttribute("data-key"), "kick-p3", "focus survives the re-render");
}

{
  const env = makeEnv();
  env.setState(admin);
  env.key("ArrowDown");
  assert.equal(env.document.activeElement.tagName, "BUTTON");
  const first = env.document.activeElement;
  env.key("ArrowDown");
  assert.notEqual(env.document.activeElement, first);
  env.key("Escape");
  assert.deepEqual(env.sent.at(-1), { op: "close" });
}

{
  const env = makeEnv();
  env.setState(admin);
  env.controller("b", "up");
  assert.equal(env.sent.filter((s) => s.op === "close").length, 0);
  env.controller("rb", "down");
  assert.deepEqual(env.sent.at(-1), { op: "action", action: "clients", params: {} });
  env.controller("b", "down");
  assert.deepEqual(env.sent.at(-1), { op: "close" });
}

{
  const opened = [];
  const handled = [];
  let keyboardOpen = false;
  const keyboard = {
    isOpen: () => keyboardOpen,
    open(element) {
      opened.push(element);
      keyboardOpen = true;
      return true;
    },
    render() {},
    handleButton(button) {
      handled.push(button);
    }
  };
  const env = makeEnv({ keyboard });
  env.setState(admin);
  env.byKey("reason").listeners.click[0]({ target: env.byKey("reason") });
  assert.equal(opened[0], env.byKey("reason"));
  env.key("Escape");
  assert.deepEqual(handled, ["b"]);
  assert.equal(env.sent.filter((s) => s.op === "close").length, 0, "Escape closes the keyboard, not the window");
}

{
  const env = makeEnv();
  env.setState(admin);
  env.setState("{not json");
  assert.match(env.root.textContent, /Players/);
}

{
  const env = makeEnv();
  env.setState(admin);
  env.byKey("body").scrollTop = 120;
  env.setState({ ...admin, serverTime: 3000 });
  assert.equal(env.byKey("body").scrollTop, 120, "a state push keeps the scroll position");
}

{
  const env = makeEnv();
  env.setState(admin);
  env.byKey("body").scrollTop = 300;
  env.byKey("kick-p3").focus();
  env.sandbox.coAdminReset();
  assert.equal(env.byKey("body").scrollTop, 0, "reopening starts at the top");
  assert.equal(env.document.activeElement, null, "reopening does not refocus a row that may be out of view");
  env.setState(admin);
  assert.equal(env.byKey("body").scrollTop, 0, "the first state after reopening stays at the top");
}

{
  const css = fs.readFileSync(path.join(__dirname, "styles.css"), "utf8");
  const bodiesOf = (selector) => [...css.matchAll(/([^{}]+)\{([^{}]*)\}/g)]
    .filter((match) => match[1].split(",").map((part) => part.trim()).includes(selector))
    .map((match) => match[2]);
  assert.ok(bodiesOf(".admin-body").some((body) => /min-height\s*:\s*0\b/.test(body)), "the body is the scroller that may shrink");
  assert.ok(bodiesOf(".admin-tabpage").length > 0, "the tab page has a layout rule");
  assert.ok(bodiesOf(".admin-tabpage").every((body) => !/min-height\s*:\s*0\b/.test(body)), "the tab page keeps its content height so the body scrolls instead of overlapping");
}

{
  const env = makeEnv();
  const reports = {
    reports: [
      { reportId: "rp-1", kind: "player", reason: "cheating", reporterPlayerId: 3, reporterName: "Nate", targetPlayerId: 5, targetName: "<img src=x onerror=alert(1)>", note: "speed hack", serverTime: 940 },
      { reportId: "rp-2", kind: "support", reporterPlayerId: 5, reporterName: "Piper", note: "", serverTime: 990 }
    ],
    total: 2
  };
  env.setState({ ...admin, data: { ...admin.data, reports } });
  env.click("Reports");
  assert.deepEqual(env.sent, [{ op: "action", action: "reports", params: {} }], "opening the Reports tab reads the list once");
  assert.equal(env.findAll((n) => n.tagName === "IMG").length, 0, "reporter and target names must never become markup");
  assert.match(env.root.textContent, /Showing the latest 2 of 2 reports\. Read-only\./);
  assert.match(env.root.textContent, /Cheating/);
  assert.match(env.root.textContent, /Support request/);
  assert.match(env.root.textContent, /From #3 Nate/);
  assert.match(env.root.textContent, /About #5 <img src=x onerror=alert\(1\)>/);
  assert.match(env.root.textContent, /speed hack/);
  assert.match(env.root.textContent, /\(no message\)/);
  assert.match(env.root.textContent, /1 min ago/);
  assert.match(env.root.textContent, /under a minute ago/);
  assert.ok(env.root.textContent.indexOf("Piper") < env.root.textContent.indexOf("Nate"), "the newest report is listed first");
  assert.deepEqual(
    env.findAll((n) => n.getAttribute("class") === "admin-btn").map((n) => n.textContent),
    ["Close", "Refresh"],
    "the Reports tab has no action buttons besides Close and Refresh"
  );
  env.click("Refresh");
  assert.deepEqual(env.sent.at(-1), { op: "action", action: "reports", params: {} }, "Refresh re-reads the list");
}

{
  const env = makeEnv();
  env.setState({ ...admin, data: { ...admin.data, reports: { reports: [], total: 0 } } });
  env.click("Reports");
  assert.match(env.root.textContent, /No player reports yet\./);
  assert.doesNotMatch(env.root.textContent, /Loading\.\.\./);
}

{
  const env = makeEnv();
  env.setState({ ...admin, data: { ...admin.data, reports: { truncated: true, bytes: 999 } } });
  env.click("Reports");
  assert.match(env.root.textContent, /Too large to display \(999 bytes\)\./);
}

{
  const env = makeEnv();
  env.setState({ ...admin, data: { ...admin.data, reports: null } });
  env.click("Reports");
  assert.match(env.root.textContent, /Loading\.\.\./);
}

{
  const env = makeEnv();
  env.setState({ ...admin, menus: ["players", "moderation", "world", "factions", "resources"] });
  assert.equal(env.findAll((n) => n.getAttribute("role") === "tab" && n.textContent === "Reports").length, 0, "no Reports tab unless the server lists it");
}

assert.doesNotMatch(source, /innerHTML/, "admin.js must not insert markup from the server");
console.log("admin.test.js: all checks passed");
