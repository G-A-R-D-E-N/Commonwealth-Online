const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const test = require("node:test");

const SOURCE = fs.readFileSync(__dirname + "/gamepad-sticks.js", "utf8");

function load(nodes) {
  const queued = [];
  const sandbox = {
    requestAnimationFrame: (callback) => queued.push(callback),
    cancelAnimationFrame: () => {},
    document: {
      scrollingElement: { scrollTop: 100, getClientRects: () => [1] },
      querySelectorAll: () => nodes || [],
    },
  };
  sandbox.window = sandbox;
  vm.runInNewContext(SOURCE, sandbox);
  return { sandbox, queued, scroller: sandbox.document.scrollingElement };
}

function runFrame(ctx) {
  ctx.queued.splice(0).forEach((callback) => callback());
}

test("stick values inside the 0.2 dead zone do not scroll or start a loop", () => {
  const ctx = load();
  ctx.sandbox.handleControllerAxes(0, 0, 0, 0.2);
  ctx.sandbox.handleControllerAxes(0, 0, 0, -0.15);
  assert.equal(ctx.queued.length, 0);
  assert.equal(ctx.scroller.scrollTop, 100);
});

test("pushing the right stick up scrolls content toward the top at full speed", () => {
  const ctx = load();
  ctx.sandbox.handleControllerAxes(0, 0, 0, 1);
  runFrame(ctx);
  assert.equal(ctx.scroller.scrollTop, 84);
});

test("pushing the right stick down scrolls content toward the bottom", () => {
  const ctx = load();
  ctx.sandbox.handleControllerAxes(0, 0, 0, -1);
  runFrame(ctx);
  assert.equal(ctx.scroller.scrollTop, 116);
});

test("the frame loop stops once the stick returns to zero", () => {
  const ctx = load();
  ctx.sandbox.handleControllerAxes(0, 0, 0, 1);
  runFrame(ctx);
  ctx.sandbox.handleControllerAxes(0, 0, 0, 0);
  runFrame(ctx);
  assert.equal(ctx.queued.length, 0);
  assert.equal(ctx.scroller.scrollTop, 84);
});

test("start(selector) scrolls the first rendered container", () => {
  const panel = { scrollTop: 50, getClientRects: () => [1] };
  const ctx = load([panel]);
  ctx.sandbox.CO_GamepadSticks.start(".faction-panel");
  ctx.sandbox.handleControllerAxes(0, 0, 0, 1);
  runFrame(ctx);
  assert.equal(panel.scrollTop, 34);
  assert.equal(ctx.scroller.scrollTop, 100);
});
