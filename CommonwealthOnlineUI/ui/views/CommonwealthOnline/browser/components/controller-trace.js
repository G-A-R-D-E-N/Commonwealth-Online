(function () {
  "use strict";

  var MAX_ENTRIES = 400;
  var PREFIX = "[CO][ctrl]";

  var entries = [];
  var sequence = 0;
  var enabled = false;

  function setEnabled(value) {
    enabled = value === true;
  }

  function isEnabled() {
    return enabled;
  }

  function keyboardTargetId() {
    if (!window.CO_Keyboard || typeof CO_Keyboard.getTarget !== "function") return null;
    var target = CO_Keyboard.getTarget();
    if (!target) return null;
    return target.id || target.tagName || null;
  }

  function activeElementId() {
    var doc = window.document;
    if (!doc || !doc.activeElement) return null;
    return doc.activeElement.id || doc.activeElement.tagName || null;
  }

  function keyboardIsOpen() {
    if (!window.CO_Keyboard || typeof CO_Keyboard.isOpen !== "function") return false;
    return !!CO_Keyboard.isOpen();
  }

  function snapshot(state) {
    if (!state) return null;
    return {
      activeTab: state.activeTab,
      focusZone: state.focusZone,
      selectedTabIndex: state.selectedTabIndex,
      selectedServerIndex: state.selectedServerIndex,
      selectedFilterIndex: state.selectedFilterIndex,
      directConnectFieldIndex: state.directConnectFieldIndex,
      hasJoinOverlay: !!state.joinOverlay,
      keyboardOpen: keyboardIsOpen(),
      keyboardTarget: keyboardTargetId(),
      activeElement: activeElementId()
    };
  }

  function record(stage, fields) {
    if (!enabled) return null;

    sequence += 1;
    var entry = { seq: sequence, stage: stage };
    var keys = Object.keys(fields || {});
    var parts = ["seq=" + sequence, "stage=" + stage];

    for (var i = 0; i < keys.length; i++) {
      var value = fields[keys[i]];
      entry[keys[i]] = value;
      parts.push(keys[i] + "=" + (value && typeof value === "object" ? JSON.stringify(value) : String(value)));
    }

    entries.push(entry);
    if (entries.length > MAX_ENTRIES) entries.shift();
    console.log(PREFIX + " " + parts.join(" "));
    return entry;
  }

  function action(stage, detail, button, state, dispatched) {
    return record(stage, {
      action: (detail && detail.action) || null,
      eventState: (detail && detail.state) || null,
      button: button || null,
      outcome: dispatched === undefined ? null : (dispatched ? "dispatched" : "ignored"),
      snapshot: snapshot(state)
    });
  }

  function enter(fn, button, state) {
    return record(fn + ":enter", { button: button || null, snapshot: snapshot(state) });
  }

  function exit(fn, button, state, outcome) {
    return record(fn + ":exit", {
      button: button || null,
      outcome: outcome === undefined ? null : outcome,
      snapshot: snapshot(state)
    });
  }

  window.CO_ControllerTrace = {
    setEnabled: setEnabled,
    isEnabled: isEnabled,
    trace: action,
    enter: enter,
    exit: exit,
    snapshot: snapshot,

    entries: function () {
      return entries.slice();
    },

    stages: function () {
      return entries.map(function (entry) {
        return entry.stage;
      });
    }
  };
})();
