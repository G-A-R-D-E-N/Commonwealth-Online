const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const path = require("node:path");

function setup() {
  const elements = {};
  const ids = ["coSleep", "coSleepPicker", "coSleepLess", "coSleepMore", "coSleepStart", "coSleepCancel", "coSleepClock", "coSleepWake", "coSleepClockHand", "coSleepRemaining", "coSleepHours", "coSleepUnit"];
  for (const id of ids) {
    elements[id] = { hidden: false, disabled: false, textContent: "", style: {}, focus() { document.activeElement = this; }, addEventListener(type, fn) { this[type] = fn; } };
  }
  const sent = [];
  const window = { coSleepAction: value => sent.push(JSON.parse(value)), addEventListener(type, fn) { this[type] = fn; } };
  const document = { getElementById: id => elements[id] };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, "sleep.js"), "utf8"), { window, document, Number, Math, JSON });
  return { elements, sent, window, document };
}

test("sleep selection is bounded and sends the current open token", () => {
  const { elements, sent, window, document } = setup();
  assert.equal(elements.coSleep.hidden, true);
  window.coSleepSetState({ phase: "choose", hours: 99, maxHours: 3, token: 12 });
  assert.equal(document.activeElement, elements.coSleepStart);
  document.activeElement = null;
  window.keydown({ key: "Tab", shiftKey: true, preventDefault() {} });
  assert.equal(document.activeElement, elements.coSleepCancel);
  assert.equal(elements.coSleepHours.textContent, 3);
  assert.equal(elements.coSleepMore.disabled, true);
  elements.coSleepLess.click();
  elements.coSleepStart.click();
  assert.deepEqual(sent[0], { op: "sleep", hours: 2, token: 12 });
  window.coSleepController("dpad_right", "down");
  window.coSleepController("a", "down");
  assert.equal(sent.length, 1);
  window.coSleepSetState({ phase: "choose", hours: 3, maxHours: 3, token: 12 });
  window.coSleepController("a", "down");
  assert.deepEqual(sent[1], { op: "sleep", hours: 3, token: 12 });
  elements.coSleepLess.click();
  elements.coSleepLess.click();
  assert.equal(elements.coSleepHours.textContent, 1);
  assert.equal(elements.coSleepUnit.textContent, "hour");
});

test("resting displays the progress clock and lets the player wake early", () => {
  const { elements, sent, window, document } = setup();
  window.coSleepSetState({ phase: "choose", token: 9 });
  window.coSleepSetState({ phase: "resting", hours: 4, elapsed: 1.5, token: 9 });
  assert.equal(elements.coSleep.hidden, true);
  assert.equal(elements.coSleepClock.hidden, false);
  assert.equal(elements.coSleepRemaining.textContent, "3 hours remaining");
  assert.equal(elements.coSleepClockHand.style.transform, "rotate(45deg)");
  assert.equal(document.activeElement, elements.coSleepWake);
  elements.coSleepWake.click();
  window.keydown({ key: "Escape", preventDefault() {} });
  window.coSleepController("b", "down");
  assert.deepEqual(sent.splice(0), [
    { op: "cancel", hours: 4, token: 9 },
    { op: "cancel", hours: 4, token: 9 },
    { op: "cancel", hours: 4, token: 9 }
  ]);
  window.keydown({ key: "Tab", preventDefault() { throw new Error("clock captured tab"); } });
  assert.equal(elements.coSleepPicker.hidden, true);
  assert.equal(elements.coSleepStart.hidden, true);
  window.coSleepSetState({ phase: "closed" });
  window.keydown({ key: "Escape", preventDefault() { throw new Error("closed menu captured input"); } });
  assert.equal(elements.coSleepClock.hidden, true);
  assert.equal(sent.length, 0);
  window.coSleepSetState({ phase: "choose", hours: 2, token: 10 });
  assert.equal(elements.coSleep.hidden, false);
  assert.equal(document.activeElement, elements.coSleepStart);
  elements.coSleepStart.click();
  assert.deepEqual(sent[0], { op: "sleep", hours: 2, token: 10 });
});
