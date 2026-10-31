const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const path = require("node:path");
const { createEnvironment, createElement } = require("../browser/tests/dom.js");

function audioSlider(id, label, value) {
  return { id, label, category: "audio", type: "float", value, minimum: 0, maximum: 1, step: 0.05,
    uiUsesPercent: true, supported: true, editable: true };
}

function qualitySetting(value) {
  return { id: "display.quality", label: "Quality", category: "display", type: "enum", value, supported: true, editable: true,
    options: [{ value: "low", label: "Low" }, { value: "medium", label: "Medium" }, { value: "high", label: "High" }] };
}

function mountSettings(items) {
  const { window, document } = createEnvironment();
  const mount = createElement("div");
  mount.ownerDocument = document;
  document.body.appendChild(mount);
  const calls = [];
  window.coSettingsAction = raw => calls.push(JSON.parse(raw));
  const context = vm.createContext(window);
  for (const file of ["settings-data.js", "settings-model.js", "settings-native.js", "settings-render.js", "settings-input.js", "settings.js"]) {
    vm.runInContext(fs.readFileSync(path.join(__dirname, file), "utf8"), context);
  }
  const settings = window.CO_Settings.create({ mount });
  settings.show();
  const gameRequest = calls.find(call => call.action === "settingsGetAll");
  settings.handleSettingsEvent({ type: "settingsResult", requestId: gameRequest.requestId,
    action: "settingsGetAll", success: true, data: { settings: items } });
  return { window, mount, calls, settings };
}

test("range slider change sends the scaled value", () => {
  const { mount, calls } = mountSettings([audioSlider("audio.master", "Master", 0.5)]);
  const slider = mount.querySelector(".settings-range");
  assert.ok(slider, "sliders must provide a real mouse-operable range input");
  slider.value = "75";
  slider.dispatchEvent({ type: "input", bubbles: true });
  assert.equal(mount.querySelector(".settings-slider__pct").textContent, "75%");
  slider.dispatchEvent({ type: "change", bubbles: true });
  assert.equal(calls.at(-1).action, "settingsSet");
  assert.equal(calls.at(-1).value, 0.75);
});

test("display name edits use the native profile action and accepted value", () => {
  const { window, mount, calls, settings } = mountSettings([audioSlider("audio.master", "Master", 0.5)]);
  const get = calls.find(call => call.action === "settingsGetMultiplayerProfile");
  assert.ok(get);
  window.dispatchCoSettingsEvent({ type: "settingsResult", requestId: get.requestId,
    action: get.action, success: true, data: { displayName: "Existing Name" } });
  mount.querySelector('[data-tab="multiplayer"]').dispatchEvent({ type: "click", bubbles: true });
  const input = mount.querySelector(".settings-text-input");
  assert.ok(input);
  assert.equal(input.value, "Existing Name");
  input.value = "New Name";
  input.dispatchEvent({ type: "change", bubbles: true });
  const save = calls.at(-1);
  assert.equal(save.action, "settingsSetDisplayName");
  assert.equal(save.value, "New Name");
  window.dispatchCoSettingsEvent({ type: "settingsResult", requestId: save.requestId,
    action: save.action, success: true, data: { displayName: "New Name" } });
  assert.equal(settings.getSettingsData()[0].items[0].value, "New Name");
  input.value = "Rejected";
  input.dispatchEvent({ type: "change", bubbles: true });
  window.dispatchCoSettingsEvent({ type: "settingsResult", requestId: calls.at(-1).requestId,
    action: save.action, success: false, error: { message: "Cannot save profile" } });
  assert.equal(settings.getSettingsData()[0].items[0].value, "New Name");
  assert.match(mount.querySelector("[data-co-settings-description]").textContent, /Cannot save profile/);
});

test("a failed display name save does not replace the settings list with a load error", () => {
  const { window, mount } = mountSettings([]);
  window.dispatchCoSettingsEvent({ type: "settingsResult", requestId: "save-name-1", action: "settingsSetDisplayName",
    success: false, error: { code: "NAME", message: "Name taken" } });
  assert.doesNotMatch(mount.querySelector("[data-co-game-settings-status]").textContent, /Failed to load settings/);
});

test("a profile reply keeps the display name text being typed", () => {
  const { window, mount, calls } = mountSettings([audioSlider("audio.master", "Master", 0.5)]);
  const get = calls.find(call => call.action === "settingsGetMultiplayerProfile");
  mount.querySelector('[data-tab="multiplayer"]').dispatchEvent({ type: "click", bubbles: true });
  const input = mount.querySelector(".settings-text-input");
  window.document.activeElement = input;
  input.value = "Typed";
  window.dispatchCoSettingsEvent({ type: "settingsResult", requestId: get.requestId,
    action: get.action, success: true, data: { displayName: "Existing Name" } });
  assert.equal(mount.querySelector(".settings-text-input").value, "Typed");
});

test("display names over 128 UTF-8 bytes are refused inline and not sent", () => {
  const { mount, calls } = mountSettings([audioSlider("audio.master", "Master", 0.5)]);
  mount.querySelector('[data-tab="multiplayer"]').dispatchEvent({ type: "click", bubbles: true });
  const input = mount.querySelector(".settings-text-input");
  input.value = "é".repeat(65);
  input.dispatchEvent({ type: "change", bubbles: true });
  assert.equal(calls.filter(call => call.action === "settingsSetDisplayName").length, 0);
  assert.match(mount.querySelector("[data-co-settings-description]").textContent, /128 bytes/);
  input.value = "é".repeat(64);
  input.dispatchEvent({ type: "change", bubbles: true });
  assert.equal(calls.at(-1).action, "settingsSetDisplayName");
});

test("ArrowDown on a focused slider moves the selected row without sending a value", () => {
  const { mount, calls, settings } = mountSettings([
    audioSlider("audio.master", "Master", 0.5),
    audioSlider("audio.music", "Music", 0.4)
  ]);
  const slider = mount.querySelector(".settings-range");
  slider.dispatchEvent({ type: "click", bubbles: true });
  let propagationStopped = false;
  const event = { type: "keydown", key: "ArrowDown", bubbles: true,
    preventDefault() {}, stopPropagation() { propagationStopped = true; } };
  slider.dispatchEvent(event);
  if (!propagationStopped) settings.handleKeydown(event);
  assert.equal(mount.querySelector(".settings-row.is-selected").getAttribute("data-setting-index"), "1");
  assert.equal(calls.some(call => call.action === "settingsSet"), false);
});


test("clicking a slider row moves the selection without replacing the row elements", () => {
  const { mount } = mountSettings([
    audioSlider("audio.master", "Master", 0.5),
    audioSlider("audio.music", "Music", 0.4)
  ]);
  const [first, second] = mount.querySelectorAll(".settings-row");
  second.dispatchEvent({ type: "click", bubbles: true });
  const rows = mount.querySelectorAll(".settings-row");
  assert.equal(rows[0], first);
  assert.equal(rows[1], second);
  assert.equal(second.classList.contains("is-selected"), true);
  assert.equal(first.classList.contains("is-selected"), false);
});

test("clicking a toggle row changes its pending value", () => {
  const { mount, calls } = mountSettings([{ id: "display.vsync", label: "VSync", category: "display", type: "boolean", value: false, supported: true, editable: true }]);
  mount.querySelector(".settings-row").dispatchEvent({ type: "click", bubbles: true });
  assert.ok(mount.querySelector(".settings-row.is-dirty"), "clicking a toggle row must change its value");
  assert.equal(calls.at(-1).action, "settingsSet");
  assert.equal(calls.at(-1).value, true);
});

test("clicking the left cycler arrow steps the value backward", () => {
  const { mount, calls } = mountSettings([qualitySetting("medium")]);
  mount.querySelector(".settings-cycler__arrow--prev").dispatchEvent({ type: "click", bubbles: true });
  assert.equal(calls.at(-1).action, "settingsSet");
  assert.equal(calls.at(-1).value, "low");
});

test("clicking a select label still steps the value forward", () => {
  const { mount, calls } = mountSettings([qualitySetting("medium")]);
  mount.querySelector(".settings-row__label").dispatchEvent({ type: "click", bubbles: true });
  assert.equal(calls.at(-1).value, "high");
});

test("apply sends settingsApply and clears CHANGED from the native snapshot", () => {
  const { window, mount, calls } = mountSettings([{ id: "display.vsync", label: "VSync", category: "display", type: "boolean", value: false, supported: true, editable: true }]);
  mount.querySelector(".settings-row").dispatchEvent({ type: "click", bubbles: true });
  const apply = mount.querySelector('[data-co-settings-action="apply"]');
  assert.equal(apply.classList.contains("is-idle"), false);
  apply.dispatchEvent({ type: "click", bubbles: true });
  const request = calls.at(-1);
  assert.equal(request.action, "settingsApply");
  window.dispatchCoSettingsEvent({ type: "settingsResult", requestId: request.requestId,
    action: request.action, success: true, data: { hasPendingChanges: false, settings: [
      { id: "display.vsync", label: "VSync", category: "display", type: "boolean", value: true, supported: true, editable: true, dirty: false }] } });
  assert.ok(!mount.querySelector(".settings-row.is-dirty"));
  assert.equal(apply.classList.contains("is-idle"), true);
});

function vsyncSetting(value) {
  return { id: "display.vsync", label: "VSync", category: "display", type: "boolean", value, supported: true, editable: true };
}

function respondToLastCall(window, calls, result) {
  const request = calls.at(-1);
  window.dispatchCoSettingsEvent(Object.assign({ type: "settingsResult", requestId: request.requestId, action: request.action }, result));
}

test("apply reports success after the snapshot and flashes the button", () => {
  const { window, mount, calls } = mountSettings([vsyncSetting(false)]);
  mount.querySelector(".settings-row").dispatchEvent({ type: "click", bubbles: true });
  const apply = mount.querySelector('[data-co-settings-action="apply"]');
  apply.dispatchEvent({ type: "click", bubbles: true });
  respondToLastCall(window, calls, { success: true, data: { settings: [Object.assign(vsyncSetting(true), { dirty: false })] } });
  const status = mount.querySelector("[data-co-game-settings-status]");
  assert.equal(status.textContent, "Settings applied.");
  assert.ok(status.classList.contains("is-success"));
  assert.ok(apply.classList.contains("is-confirmed"));
  window.__flushTimers();
  assert.equal(status.hidden, true);
  assert.equal(apply.classList.contains("is-confirmed"), false);
});

test("cancel reports discarded changes after the snapshot", () => {
  const { window, mount, calls } = mountSettings([vsyncSetting(false)]);
  mount.querySelector(".settings-row").dispatchEvent({ type: "click", bubbles: true });
  mount.querySelector('[data-co-settings-action="cancel"]').dispatchEvent({ type: "click", bubbles: true });
  assert.equal(calls.at(-1).action, "settingsCancel");
  respondToLastCall(window, calls, { success: true, data: { settings: [vsyncSetting(false)] } });
  assert.equal(mount.querySelector("[data-co-game-settings-status]").textContent, "Changes discarded.");
});

test("restore reports the restored values after the snapshot", () => {
  const { window, mount, calls } = mountSettings([vsyncSetting(false)]);
  mount.querySelector('[data-co-settings-action="restore"]').dispatchEvent({ type: "click", bubbles: true });
  assert.equal(calls.at(-1).action, "settingsResetAll");
  respondToLastCall(window, calls, { success: true, data: { settings: [vsyncSetting(false)] } });
  assert.equal(mount.querySelector("[data-co-game-settings-status]").textContent, "Restored values from when the menu opened.");
});

test("failed apply shows an error that survives the timer flush", () => {
  const { window, mount, calls } = mountSettings([vsyncSetting(false)]);
  mount.querySelector(".settings-row").dispatchEvent({ type: "click", bubbles: true });
  mount.querySelector('[data-co-settings-action="apply"]').dispatchEvent({ type: "click", bubbles: true });
  respondToLastCall(window, calls, { success: false, error: { code: "APPLY_FAILED", message: "Disk full" } });
  const status = mount.querySelector("[data-co-game-settings-status]");
  assert.equal(status.textContent, "Apply failed: Disk full");
  assert.ok(status.classList.contains("is-error"));
  window.__flushTimers();
  assert.equal(status.textContent, "Apply failed: Disk full");
});

test("apply and cancel are dimmed but stay clickable when nothing is pending", () => {
  const { mount } = mountSettings([vsyncSetting(false)]);
  const apply = mount.querySelector('[data-co-settings-action="apply"]');
  const cancel = mount.querySelector('[data-co-settings-action="cancel"]');
  assert.equal(apply.classList.contains("is-idle"), true);
  assert.equal(cancel.classList.contains("is-idle"), true);
  mount.querySelector(".settings-row").dispatchEvent({ type: "click", bubbles: true });
  assert.equal(apply.classList.contains("is-idle"), false);
  assert.equal(cancel.classList.contains("is-idle"), false);
});

test("apply and cancel with no pending changes send nothing and say so", () => {
  const { window, mount, calls } = mountSettings([vsyncSetting(false)]);
  const sent = calls.length;
  const status = mount.querySelector("[data-co-game-settings-status]");
  mount.querySelector('[data-co-settings-action="apply"]').dispatchEvent({ type: "click", bubbles: true });
  assert.equal(calls.length, sent);
  assert.equal(status.textContent, "No changes to apply.");
  assert.equal(status.classList.contains("is-error"), false);
  assert.equal(status.classList.contains("is-success"), false);
  mount.querySelector('[data-co-settings-action="cancel"]').dispatchEvent({ type: "click", bubbles: true });
  assert.equal(calls.length, sent);
  assert.equal(status.textContent, "No changes to discard.");
  window.__flushTimers();
  assert.equal(status.hidden, true);
});
