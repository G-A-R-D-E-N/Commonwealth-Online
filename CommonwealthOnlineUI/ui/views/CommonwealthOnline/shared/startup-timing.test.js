const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const path = require("node:path");
const source = fs.readFileSync(path.join(__dirname, "startup-timing.js"), "utf8");
for (const performance of [undefined, { getEntriesByType: () => [{ name: "script.js", duration: 7 }] }]) {
  const logs = [];
  const window = { performance };
  vm.runInNewContext(source, { window, console: { log: message => logs.push(message) } });
  window.CO_StartupTiming.record("browser-create", Date.now());
  assert.equal(logs.length, 0);
  window.CO_StartupTiming.flush();
  window.CO_StartupTiming.flush();
  assert.equal(logs.length, 1);
  const first = JSON.parse(logs[0].replace("[StartupTiming] ", ""));
  assert.equal(first.samples[0].name, "browser-create");
  assert.equal(first.resources.length, performance ? 1 : 0);
  window.CO_StartupTiming.record("settings-create", Date.now());
  assert.equal(logs.length, 2);
}
