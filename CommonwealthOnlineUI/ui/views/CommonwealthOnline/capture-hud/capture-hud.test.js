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
  "coCaptureMessage"
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

function load() {
  const elements = {};
  for (const id of ids) {
    elements[id] = fakeElement(id);
  }
  const window = {};
  const document = {
    getElementById: (id) => elements[id] || null
  };
  vm.runInNewContext(source, { document, window });
  return { window, elements };
}

const plain = (value) => JSON.parse(JSON.stringify(value));
const { window, elements } = load();
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
    settlementName: "Settlement 1",
    message: "Stay in range to finish the capture."
  });
});

test("a contested settlement shows its own progress and the paused message", () => {
  const capture = plain(model.adaptPluginState(near({ state: "contested", progressPercent: 30, secondsLeft: 40 }))).capture;
  assert.equal(capture.active, true);
  assert.equal(capture.state, "contested");
  assert.equal(capture.progressPercent, 30);
  assert.equal(capture.message, "A rival is in range. Progress is paused.");
});

test("an owned settlement is inactive unless the player's own progress makes it a capture", () => {
  const owned = plain(model.adaptPluginState(near({ state: "owned", ownerProfileId: "p1", progressPercent: 0, secondsLeft: 0 }))).capture;
  assert.equal(owned.active, false);
  assert.equal(owned.state, "owned");
  assert.equal(owned.message, "");

  const local = plain(model.adaptPluginState(near({ state: "owned", ownerProfileId: "p1", progressPercent: 0, secondsLeft: 0 }, 12))).capture;
  assert.equal(local.active, true);
  assert.equal(local.state, "capturing");
  assert.equal(local.progressPercent, 12);
});

test("malformed fields are clamped, floored or replaced", () => {
  const unknownState = plain(model.adaptPluginState(near({ state: "bogus", progressPercent: "50", secondsLeft: -5 }))).capture;
  assert.equal(unknownState.state, "neutral");
  assert.equal(unknownState.progressPercent, 0);
  assert.equal(unknownState.secondsLeft, 0);
  assert.equal(unknownState.settlementName, "Settlement 1");
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
  assert.equal(capture.settlementName, "Settlement " + big);
});

test("describeCapture renders the status text for each state", () => {
  const capturing = plain(model.describeCapture({ capture: { active: true, state: "capturing", progressPercent: 45, secondsLeft: 66, settlementName: "Settlement 1", message: "m" } }));
  assert.equal(capturing.visible, true);
  assert.equal(capturing.status, "Being captured 45% - 66s left");
  assert.equal(capturing.name, "Settlement 1");
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

test("coCaptureSetState paints the active HUD and hides it when idle", () => {
  window.coCaptureSetState(JSON.stringify(near({}, 45)));
  assert.equal(elements.coCaptureHud.hidden, false);
  assert.equal(elements.coCaptureHud.attributes["data-state"], "capturing");
  assert.equal(elements.coCaptureName.textContent, "Settlement 1");
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
