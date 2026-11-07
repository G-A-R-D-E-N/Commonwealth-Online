const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const source = fs.readFileSync(__dirname + "/capture-hud.js", "utf8");
const markup = fs.readFileSync(__dirname + "/index.html", "utf8");
const preview = fs.readFileSync(__dirname + "/preview.html", "utf8");
const ids = [
  "coCaptureHud",
  "coCaptureName",
  "coCaptureBar",
  "coCaptureFill",
  "coCaptureStatus",
  "coCaptureMessage",
  "coWorkbench",
  "coWorkbenchLabel",
  "coWorkbenchBar",
  "coWorkbenchFill",
  "coProtection",
  "coSelfProtection"
];

function fakeElement(id) {
  return {
    id,
    hidden: false,
    textContent: "",
    style: {},
    attributes: {},
    setAttribute(name, value) {
      this.attributes[name] = String(value);
    },
    getAttribute(name) {
      return this.attributes[name];
    }
  };
}

function fakeClock() {
  let timers = [];
  let nextId = 1;
  return {
    setInterval(callback) {
      timers.push({ id: nextId, callback });
      nextId += 1;
      return nextId - 1;
    },
    clearInterval(id) {
      timers = timers.filter((timer) => timer.id !== id);
    },
    advance(seconds) {
      for (let second = 0; second < seconds; second += 1) {
        timers.slice().forEach((timer) => timer.callback());
      }
    },
    count() {
      return timers.length;
    }
  };
}

function load() {
  const elements = {};
  for (const id of ids) {
    elements[id] = fakeElement(id);
  }
  const window = {};
  const clock = fakeClock();
  window.setInterval = clock.setInterval;
  window.clearInterval = clock.clearInterval;
  const document = {
    getElementById: (id) => elements[id] || null
  };
  vm.runInNewContext(source, { document, window });
  return { window, elements, clock };
}

const plain = (value) => JSON.parse(JSON.stringify(value));
const { window, elements, clock } = load();
const model = window.coCaptureHudModel;

function near(overrides, localProgress = null) {
  const settlement = Object.assign(
    { buildZoneId: 1, state: "capturing", ownerProfileId: "", ownerName: "", capturerProfileId: "p2", capturerName: "", raid: false, progressPercent: 45, secondsLeft: 66, cooldownSeconds: 0 },
    overrides
  );
  return { settlements: [settlement], nearBuildZoneId: 1, canCapture: false, localProgress, lastResult: null };
}

const cases = [];
function test(name, fn) {
  cases.push([name, fn]);
}

test("no settlement under the player keeps the HUD inactive", () => {
  const inputs = [
    null,
    "text",
    {},
    { settlements: "none", nearBuildZoneId: 1 },
    { settlements: [], nearBuildZoneId: null },
    { settlements: [{ buildZoneId: 1, state: "capturing", progressPercent: 50 }], nearBuildZoneId: 2 },
    { settlements: [null, 5], nearBuildZoneId: 5 },
    { settlements: [{ buildZoneId: 1, state: "capturing" }], nearBuildZoneId: "1" }
  ];
  for (const input of inputs) {
    const capture = plain(model.adaptPluginState(input)).capture;
    assert.equal(capture.active, false, JSON.stringify(input));
    assert.equal(capture.state, "neutral", JSON.stringify(input));
  }
});

test("a capturing settlement maps every HUD field", () => {
  const capture = plain(model.adaptPluginState(near({}, 45))).capture;
  assert.deepEqual(capture, {
    active: true,
    state: "capturing",
    progressPercent: 45,
    secondsLeft: 66,
    settlementName: "Unnamed settlement",
    message: "Stay in range to finish the capture.",
    prompt: false
  });
});

test("a contested settlement shows its own progress and the paused message", () => {
  const capture = plain(model.adaptPluginState(near({ state: "contested", progressPercent: 30, secondsLeft: 40 }))).capture;
  assert.equal(capture.active, true);
  assert.equal(capture.state, "contested");
  assert.equal(capture.progressPercent, 30);
  assert.equal(capture.message, "A rival is in range. Progress is paused.");
});

test("an owned settlement stays inactive when another settlement has local progress", () => {
  const owned = plain(model.adaptPluginState(near({ state: "owned", ownerProfileId: "p1", progressPercent: 0, secondsLeft: 0 }))).capture;
  assert.equal(owned.active, false);
  assert.equal(owned.state, "owned");
  assert.equal(owned.message, "");

  const local = plain(model.adaptPluginState(near({ state: "owned", ownerProfileId: "p1", progressPercent: 0, secondsLeft: 0 }, 12))).capture;
  assert.equal(local.active, false);
  assert.equal(local.state, "owned");
  assert.equal(local.progressPercent, 0);
});

test("local progress elsewhere cannot replace the nearby capturer's progress", () => {
  const capture = plain(model.adaptPluginState(near({ progressPercent: 30 }, 100))).capture;
  assert.equal(capture.state, "capturing");
  assert.equal(capture.progressPercent, 30);
});

test("malformed fields are clamped, floored or replaced", () => {
  const unknownState = plain(model.adaptPluginState(near({ state: "bogus", progressPercent: "50", secondsLeft: -5 }))).capture;
  assert.equal(unknownState.state, "neutral");
  assert.equal(unknownState.progressPercent, 0);
  assert.equal(unknownState.secondsLeft, 0);
  assert.equal(unknownState.settlementName, "Unnamed settlement");
  assert.equal(unknownState.active, false);

  const overflow = plain(model.adaptPluginState(near({ state: "capturing", progressPercent: 150, secondsLeft: 12.9 }))).capture;
  assert.equal(overflow.progressPercent, 100);
  assert.equal(overflow.secondsLeft, 12);
});

test("large build zone ids are matched exactly", () => {
  const big = 2 ** 40;
  const capture = plain(model.adaptPluginState({
    settlements: [{ buildZoneId: big, state: "capturing", progressPercent: 5, secondsLeft: 1 }],
    nearBuildZoneId: big,
    localProgress: null
  })).capture;
  assert.equal(capture.active, true);
  assert.equal(capture.progressPercent, 5);
  assert.equal(capture.settlementName, "Unnamed settlement");
});

test("describeCapture renders the status text for each state", () => {
  const capturing = plain(model.describeCapture({ capture: { active: true, state: "capturing", progressPercent: 45, secondsLeft: 66, settlementName: "Sanctuary Hills", message: "m" } }));
  assert.equal(capturing.visible, true);
  assert.equal(capturing.status, "Being captured 45% - 66s left");
  assert.equal(capturing.name, "Sanctuary Hills");
  assert.equal(capturing.message, "m");

  const noSeconds = plain(model.describeCapture({ capture: { active: true, state: "capturing", progressPercent: 45, secondsLeft: 0 } }));
  assert.equal(noSeconds.status, "Being captured 45%");

  const contested = plain(model.describeCapture({ capture: { active: true, state: "contested", progressPercent: 30 } }));
  assert.equal(contested.status, "Contested at 30%");

  const owned = plain(model.describeCapture({ capture: { active: true, state: "owned" } }));
  assert.equal(owned.status, "Owned");

  const idle = plain(model.describeCapture({ capture: { active: false, state: "neutral" } }));
  assert.equal(idle.visible, false);
  assert.equal(idle.status, "Unclaimed");

  const garbage = plain(model.describeCapture("text"));
  assert.equal(garbage.visible, false);
  assert.equal(garbage.percent, 0);
  assert.equal(garbage.state, "neutral");
  assert.equal(garbage.name, "");
});

test("at the workbench the HUD shows the settlement name and the key to press", () => {
  window.coCaptureSetState(JSON.stringify({
    settlements: [{ buildZoneId: 20, name: "Sanctuary Hills", state: "neutral" }],
    nearBuildZoneId: 20,
    promptCapture: true,
    localProgress: 0
  }));
  assert.equal(elements.coCaptureHud.hidden, false);
  assert.equal(elements.coCaptureName.textContent, "Sanctuary Hills");
  assert.equal(elements.coCaptureStatus.textContent, "Press H to capture");
  assert.equal(elements.coCaptureBar.hidden, true);
});

test("a refused capture is explained on the HUD", () => {
  window.coCaptureSetState(JSON.stringify({
    settlements: [{ buildZoneId: 20, name: "Sanctuary Hills", state: "owned" }],
    nearBuildZoneId: 20,
    promptCapture: false,
    localProgress: 0,
    lastResult: { ok: false, code: "hold_limit" }
  }));
  assert.equal(elements.coCaptureHud.hidden, false);
  assert.equal(elements.coCaptureMessage.textContent, "You already hold the maximum number of settlements.");
});

test("a stale noAttempt reply after a finished capture does not keep the HUD up", () => {
  const input = {
    settlements: [{ buildZoneId: 20, name: "Sanctuary Hills", state: "owned", ownerProfileId: "self" }],
    nearBuildZoneId: 20,
    promptCapture: false,
    localProgress: 0,
    lastResult: { ok: false, code: "no_attempt" }
  };
  const capture = plain(model.adaptPluginState(input)).capture;
  assert.equal(capture.message, "");
  assert.equal(plain(model.describeCapture({ capture })).visible, false);
  window.coCaptureSetState(JSON.stringify(input));
  assert.equal(elements.coCaptureHud.hidden, true);
});

test("a timed-out capture says so on the HUD", () => {
  window.coCaptureSetState(JSON.stringify({
    settlements: [{ buildZoneId: 20, name: "Sanctuary Hills", state: "neutral" }],
    nearBuildZoneId: 20,
    promptCapture: false,
    localProgress: 0,
    lastResult: { ok: false, code: "timed_out" }
  }));
  assert.equal(elements.coCaptureHud.hidden, false);
  assert.equal(elements.coCaptureMessage.textContent, "Capture timed out. Press H to try again.");
});

test("no prompt shows beyond the workbench or during a capture", () => {
  const away = plain(model.adaptPluginState({ settlements: [{ buildZoneId: 20, state: "neutral" }], nearBuildZoneId: 20, promptCapture: false })).capture;
  assert.equal(away.prompt, false);
  assert.equal(plain(model.describeCapture({ capture: away })).visible, false);
  const busy = plain(model.adaptPluginState({ settlements: [{ buildZoneId: 20, state: "capturing" }], nearBuildZoneId: 20, promptCapture: true })).capture;
  assert.equal(busy.prompt, false);
});

test("coCaptureSetState paints the active HUD and hides it when idle", () => {
  window.coCaptureSetState(JSON.stringify(near({}, 45)));
  assert.equal(elements.coCaptureHud.hidden, false);
  assert.equal(elements.coCaptureHud.attributes["data-state"], "capturing");
  assert.equal(elements.coCaptureName.textContent, "Unnamed settlement");
  assert.equal(elements.coCaptureFill.style.width, "45%");
  assert.equal(elements.coCaptureBar.attributes["aria-valuenow"], "45");
  assert.equal(elements.coCaptureStatus.textContent, "Being captured 45% - 66s left");
  assert.equal(elements.coCaptureMessage.hidden, false);
  assert.equal(elements.coCaptureMessage.textContent, "Stay in range to finish the capture.");

  window.coCaptureSetState(JSON.stringify({ settlements: [], nearBuildZoneId: null }));
  assert.equal(elements.coCaptureHud.hidden, true);

  window.coCaptureSetState(JSON.stringify(near({ state: "owned", progressPercent: 0, secondsLeft: 0 })));
  assert.equal(elements.coCaptureHud.hidden, true);

  window.coCaptureSetState("{not json");
  assert.equal(elements.coCaptureHud.hidden, true);
});

test("the script never assigns markup", () => {
  assert.equal(source.includes("innerHTML"), false);
});

test("index.html carries every element the script paints and loads the script", () => {
  for (const id of ids) {
    assert.ok(markup.includes(`id="${id}"`), `index.html has #${id}`);
  }
  const looked = [...source.matchAll(/getElementById\("(\w+)"\)/g)].map((match) => match[1]);
  assert.deepEqual([...new Set(looked)].sort(), [...ids].sort());
  assert.ok(markup.includes('<script src="capture-hud.js"></script>'));
  assert.ok(markup.includes('href="styles.css"'));
});

test("preview.html loads the same view files and pushes samples through the entry point", () => {
  assert.ok(preview.includes('<script src="capture-hud.js"></script>'));
  assert.ok(preview.includes('href="styles.css"'));
  assert.ok(preview.includes("window.coCaptureSetState"));
});

test("absent workbench, protection and self fields leave the adapted state inert", () => {
  const inputs = [
    null,
    "text",
    {},
    { settlements: [{ buildZoneId: 1, state: "capturing" }], nearBuildZoneId: 1 }
  ];
  for (const input of inputs) {
    const adapted = plain(model.adaptPluginState(input));
    assert.equal(adapted.workbench.present, false, JSON.stringify(input));
    assert.equal(adapted.protection.active, false, JSON.stringify(input));
    assert.equal(adapted.selfProtection.active, false, JSON.stringify(input));
  }
});

test("workbench health is whole, clamped and ignores malformed values", () => {
  const hurt = plain(model.adaptPluginState(near({ workbenchHp: 1850, workbenchMaxHp: 2000 }))).workbench;
  assert.deepEqual(hurt, { present: true, hpPercent: 92, hp: 1850, maxHp: 2000, broken: false });

  const over = plain(model.adaptPluginState(near({ workbenchHp: 2500, workbenchMaxHp: 2000 }))).workbench;
  assert.equal(over.hp, 2000);
  assert.equal(over.hpPercent, 100);

  const negative = plain(model.adaptPluginState(near({ workbenchHp: -40, workbenchMaxHp: 2000 }))).workbench;
  assert.equal(negative.hp, 0);
  assert.equal(negative.hpPercent, 0);

  const malformed = plain(model.adaptPluginState(near({ workbenchHp: NaN, workbenchMaxHp: "2000" }))).workbench;
  assert.deepEqual(malformed, { present: false, hpPercent: 0, hp: 0, maxHp: 0, broken: false });

  const absent = plain(model.adaptPluginState(near({}))).workbench;
  assert.equal(absent.present, false);
});

test("a destroyed workbench reads as destroyed at zero health", () => {
  const broken = plain(model.adaptPluginState(near({ workbenchHp: 900, workbenchMaxHp: 2000, workbenchBroken: true }))).workbench;
  assert.deepEqual(broken, { present: true, hpPercent: 0, hp: 0, maxHp: 2000, broken: true });
  const view = plain(model.describeCapture({ workbench: broken })).workbench;
  assert.equal(view.text, "Workbench destroyed");
  assert.equal(view.health, "low");
  assert.equal(view.bar, true);
});

test("workbench text groups thousands and health bands follow the percent", () => {
  const healthy = plain(model.describeCapture({ workbench: { present: true, hpPercent: 92, hp: 1850, maxHp: 2000, broken: false } })).workbench;
  assert.equal(healthy.text, "Workbench 1,850 / 2,000");
  assert.equal(healthy.percent, 92);
  assert.equal(healthy.health, "ok");

  const band = (hpPercent) => plain(model.describeCapture({ workbench: { present: true, hpPercent, hp: 0, maxHp: 0 } })).workbench.health;
  assert.equal(band(60), "ok");
  assert.equal(band(59), "hurt");
  assert.equal(band(30), "hurt");
  assert.equal(band(29), "low");
  assert.equal(band(0), "low");
});

test("protection formats the remaining time and needs a known active reason", () => {
  const cases = [
    [{ active: true, reason: "settlementProtected", secondsLeft: 11520 }, "Raid protected: 3h 12m left"],
    [{ active: true, reason: "ownerNewPlayer", secondsLeft: 169200 }, "New-player protection: 47h left"],
    [{ active: true, reason: "settlementProtected", secondsLeft: 3600 }, "Raid protected: 1h left"],
    [{ active: true, reason: "settlementProtected", secondsLeft: 720 }, "Raid protected: 12m left"],
    [{ active: true, reason: "settlementProtected", secondsLeft: 45 }, "Raid protected: 45s left"],
    [{ active: true, reason: "settlementProtected", secondsLeft: -9 }, "Raid protected: 0s left"]
  ];
  for (const [raidProtection, text] of cases) {
    const protection = plain(model.adaptPluginState(near({ raidProtection }))).protection;
    assert.equal(protection.active, true, text);
    assert.equal(protection.text, text);
  }

  const unknown = plain(model.adaptPluginState(near({ raidProtection: { active: true, reason: "bogus", secondsLeft: 60 } }))).protection;
  assert.deepEqual(unknown, { active: true, reason: "unknown", secondsLeft: 60, text: "Protected: 1m left" });

  const noReason = plain(model.adaptPluginState(near({ raidProtection: { active: true, secondsLeft: 60 } }))).protection;
  assert.deepEqual(noReason, { active: true, reason: "unknown", secondsLeft: 60, text: "Protected: 1m left" });

  const off = plain(model.adaptPluginState(near({ raidProtection: { active: false, reason: "settlementProtected", secondsLeft: 60 } }))).protection;
  assert.equal(off.active, false);
  assert.equal(off.text, "");

  const absent = plain(model.adaptPluginState(near({}))).protection;
  assert.deepEqual(absent, { active: false, reason: "none", secondsLeft: 0, text: "" });
});

test("self protection shows only while active, even away from a settlement", () => {
  const away = plain(model.adaptPluginState({ settlements: [], nearBuildZoneId: null, selfRaidProtection: { active: true, secondsLeft: 169200 } }));
  assert.deepEqual(away.selfProtection, { active: true, secondsLeft: 169200, text: "Protected: 47h" });
  assert.equal(plain(model.describeCapture(away)).visible, true);

  const off = plain(model.adaptPluginState({ settlements: [], nearBuildZoneId: null, selfRaidProtection: { active: false, secondsLeft: 600 } }));
  assert.equal(off.selfProtection.text, "");
  assert.equal(plain(model.describeCapture(off)).visible, false);

  const malformed = plain(model.adaptPluginState({ settlements: [], nearBuildZoneId: null, selfRaidProtection: "yes" }));
  assert.equal(malformed.selfProtection.active, false);
});

test("a workbench with no capture activity still shows the HUD", () => {
  const withBench = plain(model.describeCapture(model.adaptPluginState(near({ state: "owned", progressPercent: 0, secondsLeft: 0, workbenchHp: 10, workbenchMaxHp: 20 }))));
  assert.equal(withBench.visible, true);
  assert.equal(withBench.active, false);
  assert.equal(withBench.workbench.visible, true);
});

test("coCaptureSetState paints workbench and protection lines and hides empty ones", () => {
  window.coCaptureSetState(JSON.stringify(near({
    state: "owned",
    progressPercent: 0,
    secondsLeft: 0,
    workbenchHp: 1850,
    workbenchMaxHp: 2000,
    raidProtection: { active: true, reason: "settlementProtected", secondsLeft: 11520 }
  })));
  assert.equal(elements.coCaptureHud.hidden, false);
  assert.equal(elements.coCaptureBar.hidden, true);
  assert.equal(elements.coWorkbench.hidden, false);
  assert.equal(elements.coWorkbench.attributes["data-health"], "ok");
  assert.equal(elements.coWorkbenchLabel.textContent, "Workbench 1,850 / 2,000");
  assert.equal(elements.coWorkbenchBar.hidden, false);
  assert.equal(elements.coWorkbenchFill.style.width, "92%");
  assert.equal(elements.coWorkbenchBar.attributes["aria-valuenow"], "92");
  assert.equal(elements.coProtection.hidden, false);
  assert.equal(elements.coProtection.textContent, "Raid protected: 3h 12m left");
  assert.equal(elements.coSelfProtection.hidden, true);

  window.coCaptureSetState(JSON.stringify(near({ state: "owned", progressPercent: 0, secondsLeft: 0, workbenchHp: 0, workbenchMaxHp: 2000, workbenchBroken: true })));
  assert.equal(elements.coWorkbenchLabel.textContent, "Workbench destroyed");
  assert.equal(elements.coWorkbench.attributes["data-health"], "low");
  assert.equal(elements.coWorkbenchFill.style.width, "0%");
  assert.equal(elements.coProtection.hidden, true);

  window.coCaptureSetState(JSON.stringify({ settlements: [], nearBuildZoneId: null, selfRaidProtection: { active: true, secondsLeft: 169200 } }));
  assert.equal(elements.coCaptureHud.hidden, false);
  assert.equal(elements.coCaptureName.hidden, true);
  assert.equal(elements.coCaptureStatus.hidden, true);
  assert.equal(elements.coCaptureMessage.hidden, true);
  assert.equal(elements.coWorkbench.hidden, true);
  assert.equal(elements.coSelfProtection.hidden, false);
  assert.equal(elements.coSelfProtection.textContent, "Protected: 47h");

  window.coCaptureSetState(JSON.stringify({ settlements: [], nearBuildZoneId: null }));
  assert.equal(elements.coCaptureHud.hidden, true);
  assert.equal(elements.coSelfProtection.hidden, true);
});

test("a protection line counts down once a second and drops at zero", () => {
  window.coCaptureSetState(JSON.stringify(near({ state: "owned", progressPercent: 0, secondsLeft: 0, raidProtection: { active: true, reason: "settlementProtected", secondsLeft: 3 } })));
  assert.equal(elements.coProtection.textContent, "Raid protected: 3s left");
  assert.equal(clock.count(), 1);
  clock.advance(1);
  assert.equal(elements.coProtection.textContent, "Raid protected: 2s left");
  clock.advance(1);
  assert.equal(elements.coProtection.textContent, "Raid protected: 1s left");
  clock.advance(1);
  assert.equal(elements.coProtection.hidden, true);
  assert.equal(elements.coProtection.textContent, "");
  assert.equal(elements.coCaptureHud.hidden, true);
  assert.equal(clock.count(), 0);
});

test("self protection counts down and its line and HUD drop at expiry", () => {
  window.coCaptureSetState(JSON.stringify({ settlements: [], nearBuildZoneId: null, selfRaidProtection: { active: true, secondsLeft: 61 } }));
  assert.equal(elements.coSelfProtection.textContent, "Protected: 1m");
  clock.advance(1);
  assert.equal(elements.coSelfProtection.textContent, "Protected: 1m");
  clock.advance(59);
  assert.equal(elements.coSelfProtection.textContent, "Protected: 1s");
  clock.advance(1);
  assert.equal(elements.coSelfProtection.hidden, true);
  assert.equal(elements.coCaptureHud.hidden, true);
  assert.equal(clock.count(), 0);
});

test("a new state push restarts the countdown from the server's value", () => {
  window.coCaptureSetState(JSON.stringify({ settlements: [], nearBuildZoneId: null, selfRaidProtection: { active: true, secondsLeft: 10 } }));
  clock.advance(4);
  window.coCaptureSetState(JSON.stringify({ settlements: [], nearBuildZoneId: null, selfRaidProtection: { active: true, secondsLeft: 20 } }));
  assert.equal(elements.coSelfProtection.textContent, "Protected: 20s");
  assert.equal(clock.count(), 1);
  clock.advance(5);
  assert.equal(elements.coSelfProtection.textContent, "Protected: 15s");
});

test("no protection means no countdown timer, and a later idle push stops a running one", () => {
  window.coCaptureSetState(JSON.stringify({ settlements: [], nearBuildZoneId: null, selfRaidProtection: { active: true, secondsLeft: 30 } }));
  assert.equal(clock.count(), 1);
  window.coCaptureSetState(JSON.stringify({ settlements: [], nearBuildZoneId: null, selfRaidProtection: { active: false, secondsLeft: 0 } }));
  assert.equal(clock.count(), 0);
  assert.equal(elements.coSelfProtection.hidden, true);
});

let failed = 0;
for (const [name, fn] of cases) {
  try {
    fn();
    console.log(`PASS ${name}`);
  } catch (error) {
    failed += 1;
    console.log(`FAIL ${name}`);
    console.log(error.message);
  }
}
if (failed > 0) {
  console.log(`capture HUD tests FAILED: ${failed} of ${cases.length}`);
  process.exitCode = 1;
} else {
  console.log(`capture HUD tests passed: ${cases.length} cases`);
}
