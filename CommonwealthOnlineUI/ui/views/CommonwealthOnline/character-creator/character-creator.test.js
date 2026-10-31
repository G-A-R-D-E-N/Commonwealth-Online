"use strict";
// TODO-verify(#366): form-key display names mirror the bridge contract; see docs/code-notes/character-creator.md.

const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

function makeElement(tag) {
  const listeners = {};
  const element = {
    tagName: String(tag || "div").toUpperCase(),
    children: [],
    dataset: {},
    style: {},
    textContent: "",
    className: "",
    id: "",
    type: "",
    get childElementCount() {
      return this.children.length;
    },
    set innerHTML(value) {
      if (value === "") {
        this.children.length = 0;
      }
    },
    get innerHTML() {
      return "";
    },
    appendChild(child) {
      this.children.push(child);
      child.parentNode = this;
      return child;
    },
    addEventListener(name, callback) {
      (listeners[name] = listeners[name] || []).push(callback);
    },
    fire(name, event) {
      (listeners[name] || []).forEach((callback) => callback(event || {}));
    }
  };

  element.classList = {
    _names() {
      return String(element.className || "")
        .split(/\s+/)
        .filter((name) => name.length > 0);
    },
    add(name) {
      if (!this.contains(name)) {
        element.className = (element.className ? element.className + " " : "") + name;
      }
    },
    remove(name) {
      element.className = this._names()
        .filter((existing) => existing !== name)
        .join(" ");
    },
    contains(name) {
      return this._names().indexOf(name) !== -1;
    },
    toggle(name, force) {
      const on = force === undefined ? !this.contains(name) : !!force;
      if (on) {
        this.add(name);
      } else {
        this.remove(name);
      }
      return on;
    }
  };

  return element;
}

const byId = {};
const sent = [];
const wireSent = [];
const documentListeners = {};

const document = {
  getElementById(id) {
    if (!byId[id]) {
      byId[id] = makeElement("div");
      byId[id].id = id;
    }
    return byId[id];
  },
  createElement(tag) {
    return makeElement(tag);
  },
  querySelectorAll() {
    return [];
  },
  addEventListener(name, callback) {
    (documentListeners[name] = documentListeners[name] || []).push(callback);
  }
};

const context = {
  document,
  console,
  JSON,
  Math,
  setTimeout(callback) {
    callback();
    return 0;
  },
  window: {
    coCreatorAction(payload) {
      const message = JSON.parse(payload);
      wireSent.push(message);
      const action = Object.assign({}, message);
      delete action.generation;
      sent.push(action);
    },
    addEventListener() {}
  }
};

["components/render.js", "components/controls.js", "script.js"].forEach(function (file) {
  vm.runInNewContext(
    fs.readFileSync(path.join(__dirname, file), "utf8"),
    context,
    { filename: file }
  );
});

function withText(root, text) {
  if (root.textContent === text) {
    return root;
  }
  for (const child of root.children) {
    const hit = withText(child, text);
    if (hit) {
      return hit;
    }
  }
  return null;
}

function findFeature(root, feature) {
  if (root.dataset && root.dataset.feature === String(feature)) {
    return root;
  }
  for (const child of root.children) {
    const hit = findFeature(child, feature);
    if (hit) {
      return hit;
    }
  }
  return null;
}

assert.strictEqual(typeof context.window.dispatchCoCreatorEvent, "function");
assert.strictEqual(typeof context.window.onCharacterCreatorShown, "function");

assert.deepStrictEqual(sent[0], { action: "getState" }, "view requests state on load");

context.window.dispatchCoCreatorEvent({
  type: "creatorState",
  generation: 7,
  menuOpen: true,
  available: true,
  isFemale: true,
  raceFormId: 0x00013746,
  height: 1.0,
  byCharGenFlag: 7,
  charGenHUDMode: 1,
  hairPartFormId: 0x00012345,
  facialHairPartFormId: 0,
  eyesPartFormId: 0x00012346,
  hairColorFormId: 0x00012347,
  facialHairColorFormId: 0,
  currentBoneId: 4294967295,
  characterPresetCount: 12,
  menuMode: 4,
  sculpting: false,
  dirty: true,
  morphWeight: { thin: 0.5, muscular: 0.25, large: 0.25 },
  bodyTintColor: { r: 1, g: 2, b: 3, a: 4 },
  tintCount: 3,
  headParts: [{ formId: 0x00012345, type: 3, editorId: "HairMale" }]
});

assert.ok(
  String(byId.generation.textContent).indexOf("7") !== -1,
  "session generation is rendered"
);
assert.ok(
  withText(byId["panel-head"], "0x00012345"),
  "hair part form id is rendered"
);
assert.ok(withText(byId["panel-overview"], "4 HAIR"), "live menu mode is rendered");
assert.ok(withText(byId["panel-overview"], "true"), "pending-change state is rendered");

context.window.dispatchCoCreatorEvent({
  type: "creatorState",
  generation: 7,
  menuOpen: true,
  available: true,
  currentBoneId: 5,
  menuMode: 1,
  sculpting: true,
  dirty: false
});

const regionLabel = withText(byId.controls, "Bone region: 0");
assert.ok(regionLabel, "bone region stepper is rendered");
const beforeRegion = sent.length;
regionLabel.parentNode.children[2].fire("click");
assert.strictEqual(wireSent[beforeRegion].generation, 7, "actions carry the last observed session generation");
assert.deepStrictEqual(
  sent[beforeRegion],
  { action: "setBoneRegion", value: 1 },
  "the region stepper sets the movie's own currentBoneRegionID property"
);
assert.deepStrictEqual(
  sent[beforeRegion + 1],
  { action: "getState" },
  "setting the region re-reads state"
);

const intensityLabel = withText(byId.controls, "Morph intensity: 0.0");
assert.ok(intensityLabel, "morph intensity stepper is rendered");
const beforeIntensity = sent.length;
intensityLabel.parentNode.children[2].fire("click");
assert.deepStrictEqual(
  sent[beforeIntensity],
  { action: "setMorphIntensity", value: 0.1 },
  "the morph stepper sends the signed preset intensity"
);
intensityLabel.parentNode.children[0].fire("click");
intensityLabel.parentNode.children[0].fire("click");
assert.deepStrictEqual(
  sent[sent.length - 1],
  { action: "setMorphIntensity", value: -0.1 },
  "the morph stepper steps downward within [-1,1]"
);

context.window.dispatchCoCreatorEvent({
  type: "creatorInput",
  control: "Accept",
  handled: false
});
assert.ok(
  byId.inputFeedback.classList.contains("is-rejected"),
  "unhandled control is marked rejected"
);

context.window.dispatchCoCreatorEvent({
  type: "creatorInput",
  control: "Accept",
  handled: true
});
assert.ok(
  !byId.inputFeedback.classList.contains("is-rejected"),
  "handled control clears the rejection mark"
);

context.window.onCharacterCreatorShown();
assert.deepStrictEqual(wireSent[wireSent.length - 1], { action: "getState" }, "show refreshes state without the previous token");
context.window.dispatchCoCreatorEvent({ type: "creatorState", generation: 7, menuOpen: true, available: false });
const hairButton = findFeature(byId.controls, 0);
assert.ok(hairButton, "feature button exists");

const beforeOptions = sent.length;
hairButton.fire("click");
assert.strictEqual(wireSent[beforeOptions].generation, 7, "option reads carry the refreshed session generation");
assert.deepStrictEqual(
  sent[beforeOptions],
  { action: "getOptions", feature: 0 },
  "feature button requests the engine option list"
);

context.window.dispatchCoCreatorEvent({
  type: "creatorOptions",
  feature: 0,
  groupIndex: 1,
  count: 2,
  options: [
    { index: 0, text: "Hair A", applied: false },
    { index: 1, text: "Hair B", applied: true }
  ]
});

const optionA = withText(byId.options, "  Hair A");
const optionB = withText(byId.options, "\u2022 Hair B");
assert.ok(optionA, "first option rendered");
assert.ok(optionB, "applied option rendered");
assert.ok(optionB.classList.contains("is-applied"), "applied option is marked");
assert.ok(!optionA.classList.contains("is-applied"), "unapplied option is not marked");

const beforeApply = sent.length;
optionA.fire("click");
assert.deepStrictEqual(
  sent[beforeApply],
  { action: "setHairStyle", value: 0 },
  "clicking an option applies its index"
);
assert.deepStrictEqual(
  sent[beforeApply + 1],
  { action: "getOptions", feature: 0, groupIndex: 1 },
  "applying re-reads the option list for the same group"
);
assert.deepStrictEqual(
  sent[beforeApply + 2],
  { action: "getState" },
  "applying re-reads the state"
);

findFeature(byId.controls, 2).fire("click");
const beforeStaleOption = sent.length;
optionA.fire("click");
assert.strictEqual(sent.length, beforeStaleOption, "an old hair option cannot issue a beard command");
context.window.dispatchCoCreatorEvent({
  type: "creatorOptions", feature: 0, groupIndex: 1, count: 1,
  options: [{ index: 0, text: "Delayed Hair", applied: false }]
});
assert.ok(!withText(byId.options, "  Delayed Hair"), "a delayed list cannot replace the selected feature");

const beforePad = sent.length;
context.window.dispatchCoCreatorEvent({
  type: "creatorController",
  button: "a",
  phase: "down"
});
assert.deepStrictEqual(
  sent[beforePad],
  { action: "input", control: "Accept" },
  "controller A maps onto the movie's Accept control"
);

const beforePadUp = sent.length;
context.window.dispatchCoCreatorEvent({
  type: "creatorController",
  button: "a",
  phase: "up"
});
assert.strictEqual(sent.length, beforePadUp, "button release is not forwarded");

const beforeUnknown = sent.length;
context.window.dispatchCoCreatorEvent({ type: "creatorSomethingElse" });
context.window.dispatchCoCreatorEvent(null);
assert.strictEqual(sent.length, beforeUnknown, "unknown payloads are ignored");

const morphButton = findFeature(byId.controls, 6);
const beforeMorphs = sent.length;
morphButton.fire("click");
assert.deepStrictEqual(
  sent[beforeMorphs],
  { action: "getOptions", feature: 6 },
  "morphs enumerate without an explicit group so the native side uses the live bone region"
);

context.window.dispatchCoCreatorEvent({
  type: "creatorOptions",
  feature: 6,
  groupIndex: 12,
  count: 1,
  options: [{ index: 0, text: "Preset A", applied: false }]
});
const beforePreset = sent.length;
withText(byId.options, "  Preset A").fire("click");
assert.deepStrictEqual(
  sent[beforePreset],
  { action: "setMorphPreset", value: 0 },
  "a morph option applies through ChangePreset"
);

const extrasButton = findFeature(byId.controls, 4);
const beforeExtras = sent.length;
extrasButton.fire("click");
assert.deepStrictEqual(
  sent[beforeExtras],
  { action: "getOptions", feature: 4, groupIndex: 4294967295 },
  "extras first lists its categories using the movie's own uint.MAX_VALUE group"
);

context.window.dispatchCoCreatorEvent({
  type: "creatorOptions",
  feature: 4,
  groupIndex: 4294967295,
  count: 2,
  options: [
    { index: 0, text: "Complexion", applied: false },
    { index: 1, text: "Dirt", applied: false }
  ]
});
const beforeCategory = sent.length;
withText(byId.options, "  Dirt").fire("click");
assert.deepStrictEqual(
  sent[beforeCategory],
  { action: "getOptions", feature: 4, groupIndex: 1 },
  "choosing an extras category re-enumerates that category by its list index"
);

context.window.dispatchCoCreatorEvent({
  type: "creatorOptions",
  feature: 4,
  groupIndex: 1,
  count: 1,
  options: [{ index: 3, text: "Dirt 3", applied: false }]
});
assert.ok(withText(byId.options, "BACK TO EXTRAS"), "extras entries offer a route back to categories");

const beforeTint = sent.length;
withText(byId.options, "  Dirt 3").fire("click");
assert.deepStrictEqual(
  sent[beforeTint],
  { action: "applyTint", group: 1, value: 3 },
  "an unapplied extra is applied with its category group and entry index"
);

context.window.dispatchCoCreatorEvent({
  type: "creatorOptions",
  feature: 4,
  groupIndex: 1,
  count: 1,
  options: [{ index: 3, text: "Dirt 3", applied: true }]
});
const beforeRemove = sent.length;
withText(byId.options, "\u2022 Dirt 3").fire("click");
assert.deepStrictEqual(
  sent[beforeRemove],
  { action: "removeTint", group: 1 },
  "clicking the applied extra removes it instead of re-applying"
);

const axesButton = withText(byId.controls, "LOAD AXES");
assert.ok(axesButton, "axes loader is rendered");
const beforeAxes = sent.length;
axesButton.fire("click");
assert.deepStrictEqual(sent[beforeAxes], { action: "getAxes" }, "axes loader requests the axis list");

context.window.dispatchCoCreatorEvent({
  type: "creatorAxes",
  boneIndex: 3,
  boneId: 17,
  count: 2,
  axes: [
    { slot: 0, axisIndex: 4, isSlider: true, isScalingAxis: false, sliderValue: 0.25 },
    { slot: 1, axisIndex: 6, isSlider: false, isScalingAxis: true, scalingValue: -0.5 }
  ]
});

const morphAxis = withText(byId.axes, "axis 4 morph: 0.25");
const scaleAxis = withText(byId.axes, "axis 6 scale: -0.50");
assert.ok(morphAxis, "morph axis is listed with its live value");
assert.ok(scaleAxis, "scaling axis is listed with its live value");

const beforeMorphAxis = sent.length;
morphAxis.parentNode.children[1].fire("click");
assert.deepStrictEqual(
  sent[beforeMorphAxis],
  { action: "setAxisMorph", axis: 4, value: 0.15 },
  "a morph axis steps through ChangeBoneRegionMorphSlider"
);

const beforeScaleAxis = sent.length;
scaleAxis.parentNode.children[2].fire("click");
assert.deepStrictEqual(
  sent[beforeScaleAxis],
  { action: "setAxisScale", axis: 6, value: -0.4 },
  "a scaling axis steps through ChangeBoneRegionAxis"
);

context.window.dispatchCoCreatorEvent({
  type: "creatorPresets",
  available: false,
  count: 0,
  presets: []
});
assert.ok(
  withText(byId.presets, "Presets unavailable: LooksMenu presets are an F4EE feature and the F4EE plugin is not loaded."),
  "missing F4EE is reported rather than shown as an empty preset list"
);

context.window.dispatchCoCreatorEvent({
  type: "creatorPresets",
  available: true,
  count: 1,
  presets: [{ name: "Vault Dweller.json", path: "Data/F4SE/Plugins/F4EE/Presets/Vault Dweller.json" }]
});
const presetRow = withText(byId.presets, "Vault Dweller.json");
assert.ok(presetRow, "preset is listed by name");
const beforePresetLoad = sent.length;
presetRow.fire("click");
assert.deepStrictEqual(
  sent[beforePresetLoad],
  {
    action: "loadPreset",
    path: "Data/F4SE/Plugins/F4EE/Presets/Vault Dweller.json"
  },
  "clicking a preset loads it by path"
);
assert.deepStrictEqual(
  sent[beforePresetLoad + 1],
  { action: "getState" },
  "loading a preset re-reads state"
);

const presetsLoader = withText(byId.controls, "LOAD PRESETS");
assert.ok(presetsLoader, "preset loader is rendered");
const beforePresets = sent.length;
presetsLoader.fire("click");
assert.deepStrictEqual(sent[beforePresets], { action: "getPresets" }, "preset loader requests the list");

findFeature(byId.controls, 2).fire("click");
context.window.dispatchCoCreatorEvent({
  type: "creatorOptions",
  feature: 2,
  groupIndex: 0,
  count: 0,
  options: []
});
assert.ok(
  withText(byId.options, "Engine returned no options for feature 2 (group 0)."),
  "empty option list is surfaced rather than treated as success"
);

context.window.onCharacterCreatorShown();
const beforeBootstrap = sent.length;
byId.confirm.fire("click");
assert.strictEqual(sent.length, beforeBootstrap, "show blocks mutations until refreshed state arrives");
context.window.dispatchCoCreatorEvent({ type: "creatorState", generation: 8, menuOpen: true, available: false });
byId.confirm.fire("click");
assert.deepStrictEqual(wireSent[wireSent.length - 1], { action: "confirm", generation: 8 }, "reopened session commands use the refreshed generation");

console.log("character creator view regression contracts passed");
