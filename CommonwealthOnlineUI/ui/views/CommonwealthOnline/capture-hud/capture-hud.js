(function () {
  "use strict";

  var STATES = ["neutral", "owned", "capturing", "contested"];

  function isObject(value) {
    return !!value && typeof value === "object" && !Array.isArray(value);
  }

  function isBuildZoneId(value) {
    return Number.isInteger(value) && value >= 0;
  }

  function wholeOr0(value) {
    return typeof value === "number" && Number.isFinite(value) && value > 0 ? Math.floor(value) : 0;
  }

  function percentOf(value) {
    return Math.min(100, wholeOr0(value));
  }

  function idleCapture() {
    return { active: false, state: "neutral", progressPercent: 0, secondsLeft: 0, settlementName: "", message: "" };
  }

  function messageFor(state) {
    if (state === "capturing") {
      return "Stay in range to finish the capture.";
    }
    if (state === "contested") {
      return "A rival is in range. Progress is paused.";
    }
    return "";
  }

  function findNear(plugin) {
    if (!isObject(plugin) || !Array.isArray(plugin.settlements) || !isBuildZoneId(plugin.nearBuildZoneId)) {
      return null;
    }
    return plugin.settlements.find(function (settlement) {
      return isObject(settlement) && settlement.buildZoneId === plugin.nearBuildZoneId;
    }) || null;
  }

  function adaptPluginState(plugin) {
    var near = findNear(plugin);
    if (!near) {
      return { capture: idleCapture() };
    }
    var local = percentOf(plugin.localProgress);
    var state = STATES.indexOf(near.state) >= 0 ? near.state : "neutral";
    if (local > 0 && state !== "capturing" && state !== "contested") {
      state = "capturing";
    }
    var capturing = state === "capturing" || state === "contested";
    return {
      capture: {
        active: capturing,
        state: state,
        progressPercent: local > 0 ? local : percentOf(near.progressPercent),
        secondsLeft: wholeOr0(near.secondsLeft),
        settlementName: "Settlement " + near.buildZoneId,
        message: messageFor(state)
      }
    };
  }

  function statusFor(state, percent, secondsLeft) {
    if (state === "capturing") {
      return "Being captured " + percent + "%" + (secondsLeft > 0 ? " - " + secondsLeft + "s left" : "");
    }
    if (state === "contested") {
      return "Contested at " + percent + "%";
    }
    return state === "owned" ? "Owned" : "Unclaimed";
  }

  function describeCapture(input) {
    var capture = isObject(input) && isObject(input.capture) ? input.capture : {};
    var state = STATES.indexOf(capture.state) >= 0 ? capture.state : "neutral";
    var percent = percentOf(capture.progressPercent);
    return {
      visible: capture.active === true,
      state: state,
      name: typeof capture.settlementName === "string" ? capture.settlementName : "",
      percent: percent,
      status: statusFor(state, percent, wholeOr0(capture.secondsLeft)),
      message: typeof capture.message === "string" ? capture.message : ""
    };
  }

  function paint(view) {
    var root = document.getElementById("coCaptureHud");
    var name = document.getElementById("coCaptureName");
    var bar = document.getElementById("coCaptureBar");
    var fill = document.getElementById("coCaptureFill");
    var status = document.getElementById("coCaptureStatus");
    var message = document.getElementById("coCaptureMessage");
    if (!root || !name || !bar || !fill || !status || !message) {
      return;
    }
    root.hidden = !view.visible;
    root.setAttribute("data-state", view.state);
    name.textContent = view.name;
    fill.style.width = view.percent + "%";
    bar.setAttribute("aria-valuenow", String(view.percent));
    status.textContent = view.status;
    message.textContent = view.message;
    message.hidden = view.message === "";
  }

  window.coCaptureSetState = function (json) {
    var plugin = null;
    try {
      plugin = JSON.parse(json);
    } catch (error) {
      plugin = null;
    }
    paint(describeCapture(adaptPluginState(plugin)));
  };
  window.coCaptureHudModel = {
    adaptPluginState: adaptPluginState,
    describeCapture: describeCapture
  };
})();
