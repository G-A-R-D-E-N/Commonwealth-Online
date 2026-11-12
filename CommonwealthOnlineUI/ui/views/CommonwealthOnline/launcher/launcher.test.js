"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

let rect = { left: 1500, top: 24, width: 220, height: 54 };
const regions = [];
const focusEvents = [];
const actions = [];
const listeners = {};
const scales = [];
const launcher = {
  getBoundingClientRect: () => rect,
  addEventListener: (name, callback) => { listeners[name] = callback; }
};
const context = {
  document: {
    documentElement: { style: { setProperty: (name, value) => scales.push([name, value]) } },
    getElementById: () => launcher
  },
  window: {
    innerWidth: 1920,
    innerHeight: 1080,
    coLauncherHitRegion: payload => regions.push(JSON.parse(payload)),
    coLauncherPointerFocus: payload => focusEvents.push(JSON.parse(payload)),
    coLauncherAction: payload => actions.push(JSON.parse(payload)),
    addEventListener: (name, callback) => { listeners[name] = callback; }
  },
  JSON
};

vm.runInNewContext(
  fs.readFileSync(path.join(__dirname, "script.js"), "utf8"),
  context,
  { filename: "script.js" }
);

assert.strictEqual(typeof context.window.setLauncherGeneration, "function");
assert.strictEqual(regions[0].generation, 0);
assert.deepStrictEqual(regions[0], {
  generation: 0,
  active: true,
  left: 1500,
  top: 24,
  width: 220,
  height: 54
});

context.window.setLauncherGeneration(7);

context.window.updateLauncherRegion();
assert.deepStrictEqual(regions.at(-1), {
  generation: 7,
  active: true,
  left: 1500,
  top: 24,
  width: 220,
  height: 54
});

listeners.pointerenter();
assert.deepStrictEqual(focusEvents.at(-1), { generation: 7, focus: true });
listeners.pointerleave();
assert.deepStrictEqual(focusEvents.at(-1), { generation: 7, focus: false });

listeners.click();
assert.deepStrictEqual(actions.at(-1), { generation: 7, action: "restore" });

rect = { left: 1600, top: 30, width: 220, height: 54 };
context.window.updateLauncherRegion();
assert.deepStrictEqual(regions.at(-1), {
  generation: 7,
  active: true,
  left: 1600,
  top: 30,
  width: 220,
  height: 54
});

context.window.innerWidth = 3840;
context.window.innerHeight = 2160;
rect = { left: 3200, top: 48, width: 440, height: 108 };
listeners.resize();
assert.deepStrictEqual(regions.at(-1), {
  generation: 7,
  active: true,
  left: 3200,
  top: 48,
  width: 440,
  height: 108
});
assert.deepStrictEqual(scales.at(-1), ["--co-ui-scale", "2"]);

rect = { left: 0, top: 0, width: 0, height: 0 };
context.window.updateLauncherRegion();
assert.deepStrictEqual(regions.at(-1), {
  generation: 7,
  active: false,
  left: 0,
  top: 0,
  width: 0,
  height: 0
});

rect = { left: 1600, top: 30, width: 220, height: 54 };
context.window.updateLauncherRegion();
assert.deepStrictEqual(regions.at(-1), {
  generation: 7,
  active: true,
  left: 1600,
  top: 30,
  width: 220,
  height: 54
});

assert.strictEqual(regions.at(-1).width < 1920, true);
assert.strictEqual(regions.at(-1).height < 1080, true);

assert.strictEqual(regions.every(region => Number.isInteger(region.generation)), true);
assert.strictEqual(focusEvents.every(event => Number.isInteger(event.generation)), true);
assert.strictEqual(actions.every(action => Number.isInteger(action.generation)), true);
console.log("Launcher bounded region regression passed");
