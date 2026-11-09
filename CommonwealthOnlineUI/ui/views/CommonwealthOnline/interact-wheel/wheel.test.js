const assert = require("node:assert");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const context = { window: {} };
vm.runInNewContext(fs.readFileSync(path.join(__dirname, "wheel.js"), "utf8"), context);
const wheel = context.window.coWheelModel;

const four = [{ id: "a" }, { id: "b" }, { id: "c" }, { id: "d" }];

assert.equal(wheel.sliceIndexForAngle(4, 0), 0, "straight up is the first slice");
assert.equal(wheel.sliceIndexForAngle(4, 44), 0, "the first slice owns the area just before the boundary");
assert.equal(wheel.sliceIndexForAngle(4, 45), 1, "the exact boundary belongs to the next slice");
assert.equal(wheel.sliceIndexForAngle(4, 46), 1, "past 45 degrees is the second slice");
assert.equal(wheel.sliceIndexForAngle(4, 90), 1, "right is the second slice");
assert.equal(wheel.sliceIndexForAngle(4, 180), 2, "down is the third slice");
assert.equal(wheel.sliceIndexForAngle(4, 270), 3, "left is the fourth slice");
assert.equal(wheel.sliceIndexForAngle(4, 359), 0, "just before up wraps to the first slice");
assert.equal(wheel.sliceIndexForAngle(0, 90), -1, "no slices means no index");
assert.equal(wheel.sliceIndexForAngle(4, Number.NaN), -1, "a bad angle is rejected");

assert.equal(wheel.angleForPoint(0, -1), 0, "up is zero degrees");
assert.equal(wheel.angleForPoint(1, 0), 90, "right is ninety degrees");
assert.equal(wheel.angleForPoint(0, 1), 180, "down is one eighty");
assert.equal(wheel.angleForPoint(-1, 0), 270, "left is two seventy");

assert.equal(wheel.sliceIndexForAxes(4, 0, 1), 0, "pushing the stick up selects the first slice");
assert.equal(wheel.sliceIndexForAxes(4, 1, 0), 1, "pushing right selects the second slice");
assert.equal(wheel.sliceIndexForAxes(4, 0, -1), 2, "pushing down selects the third slice");
assert.equal(wheel.sliceIndexForAxes(4, -1, 0), 3, "pushing left selects the fourth slice");
assert.equal(wheel.sliceIndexForAxes(4, 0.1, 0.1), -1, "the deadzone keeps the current selection");
assert.equal(wheel.sliceIndexForAxes(4, 0.9, -0.4), 1, "a diagonal push picks the nearest slice");

const normalized = wheel.normalize({
  title: "Stranger",
  slices: [
    { id: "addFriend", label: "Add Friend" },
    { id: "requestSent", label: "Friend Request Sent", disabled: true },
    { id: "bogus", label: "Bogus" },
    { id: "toString", label: "Unknown" },
    { id: "close", label: "Close" },
    { id: "close", label: "" }
  ]
});
assert.equal(normalized.title, "Stranger");
assert.deepEqual(JSON.parse(JSON.stringify(normalized.slices)), [
  { id: "addFriend", label: "Add Friend", disabled: false },
  { id: "requestSent", label: "Friend Request Sent", disabled: true },
  { id: "close", label: "Close", disabled: false }
], "unknown ops and empty labels are dropped");

assert.equal(wheel.normalize(null).title, "Unknown player");
assert.equal(wheel.normalize({ title: "" }).title, "Unknown player");
assert.equal(wheel.normalize({ title: "x".repeat(200) }).title, "Unknown player", "an oversized title falls back");

const fromString = wheel.normalize(JSON.stringify({ title: "Stranger", slices: [{ id: "close", label: "Close" }] }));
assert.equal(fromString.title, "Stranger", "a JSON string payload is accepted");
assert.equal(fromString.slices.length, 1, "a JSON string payload keeps its slices");
assert.equal(wheel.normalize("{not json").title, "Unknown player", "a malformed string falls back");

assert.equal(wheel.enabledIndex(normalized, 1), -1, "a disabled slice cannot be activated");
assert.equal(wheel.enabledIndex(normalized, 0), 0);
assert.equal(wheel.enabledIndex(normalized, 99), -1);
assert.equal(wheel.stepIndex(normalized, 0, 1), 2, "stepping skips the disabled slice");
assert.equal(wheel.stepIndex(normalized, 2, 1), 0, "stepping wraps");
assert.equal(wheel.stepIndex(normalized, 0, -1), 2, "stepping backwards wraps past the disabled slice");

const allDisabled = wheel.normalize({ slices: [{ id: "requestSent", label: "Sent", disabled: true }] });
assert.equal(wheel.stepIndex(allDisabled, 0, 1), -1, "an all-disabled wheel has no selection");

const actions = [];
context.window.coInteractWheelAction = function (json) { actions.push(JSON.parse(json)); };
context.window.coInteractWheelSetState({
  title: "Stranger",
  slices: [
    { id: "addFriend", label: "Add Friend" },
    { id: "removeFriend", label: "Remove Friend" },
    { id: "close", label: "Close" }
  ]
});
const controller = context.window.coInteractWheelController;
controller("a", "down");
assert.equal(actions.pop().op, "addFriend", "A activates the selected slice on the phase the game sends");
controller("a", "up");
assert.equal(actions.length, 0, "the release phase does nothing");
controller("press", "press");
assert.equal(actions.length, 0, "only the down and up phases the C++ sends are handled");
controller("dpad_right", "down");
controller("a", "down");
assert.equal(actions.pop().op, "removeFriend", "the D-pad moves the selection before A activates");
controller("dpad_left", "down");
controller("a", "down");
assert.equal(actions.pop().op, "addFriend", "the D-pad walks back");
controller("dpad_down", "down");
controller("a", "down");
assert.equal(actions.pop().op, "removeFriend", "dpad_down steps forward");
controller("dpad_up", "down");
controller("a", "down");
assert.equal(actions.pop().op, "addFriend", "dpad_up steps back");
controller("rb", "down");
controller("a", "down");
assert.equal(actions.pop().op, "removeFriend", "the right bumper steps forward");
controller("b", "down");
assert.equal(actions.pop().op, "close", "B closes the wheel");
assert.equal(actions.length, 0, "a close is sent once per press");

context.window.coInteractWheelSetState({
  title: "Stranger",
  slices: [{ id: "requestSent", label: "Friend Request Sent", disabled: true }]
});
controller("a", "down");
assert.equal(actions.length, 0, "a disabled slice cannot be activated from the controller");
context.window.coInteractWheelSetState({
  title: "Stranger",
  slices: [{ id: "requestSent", label: "Friend Request Sent", disabled: true }, { id: "close", label: "Close" }]
});
controller("dpad_right", "down");
controller("a", "down");
assert.equal(actions.pop().op, "close", "stepping skips the disabled slice")

context.window.coInteractWheelSetState({
  title: "Stranger",
  slices: [{ id: "trade", label: "Trade" }, { id: "close", label: "Close" }]
});
controller("dpad_left", "down");
controller("a", "down");
assert.equal(actions.pop().op, "trade", "picking Trade sends the trade action");
assert.equal(actions.length, 0, "picking Trade sends one action");

const handlers = {};
let focused = "";
const ring = {
  innerHTML: "",
  addEventListener: (name, fn) => { handlers[name] = fn; },
  contains: (element) => element === "focused-action",
  querySelector: (selector) => ({ focus: () => { focused = selector; } })
};
const title = { textContent: "" };
const dom = {
  readyState: "complete",
  getElementById: (id) => ({ coWheel: {}, coWheelRing: ring, coWheelTitle: title })[id],
  addEventListener: (name, fn) => { handlers[name] = fn; }
};
const domContext = { window: {}, document: dom };
vm.runInNewContext(fs.readFileSync(path.join(__dirname, "wheel.js"), "utf8"), domContext);
const sent = [];
domContext.window.coInteractWheelAction = (json) => sent.push(JSON.parse(json).op);
domContext.window.coInteractWheelSetState({ title: "Player", slices: [
  { id: "trade", label: "Trade" }, { id: "requestSent", label: "Waiting", disabled: true },
  { id: "close", label: '<Close & leave>' }
] });
assert.equal((ring.innerHTML.match(/class="wheel-sector /g) || []).length, 3, "each action has a radial sector");
assert.match(ring.innerHTML, /&lt;Close &amp; leave&gt;/, "labels remain escaped in the rendered wheel");
const target = (index) => ({ closest: () => ({ getAttribute: () => String(index) }) });
handlers.click({ target: target(1) });
assert.deepEqual(sent, [], "clicking a disabled sector never activates the previous selection");
handlers.click({ target: target(2) });
assert.deepEqual(sent, ["close"], "clicking a sector activates its action");
assert.match(ring.innerHTML, /wheel-center-label">&lt;Close &amp; leave&gt;/, "the selected action is shown in the center");
handlers.focusin({ target: target(0) });
assert.equal(focused, '.wheel-action[data-index="0"]', "tab focus follows the selected action after redraw");
handlers.keydown({ key: "ArrowRight", preventDefault: () => {} });
assert.equal(focused, '.wheel-action[data-index="2"]', "keyboard navigation skips disabled actions and restores focus");
handlers.keydown({ key: "Enter", preventDefault: () => {} });
assert.deepEqual(sent, ["close", "close"], "Enter activates the keyboard-focused action");
dom.activeElement = "focused-action";
focused = "";
domContext.window.coInteractWheelSetState({ title: "Player", slices: [
  { id: "trade", label: "Trade" }, { id: "requestSent", label: "Waiting", disabled: true },
  { id: "close", label: "Close" }
] });
assert.equal(focused, '.wheel-action[data-index="2"]', "state updates restore keyboard focus to the selection");

console.log("wheel.test: all checks passed.");
