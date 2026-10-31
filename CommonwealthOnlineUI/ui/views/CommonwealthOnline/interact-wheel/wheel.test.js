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
    { id: "trade", label: "Trade" },
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

assert.equal(wheel.enabledIndex(normalized, 1), -1, "a disabled slice cannot be activated");
assert.equal(wheel.enabledIndex(normalized, 0), 0);
assert.equal(wheel.enabledIndex(normalized, 99), -1);
assert.equal(wheel.stepIndex(normalized, 0, 1), 2, "stepping skips the disabled slice");
assert.equal(wheel.stepIndex(normalized, 2, 1), 0, "stepping wraps");
assert.equal(wheel.stepIndex(normalized, 0, -1), 2, "stepping backwards wraps past the disabled slice");

const allDisabled = wheel.normalize({ slices: [{ id: "requestSent", label: "Sent", disabled: true }] });
assert.equal(wheel.stepIndex(allDisabled, 0, 1), -1, "an all-disabled wheel has no selection");

console.log("wheel.test: all checks passed.");
