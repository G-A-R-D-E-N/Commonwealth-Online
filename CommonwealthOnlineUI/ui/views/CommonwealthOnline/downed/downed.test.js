const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const DOWNED = fs.readFileSync(__dirname + "/downed.js", "utf8");
const RESPAWN = fs.readFileSync(__dirname + "/respawn.js", "utf8");

function createHarness(source, rootId) {
  const timers = [];
  const listeners = {};
  const live = {
    clock: { textContent: "" },
    unit: { textContent: "" },
    fill: { style: { width: "" } }
  };
  const root = {
    innerHTML: "",
    className: "",
    addEventListener(type, handler) {
      (listeners[type] ||= []).push(handler);
    },
    querySelector(selector) {
      if (selector === "[data-clock]") {
        return live.clock;
      }
      if (selector === "[data-countdown]") {
        return live.fill;
      }
      if (selector === "[data-unit]") {
        return live.unit;
      }
      return null;
    },
    querySelectorAll() {
      return [];
    }
  };
  const document = {
    readyState: "complete",
    activeElement: null,
    documentElement: { style: { setProperty() {} } },
    getElementById(id) {
      return id === rootId ? root : null;
    },
    addEventListener(type, handler) {
      (listeners[type] ||= []).push(handler);
    }
  };
  const window = {
    innerWidth: 1920,
    addEventListener(type, handler) {
      (listeners[type] ||= []).push(handler);
    }
  };

  vm.runInNewContext(source, {
    document,
    window,
    setTimeout(handler) {
      timers.push(handler);
      return timers.length;
    },
    clearTimeout(id) {
      timers[id - 1] = null;
    },
    setInterval(handler) {
      timers.push(handler);
      return timers.length;
    },
    clearInterval(id) {
      timers[id - 1] = null;
    },
    Date,
    Math,
    JSON,
    String,
    Number,
    Array,
    Object,
    isFinite,
    console
  });

  return { window, root, document, listeners, timers, live };
}

function same(actual, expected) {
  assert.equal(JSON.stringify(actual), JSON.stringify(expected));
}

function button(point, friendId, disabled) {
  return {
    tagName: "BUTTON",
    disabled: disabled === true,
    getAttribute(name) {
      if (name === "data-point") {
        return point;
      }
      if (name === "data-friend-id") {
        return friendId === undefined ? null : friendId;
      }
      return null;
    }
  };
}

function click(root, listeners, element) {
  listeners.click.forEach(handler => handler({ target: { closest: () => element } }));
}

function TestOverlayClock() {
  const harness = createHarness(DOWNED, "coDowned");
  const model = harness.window.coDownedModel;

  assert.equal(model.formatSeconds(0), "0");
  assert.equal(model.formatSeconds(250), "1");
  assert.equal(model.formatSeconds(1000), "1");
  assert.equal(model.formatSeconds(59000), "59");
  assert.equal(model.formatSeconds(59999), "60");
  assert.equal(model.formatSeconds(60000), "60");
  assert.equal(model.formatSeconds(-4000), "0");
  assert.equal(model.formatSeconds(600000), "600");
  assert.equal(model.formatSeconds("nonsense"), "0");
  assert.equal(model.unitFor(1000), "SECOND", "one second left reads as a singular");
  assert.equal(model.unitFor(250), "SECOND", "a partial second still rounds up to one");
  assert.equal(model.unitFor(2000), "SECONDS");
  assert.equal(model.unitFor(0), "SECONDS");
  assert.equal(model.maxMs, 600000);
}

function TestOverlayNormalize() {
  const harness = createHarness(DOWNED, "coDowned");
  const normalize = harness.window.coDownedModel.normalize;

  assert.equal(normalize(null), null);
  assert.equal(normalize(7), null);
  assert.equal(normalize([]), null);
  assert.equal(normalize("{"), null);

  same(normalize("{}"), { phase: "hidden", remainingMs: 0, rescuer: null, reviving: null, prompt: null });
  same(normalize('{"phase":"downed","remainingMs":4200}'), {
    phase: "downed",
    remainingMs: 4200,
    rescuer: null,
    reviving: null,
    prompt: null
  });

  assert.equal(normalize({ phase: "dead" }).phase, "dead");
  assert.equal(normalize({ phase: "DOWNED" }).phase, "hidden");
  assert.equal(normalize({ remainingMs: -10 }).remainingMs, 0);
  assert.equal(normalize({ remainingMs: 90000 }).remainingMs, 90000);
  assert.equal(normalize({ remainingMs: 120000 }).remainingMs, 120000);
  assert.equal(normalize({ remainingMs: 700000 }).remainingMs, 600000);
  assert.equal(normalize({ remainingMs: "5000" }).remainingMs, 5000);
  assert.equal(normalize({ remainingMs: "nonsense" }).remainingMs, 0);

  assert.equal(normalize({ rescuer: "Bo" }).rescuer, null);
  assert.equal(normalize({ rescuer: [] }).rescuer, null);
  same(normalize({ rescuer: {} }).rescuer, { name: "", progress: 0 });
  same(normalize({ rescuer: { name: "Bo", progress: 0.5 } }).rescuer, { name: "Bo", progress: 0.5 });
  assert.equal(normalize({ rescuer: { progress: -3 } }).rescuer.progress, 0);
  assert.equal(normalize({ rescuer: { progress: 7 } }).rescuer.progress, 1);
  assert.equal(normalize({ rescuer: { progress: "half" } }).rescuer.progress, 0);

  assert.equal(normalize({ prompt: "Bo" }).prompt, null);
  assert.equal(normalize({ prompt: [] }).prompt, null);
  same(normalize({ prompt: {} }).prompt, { name: "", progress: 0 });
  same(normalize({ prompt: { name: "Cait", progress: 0.4 } }).prompt, { name: "Cait", progress: 0.4 });
  assert.equal(normalize({ prompt: { progress: "half" } }).prompt.progress, 0);
  assert.equal(normalize({ prompt: { progress: 9 } }).prompt.progress, 1);
}

function TestOverlayRender() {
  const harness = createHarness(DOWNED, "coDowned");
  const model = harness.window.coDownedModel;

  assert.equal(model.render({ phase: "hidden", rescuer: null, reviving: null }, 0), "");

  const downed = model.render({ phase: "downed", rescuer: null, reviving: null }, 42000);
  assert.ok(downed.includes("YOU ARE UNCONSCIOUS FOR"));
  assert.ok(downed.includes(">42<"));
  assert.ok(downed.includes("SECONDS"));
  assert.ok(downed.includes("USE THE STIMPAK BUTTON TO GET UP"));
  assert.ok(!downed.includes("REVIVING YOU"));

  const oneSecond = model.render({ phase: "downed", rescuer: null, reviving: null }, 1000);
  assert.ok(oneSecond.includes(">1<") && oneSecond.includes("SECOND</span>"),
    "the last second must read as one second, not one seconds");
  assert.ok(!oneSecond.includes("SECONDS"));

  const rescued = model.render(
    { phase: "downed", rescuer: { name: "Bo", progress: 0.25 }, reviving: null },
    42000
  );
  assert.ok(rescued.includes("A FRIEND IS REVIVING YOU"));
  assert.ok(rescued.includes("width:25%"));
  assert.ok(!rescued.includes("USE THE STIMPAK BUTTON TO GET UP"),
    "the stimpak line gives way to the reviver line while someone is working");

  const reviving = model.render({ phase: "hidden", rescuer: null, reviving: { name: "Cait", progress: 0.75 } }, 0);
  assert.ok(reviving.includes("REVIVING"));
  assert.ok(reviving.includes("Cait"));
  assert.ok(reviving.includes("width:75%"));

  const fallback = model.render({ phase: "hidden", rescuer: null, reviving: { name: "", progress: 0 } }, 0);
  assert.ok(fallback.includes("A FRIEND"));

  const escaped = model.render({ phase: "hidden", rescuer: null, reviving: { name: "<img>", progress: 0 } }, 0);
  assert.ok(escaped.includes("&lt;img&gt;"));
  assert.ok(!escaped.includes("<img>"));
}

function TestOverlayPromptRender() {
  const harness = createHarness(DOWNED, "coDowned");
  const model = harness.window.coDownedModel;

  assert.equal(model.render({ phase: "hidden", prompt: null, reviving: null }, 0), "");

  const prompt = model.render({ phase: "hidden", rescuer: null, reviving: null, prompt: { name: "Cait", progress: 0 } }, 0);
  assert.ok(prompt.includes("co-downed-prompt"));
  assert.ok(prompt.includes("HOLD"));
  assert.ok(prompt.includes("<kbd>ACTIVATE</kbd>"));
  assert.ok(prompt.includes("TO REVIVE"));
  assert.ok(prompt.includes("Cait"));
  assert.ok(prompt.includes('aria-live="polite"'));
  assert.ok(prompt.includes("width:0%"));
  assert.ok(!prompt.includes("co-downed-card"), "the prompt is not the full-screen downed card");

  const holding = model.render({ phase: "hidden", prompt: { name: "Cait", progress: 0.5 } }, 0);
  assert.ok(holding.includes("width:50%"));

  const unnamed = model.render({ phase: "hidden", prompt: { name: "", progress: 0 } }, 0);
  assert.ok(unnamed.includes("A FRIEND"));

  const escaped = model.render({ phase: "hidden", prompt: { name: "<img>", progress: 0 } }, 0);
  assert.ok(escaped.includes("&lt;img&gt;"));
  assert.ok(!escaped.includes("<img>"));

  const channelWins = model.render(
    { phase: "hidden", prompt: { name: "Cait", progress: 0 }, reviving: { name: "Cait", progress: 0.2 } },
    0
  );
  assert.ok(channelWins.includes("REVIVING"));
  assert.ok(!channelWins.includes("co-downed-prompt"));

  const downedWins = model.render(
    { phase: "downed", remainingMs: 42000, rescuer: null, reviving: null, prompt: { name: "Cait", progress: 0 } },
    42000
  );
  assert.ok(downedWins.includes("YOU ARE UNCONSCIOUS FOR"));
  assert.ok(!downedWins.includes("co-downed-prompt"));
}

function TestOverlayTicks() {
  const harness = createHarness(DOWNED, "coDowned");
  const model = harness.window.coDownedModel;

  assert.equal(harness.window.coDownedSetState({ phase: "downed", remainingMs: 1000 }), true);
  assert.equal(harness.timers.length, 1);
  assert.equal(harness.live.clock.textContent, "1");
  assert.equal(harness.live.unit.textContent, "SECOND");
  assert.equal(harness.live.fill.style.width, "100%");

  harness.timers[0]();
  assert.equal(harness.live.clock.textContent, "1");
  assert.equal(harness.live.fill.style.width, "75%");

  harness.timers[0]();
  harness.timers[0]();
  harness.timers[0]();
  assert.equal(harness.live.clock.textContent, "0");
  assert.equal(harness.live.unit.textContent, "SECONDS");
  assert.equal(harness.live.fill.style.width, "0%");

  harness.window.coDownedSetState({ phase: "downed", remainingMs: 300000 });
  assert.equal(harness.live.fill.style.width, "100%");
  assert.equal(harness.live.clock.textContent, "300");

  harness.timers[0]();
  assert.equal(harness.live.clock.textContent, "300");

  assert.equal(harness.window.coDownedSetState("not json"), false);
  assert.equal(harness.window.coDownedSetState({ phase: "hidden" }), true);
  assert.ok(harness.root.innerHTML === "");
}

function TestOverlayBarUsesPeak() {
  const harness = createHarness(DOWNED, "coDowned");
  const set = state => harness.window.coDownedSetState(state);

  set({ phase: "downed", remainingMs: 60000 });
  assert.equal(harness.live.fill.style.width, "100%");
  set({ phase: "downed", remainingMs: 30000 });
  assert.equal(harness.live.fill.style.width, "50%");
  set({ phase: "downed", remainingMs: 15000 });
  assert.equal(harness.live.fill.style.width, "25%");

  set({ phase: "hidden", remainingMs: 0 });
  set({ phase: "downed", remainingMs: 20000 });
  assert.equal(harness.live.fill.style.width, "100%");
  set({ phase: "downed", remainingMs: 10000 });
  assert.equal(harness.live.fill.style.width, "50%");

  set({ phase: "downed", remainingMs: 40000 });
  assert.equal(harness.live.fill.style.width, "100%");

  const fresh = createHarness(DOWNED, "coDowned").window.coDownedModel;
  assert.ok(fresh.render({ phase: "downed", rescuer: null, reviving: null }, 300000).includes("width:50%"));
}

function TestRespawnNormalize() {
  const harness = createHarness(RESPAWN, "coRespawn");
  const normalize = harness.window.coRespawnModel.normalize;

  assert.equal(normalize(null), null);
  assert.equal(normalize("nope"), null);
  same(normalize("{}"), { phase: "hidden", stimpaks: 0, stimpakCost: 1, points: [], friends: [] });

  const state = normalize({
    phase: "dead",
    points: [
      { id: "sanctuary", label: "Sanctuary Hills", available: true },
      { id: "lastbed", label: "Last bed", available: false },
      { id: "nowhere", label: "Nope", available: true },
      { id: "sanctuary", label: "No flag" },
      "string",
      null
    ],
    friends: [
      { id: "profile-1", name: "Bo" },
      { id: "", name: "No id" },
      { id: "profile-2" },
      null
    ]
  });

  assert.equal(state.phase, "dead");
  same(state.points, [
    { id: "sanctuary", label: "Sanctuary Hills", available: true },
    { id: "lastbed", label: "Last bed", available: false },
    { id: "sanctuary", label: "No flag", available: false }
  ]);
  same(state.friends, [{ id: "profile-1", name: "Bo" }, { id: "profile-2", name: "" }]);

  const lastSave = normalize({
    phase: "dead",
    points: [{ id: "checkpoint", label: "Last save", available: true }]
  });
  same(lastSave.points, [{ id: "checkpoint", label: "Last save", available: true }]);

  const notDead = normalize({ phase: "downed", points: [{ id: "sanctuary" }] });
  assert.equal(notDead.phase, "downed");
  assert.equal(notDead.points.length, 1);

  assert.equal(normalize({ stimpaks: 3 }).stimpaks, 3);
  assert.equal(normalize({ stimpaks: 3.9 }).stimpaks, 3);
  assert.equal(normalize({ stimpaks: -4 }).stimpaks, 0);
  assert.equal(normalize({ stimpaks: 500 }).stimpaks, 99);
  assert.equal(normalize({ stimpaks: "x" }).stimpaks, 0);
  assert.equal(normalize({}).stimpaks, 0);

  const capped = normalize({
    points: Array.from({ length: 20 }, () => ({ id: "sanctuary", label: "L" })),
    friends: Array.from({ length: 40 }, (value, index) => ({ id: "p" + index, name: "N" }))
  });
  assert.equal(capped.points.length, 8);
  assert.equal(capped.friends.length, 32);

  const truncated = normalize({
    points: [{ id: "sanctuary", label: "x".repeat(120) }],
    friends: [{ id: "y".repeat(120), name: "z".repeat(120) }]
  });
  assert.equal(truncated.points[0].label.length, 48);
  assert.equal(truncated.friends[0].id.length, 64);
  assert.equal(truncated.friends[0].name.length, 32);
}

function TestRespawnRender() {
  const harness = createHarness(RESPAWN, "coRespawn");
  const model = harness.window.coRespawnModel;

  assert.equal(model.render({ phase: "hidden", points: [], friends: [] }), "");

  const html = model.render({
    phase: "dead",
    points: [
      { id: "sanctuary", label: "Sanctuary Hills", available: true },
      { id: "lastbed", label: "", available: false }
    ],
    friends: [{ id: "profile-1", name: "Cait" }]
  });

  assert.ok(html.includes("YOU DIED"));
  assert.ok(html.includes('data-point="sanctuary"'));
  assert.ok(html.includes(">Sanctuary Hills</button>"));
  assert.ok(html.includes('data-point="lastbed" disabled'));
  assert.ok(html.includes(">lastbed</button>"));
  assert.ok(html.includes('data-friend-id="profile-1"'));
  assert.ok(html.includes("Respawn next to Cait"));

  const empty = model.render({ phase: "dead", points: [], friends: [] });
  assert.ok(empty.includes("No respawn points are available."));
  assert.ok(empty.includes("No friends are online."));

  const fallback = model.render({ phase: "dead", points: [], friends: [{ id: "p", name: "" }] });
  assert.ok(fallback.includes("Respawn next to a friend"));

  const escaped = model.render({ phase: "dead", points: [], friends: [{ id: 'a"b', name: "<b>" }] });
  assert.ok(escaped.includes("&lt;b&gt;"));
  assert.ok(escaped.includes("a&quot;b"));
}

function TestRespawnChoice() {
  const harness = createHarness(RESPAWN, "coRespawn");
  const sent = [];
  harness.window.coDownedAction = payload => sent.push(payload);

  assert.equal(harness.window.coRespawnSetState({ phase: "dead", points: [], friends: [] }), true);

  click(harness.root, harness.listeners, button("sanctuary", undefined, false));
  assert.equal(sent.length, 1);
  same(JSON.parse(sent[0]), { action: "respawn", point: "sanctuary", friendId: "" });

  click(harness.root, harness.listeners, button("lastbed", undefined, false));
  assert.equal(sent.length, 1);

  harness.window.coRespawnSetState({ phase: "dead", points: [{ id: "sanctuary", label: "S", available: true }], friends: [] });
  click(harness.root, harness.listeners, button("friend", "profile-9", false));
  assert.equal(sent.length, 2);
  same(JSON.parse(sent[1]), { action: "respawn", point: "friend", friendId: "profile-9" });

  click(harness.root, harness.listeners, button("lastbed", undefined, true));
  assert.equal(sent.length, 2);

  click(harness.root, harness.listeners, { tagName: "DIV", disabled: false, getAttribute: () => null });
  assert.equal(sent.length, 2);
}

function TestRespawnPendingRelease() {
  const harness = createHarness(RESPAWN, "coRespawn");
  const sent = [];
  harness.window.coDownedAction = payload => sent.push(payload);

  const dead = { phase: "dead", points: [{ id: "sanctuary", label: "S", available: true }], friends: [] };
  harness.window.coRespawnSetState(dead);

  click(harness.root, harness.listeners, button("sanctuary", undefined, false));
  assert.equal(sent.length, 1);
  assert.ok(harness.root.innerHTML.indexOf("Respawning") >= 0);
  assert.equal(harness.timers.length, 1);

  harness.window.coRespawnSetState(dead);
  assert.equal(harness.root.innerHTML.indexOf("Respawning"), -1);
  assert.equal(harness.root.innerHTML.indexOf("Choose where to respawn.") >= 0, true);

  click(harness.root, harness.listeners, button("sanctuary", undefined, false));
  assert.equal(sent.length, 2);
  assert.ok(harness.root.innerHTML.indexOf("Respawning") >= 0);

  harness.timers[1]();
  assert.equal(harness.root.innerHTML.indexOf("Respawning"), -1);

  click(harness.root, harness.listeners, button("sanctuary", undefined, false));
  assert.equal(sent.length, 3);
}

function TestRespawnAcceptsPushedPayload() {
  const harness = createHarness(RESPAWN, "coRespawn");
  const entry = harness.window.coRespawnSetState;
  assert.equal(typeof entry, "function");

  assert.equal(
    entry({
      phase: "dead",
      remainingMs: 120000,
      rescuer: null,
      reviving: null,
      points: [{ id: "sanctuary", label: "Sanctuary Hills", available: true }],
      friends: [{ id: "profile-1", name: "Cait" }]
    }),
    true
  );
  assert.ok(harness.root.innerHTML.indexOf("YOU DIED") >= 0);
  assert.ok(harness.root.innerHTML.indexOf("Sanctuary Hills") >= 0);
  assert.ok(harness.root.innerHTML.indexOf("Cait") >= 0);

  assert.equal(entry({ phase: "hidden", remainingMs: 0, points: [], friends: [] }), true);
  assert.equal(harness.root.innerHTML, "");
}

function TestRespawnInput() {
  const harness = createHarness(RESPAWN, "coRespawn");
  const model = harness.window.coRespawnModel;
  const sent = [];
  harness.window.coDownedAction = payload => sent.push(payload);

  assert.equal(harness.listeners.keydown.length, 1);
  assert.equal(harness.listeners["prisma-controller-action"].length, 1);

  const preventions = [];
  harness.listeners.keydown.forEach(handler =>
    handler({ key: "ArrowDown", preventDefault: () => preventions.push("down") })
  );
  harness.listeners.keydown.forEach(handler =>
    handler({ key: "Enter", preventDefault: () => preventions.push("enter") })
  );
  harness.listeners.keydown.forEach(handler => handler({ key: "F5", preventDefault: () => preventions.push("f5") }));
  same(preventions, ["down", "enter"]);

  harness.document.activeElement = button("lastbed", undefined, false);
  harness.listeners["prisma-controller-action"].forEach(handler => handler({ detail: { action: "co.select", state: "released" } }));
  assert.equal(sent.length, 0);

  harness.listeners["prisma-controller-action"].forEach(handler => handler({ detail: { action: "co.select", state: "pressed" } }));
  assert.equal(sent.length, 1);
  same(JSON.parse(sent[0]), { action: "respawn", point: "lastbed", friendId: "" });

  harness.listeners["prisma-controller-action"].forEach(handler => handler({ detail: { action: "co.navUp", state: "pressed" } }));
  harness.listeners["prisma-controller-action"].forEach(handler => handler({ detail: { action: "co.navDown", state: "pressed" } }));
  harness.listeners["prisma-controller-action"].forEach(handler => handler({ detail: { action: "co.unknown", state: "pressed" } }));
  harness.listeners["prisma-controller-action"].forEach(handler => handler({}));
  assert.equal(sent.length, 1);

  model.handleButton("a");
  model.handleButton("dpad_up");
  model.handleButton("dpad_down");
  assert.equal(sent.length, 1);
}

function stimpakButton(disabled) {
  return {
    tagName: "BUTTON",
    disabled: disabled === true,
    getAttribute(name) {
      return name === "data-action" ? "stimpak" : null;
    }
  };
}

function TestRespawnDowned() {
  const harness = createHarness(RESPAWN, "coRespawn");
  const model = harness.window.coRespawnModel;
  const sent = [];
  harness.window.coDownedAction = payload => sent.push(payload);

  const html = model.render({ phase: "downed", stimpaks: 2, points: [], friends: [] });
  assert.ok(html.includes('data-action="stimpak"'));
  assert.ok(html.includes("Use Stimpak (2)"));
  assert.ok(!html.includes("YOU DIED"));
  assert.ok(!html.includes('data-action="stimpak" disabled'), "a stimpak button with stock stays enabled");

  const empty = model.render({ phase: "downed", stimpaks: 0, points: [], friends: [] });
  assert.ok(empty.includes('data-action="stimpak" disabled'));
  assert.ok(empty.includes("No Stimpak"));
  assert.ok(!empty.includes("YOU DIED"));

  const costly = model.render({ phase: "downed", stimpaks: 2, stimpakCost: 3, points: [], friends: [] });
  assert.ok(costly.includes('data-action="stimpak" disabled'), "two Stimpaks cannot pay a cost of three");
  assert.ok(costly.includes("Need 3 Stimpaks"));
  const paid = model.render({ phase: "downed", stimpaks: 3, stimpakCost: 3, points: [], friends: [] });
  assert.ok(!paid.includes('data-action="stimpak" disabled'));
  assert.ok(paid.includes("Use 3 Stimpaks (3)"));
  const free = model.render({ phase: "downed", stimpaks: 0, stimpakCost: 0, points: [], friends: [] });
  assert.ok(!free.includes('data-action="stimpak" disabled'), "a free revive needs no Stimpak");
  assert.ok(free.includes("Get Up"));

  assert.equal(harness.window.coRespawnSetState({ phase: "downed", stimpaks: 2, points: [], friends: [] }), true);
  assert.equal(harness.root.className, "co-respawn is-downed");

  click(harness.root, harness.listeners, stimpakButton(false));
  assert.equal(sent.length, 1);
  assert.equal(sent[0], '{"action":"stimpak"}');
  assert.ok(harness.root.innerHTML.indexOf("Using Stimpak") >= 0);

  click(harness.root, harness.listeners, stimpakButton(false));
  assert.equal(sent.length, 1, "a second press while the stimpak request is pending sends nothing");

  harness.window.coRespawnSetState({ phase: "downed", stimpaks: 2, points: [], friends: [] });
  click(harness.root, harness.listeners, stimpakButton(false));
  assert.equal(sent.length, 1, "a repeated downed push with the same state keeps the request pending");

  harness.window.coRespawnSetState({ phase: "downed", stimpaks: 0, points: [], friends: [] });
  click(harness.root, harness.listeners, stimpakButton(true));
  assert.equal(sent.length, 1, "a disabled stimpak button sends nothing");
}

function TestRespawnContainerClass() {
  const harness = createHarness(RESPAWN, "coRespawn");
  const set = state => harness.window.coRespawnSetState(state);

  assert.equal(harness.root.className, "co-respawn is-hidden", "the initial hidden paint must not darken the screen");

  set({ phase: "downed", stimpaks: 1, points: [], friends: [] });
  assert.equal(harness.root.className, "co-respawn is-downed");

  set({ phase: "dead", points: [], friends: [] });
  assert.equal(harness.root.className, "co-respawn", "only the death menu gets the dark backdrop");

  set({ phase: "hidden", points: [], friends: [] });
  assert.equal(harness.root.className, "co-respawn is-hidden", "a revive must not leave a dark flash behind");
}

function main() {
  TestOverlayClock();
  TestOverlayNormalize();
  TestOverlayRender();
  TestOverlayPromptRender();
  TestOverlayTicks();
  TestOverlayBarUsesPeak();
  TestRespawnNormalize();
  TestRespawnRender();
  TestRespawnChoice();
  TestRespawnPendingRelease();
  TestRespawnAcceptsPushedPayload();
  TestRespawnDowned();
  TestRespawnContainerClass();
  TestRespawnInput();
  console.log("Downed overlay and respawn menu regression passed");
}

main();
