const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const source = fs.readFileSync(__dirname + "/structure-hud.js", "utf8");
const markup = fs.readFileSync(__dirname + "/index.html", "utf8");
const preview = fs.readFileSync(__dirname + "/preview.html", "utf8");
const ids = ["coStructureHud", "coStructureBar", "coStructureFill", "coStructureValue"];

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

let clock = 0;
let nextTimerId = 1;
let timers = new Map();

function fakeSetTimeout(callback, delay) {
  const id = nextTimerId++;
  timers.set(id, { at: clock + delay, callback });
  return id;
}

function fakeClearTimeout(id) {
  timers.delete(id);
}

function advance(ms) {
  clock += ms;
  for (const [id, timer] of [...timers]) {
    if (timer.at <= clock && timers.has(id)) {
      timers.delete(id);
      timer.callback();
    }
  }
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
  clock = 0;
  nextTimerId = 1;
  timers = new Map();
  vm.runInNewContext(source, { document, window, setTimeout: fakeSetTimeout, clearTimeout: fakeClearTimeout });
  return { window, elements };
}

const plain = (value) => JSON.parse(JSON.stringify(value));
const { window, elements } = load();
const model = window.coStructureHudModel;

const cases = [];
function test(name, fn) {
  cases.push([name, fn]);
}

test("missing, dead or invalid health keeps the HUD hidden", () => {
  const inputs = [
    null,
    "text",
    {},
    { health: 50 },
    { health: 0, maxHealth: 100 },
    { health: -5, maxHealth: 100 },
    { health: 50, maxHealth: 0 },
    { health: "50", maxHealth: 100 },
    { health: NaN, maxHealth: 100 }
  ];
  for (const input of inputs) {
    assert.equal(model.describeHealth(input).visible, false, JSON.stringify(input));
  }
});

test("green above half, amber above a quarter, red at a quarter or below", () => {
  assert.deepEqual(plain(model.describeHealth({ health: 80, maxHealth: 100 })), { visible: true, tier: "green", percent: 80 });
  assert.equal(model.describeHealth({ health: 51, maxHealth: 100 }).tier, "green");
  assert.equal(model.describeHealth({ health: 50, maxHealth: 100 }).tier, "amber");
  assert.equal(model.describeHealth({ health: 26, maxHealth: 100 }).tier, "amber");
  assert.equal(model.describeHealth({ health: 25, maxHealth: 100 }).tier, "red");
  assert.equal(model.describeHealth({ health: 1, maxHealth: 100 }).tier, "red");
});

test("health above the maximum clamps to a full bar", () => {
  assert.deepEqual(plain(model.describeHealth({ health: 150, maxHealth: 100 })), { visible: true, tier: "green", percent: 100 });
});

test("coStructureSetState paints the bar and percentage, then hides on dead health", () => {
  window.coStructureSetState(JSON.stringify({ objectId: 7, health: 30, maxHealth: 100 }));
  assert.equal(elements.coStructureHud.hidden, false);
  assert.equal(elements.coStructureHud.attributes["data-tier"], "amber");
  assert.equal(elements.coStructureFill.style.width, "30%");
  assert.equal(elements.coStructureValue.textContent, "30%");
  assert.equal(elements.coStructureBar.attributes["aria-valuenow"], "30");
  window.coStructureSetState(JSON.stringify({ objectId: 7, health: 0, maxHealth: 100 }));
  assert.equal(elements.coStructureHud.hidden, true);
  assert.equal(elements.coStructureValue.textContent, "");
});

test("the HUD hides 4000 ms after the last update and each update restarts that wait", () => {
  window.coStructureSetState(JSON.stringify({ health: 0, maxHealth: 100 }));
  window.coStructureSetState(JSON.stringify({ health: 90, maxHealth: 100 }));
  advance(3000);
  window.coStructureSetState(JSON.stringify({ health: 80, maxHealth: 100 }));
  advance(3999);
  assert.equal(elements.coStructureHud.hidden, false);
  advance(1);
  assert.equal(elements.coStructureHud.hidden, true);
});

test("malformed state hides the HUD and cancels the pending hide", () => {
  window.coStructureSetState(JSON.stringify({ health: 50, maxHealth: 100 }));
  window.coStructureSetState("{not json");
  assert.equal(elements.coStructureHud.hidden, true);
  assert.equal(timers.size, 0);
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
  assert.ok(markup.includes('<script src="structure-hud.js"></script>'));
  assert.ok(markup.includes('href="styles.css"'));
});

test("preview.html loads the same view files and pushes samples through the entry point", () => {
  assert.ok(preview.includes('<script src="structure-hud.js"></script>'));
  assert.ok(preview.includes('href="styles.css"'));
  assert.ok(preview.includes("window.coStructureSetState"));
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
  console.log(`structure HUD tests FAILED: ${failed} of ${cases.length}`);
  process.exitCode = 1;
} else {
  console.log(`structure HUD tests passed: ${cases.length} cases`);
}
