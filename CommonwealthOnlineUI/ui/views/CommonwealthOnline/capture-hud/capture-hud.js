(function () {
  "use strict";

  var STATES = ["neutral", "owned", "capturing", "contested"];
  // Constraint: noAttempt arrives when a stale heartbeat or cancel follows a finished capture; it is not a refusal to show.
  var IGNORED_CODE = "no_attempt";
  var COUNTDOWN_MS = 1000;
  var shown = null;
  var elapsed = 0;
  var timer = null;

  var REJECT_TEXT = {
    too_far: "Move closer to the workbench to capture it.",
    hold_limit: "You already hold the maximum number of settlements.",
    already_yours: "You already own this settlement.",
    unknown_settlement: "This settlement is unknown to the server.",
    workbench_intact: "Destroy the owner's workbench before you can capture this settlement.",
    timed_out: "Capture timed out. Press H to try again.",
    not_allowed: "You cannot capture this settlement right now."
  };

  var PROTECTION_LABEL = {
    settlementProtected: "Raid protected",
    ownerNewPlayer: "New-player protection"
  };

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

  function formatThousands(value) {
    return String(value).replace(/\B(?=(\d{3})+(?!\d))/g, ",");
  }

  function formatDuration(seconds) {
    if (seconds >= 3600) {
      var minutes = Math.floor((seconds % 3600) / 60);
      return Math.floor(seconds / 3600) + "h" + (minutes > 0 ? " " + minutes + "m" : "");
    }
    if (seconds >= 60) {
      return Math.floor(seconds / 60) + "m";
    }
    return seconds + "s";
  }

  function healthFor(percent) {
    if (percent >= 60) {
      return "ok";
    }
    return percent >= 30 ? "hurt" : "low";
  }

  function idleCapture() {
    return { active: false, state: "neutral", progressPercent: 0, secondsLeft: 0, settlementName: "", message: "", prompt: false };
  }

  function rejectText(plugin) {
    var result = plugin.lastResult;
    if (!isObject(result) || result.ok === true || result.code === IGNORED_CODE) {
      return "";
    }
    return Object.prototype.hasOwnProperty.call(REJECT_TEXT, result.code) ? REJECT_TEXT[result.code] : "Something went wrong.";
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

  function workbenchFrom(settlement) {
    var source = isObject(settlement) ? settlement : {};
    var maxHp = wholeOr0(source.workbenchMaxHp);
    var broken = source.workbenchBroken === true;
    var hp = broken ? 0 : Math.min(maxHp, wholeOr0(source.workbenchHp));
    return {
      present: maxHp > 0 || broken,
      hpPercent: maxHp > 0 ? Math.floor(hp * 100 / maxHp) : 0,
      hp: hp,
      maxHp: maxHp,
      broken: broken
    };
  }

  function protectionFrom(raid) {
    var source = isObject(raid) ? raid : {};
    var known = Object.prototype.hasOwnProperty.call(PROTECTION_LABEL, source.reason);
    var active = source.active === true;
    var secondsLeft = wholeOr0(source.secondsLeft);
    var label = known ? PROTECTION_LABEL[source.reason] : "Protected";
    return {
      active: active,
      reason: active ? (known ? source.reason : "unknown") : "none",
      secondsLeft: secondsLeft,
      text: active ? label + ": " + formatDuration(secondsLeft) + " left" : ""
    };
  }

  function selfProtectionFrom(plugin) {
    var source = isObject(plugin) && isObject(plugin.selfRaidProtection) ? plugin.selfRaidProtection : {};
    var active = source.active === true;
    var secondsLeft = wholeOr0(source.secondsLeft);
    return {
      active: active,
      secondsLeft: secondsLeft,
      text: active ? "Protected: " + formatDuration(secondsLeft) : ""
    };
  }

  function adaptPluginState(plugin) {
    var near = findNear(plugin);
    var selfProtection = selfProtectionFrom(plugin);
    if (!near) {
      return {
        capture: idleCapture(),
        workbench: workbenchFrom(null),
        protection: protectionFrom(null),
        selfProtection: selfProtection
      };
    }
    var state = STATES.indexOf(near.state) >= 0 ? near.state : "neutral";
    var capturing = state === "capturing" || state === "contested";
    return {
      capture: {
        active: capturing,
        state: state,
        progressPercent: percentOf(near.progressPercent),
        secondsLeft: wholeOr0(near.secondsLeft),
        settlementName: typeof near.name === "string" && near.name !== "" ? near.name : "Unnamed settlement",
        message: rejectText(plugin) || messageFor(state),
        prompt: plugin.promptCapture === true && !capturing
      },
      workbench: workbenchFrom(near),
      protection: protectionFrom(near.raidProtection),
      selfProtection: selfProtection
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
    var workbench = isObject(input) && isObject(input.workbench) ? input.workbench : {};
    var protection = isObject(input) && isObject(input.protection) ? input.protection : {};
    var selfProtection = isObject(input) && isObject(input.selfProtection) ? input.selfProtection : {};
    var state = STATES.indexOf(capture.state) >= 0 ? capture.state : "neutral";
    var percent = percentOf(capture.progressPercent);
    var hpPercent = percentOf(workbench.hpPercent);
    var hasWorkbench = workbench.present === true;
    var captureVisible = capture.active === true || capture.prompt === true || (typeof capture.message === "string" && capture.message !== "");
    return {
      visible: captureVisible || hasWorkbench || protection.active === true || selfProtection.active === true,
      active: capture.active === true,
      state: state,
      name: typeof capture.settlementName === "string" ? capture.settlementName : "",
      percent: percent,
      status: capture.prompt === true ? "Press H to capture" : statusFor(state, percent, wholeOr0(capture.secondsLeft)),
      message: typeof capture.message === "string" ? capture.message : "",
      workbench: {
        visible: hasWorkbench,
        bar: wholeOr0(workbench.maxHp) > 0,
        health: healthFor(hpPercent),
        percent: hpPercent,
        text: workbench.broken === true
          ? "Workbench destroyed"
          : "Workbench " + formatThousands(wholeOr0(workbench.hp)) + " / " + formatThousands(wholeOr0(workbench.maxHp))
      },
      protection: {
        visible: protection.active === true,
        text: typeof protection.text === "string" ? protection.text : ""
      },
      selfProtection: {
        visible: selfProtection.active === true,
        text: typeof selfProtection.text === "string" ? selfProtection.text : ""
      }
    };
  }

  function paint(view) {
    var root = document.getElementById("coCaptureHud");
    var name = document.getElementById("coCaptureName");
    var bar = document.getElementById("coCaptureBar");
    var fill = document.getElementById("coCaptureFill");
    var status = document.getElementById("coCaptureStatus");
    var message = document.getElementById("coCaptureMessage");
    var workbench = document.getElementById("coWorkbench");
    var workbenchLabel = document.getElementById("coWorkbenchLabel");
    var workbenchBar = document.getElementById("coWorkbenchBar");
    var workbenchFill = document.getElementById("coWorkbenchFill");
    var protection = document.getElementById("coProtection");
    var selfProtection = document.getElementById("coSelfProtection");
    if (!root || !name || !bar || !fill || !status || !message || !workbench || !workbenchLabel || !workbenchBar || !workbenchFill || !protection || !selfProtection) {
      return;
    }
    root.hidden = !view.visible;
    root.setAttribute("data-state", view.state);
    name.textContent = view.name;
    name.hidden = view.name === "";
    bar.hidden = !view.active;
    fill.style.width = view.percent + "%";
    bar.setAttribute("aria-valuenow", String(view.percent));
    status.textContent = view.status;
    status.hidden = view.name === "";
    message.textContent = view.message;
    message.hidden = view.message === "";
    workbench.hidden = !view.workbench.visible;
    workbench.setAttribute("data-health", view.workbench.health);
    workbenchLabel.textContent = view.workbench.text;
    workbenchBar.hidden = !view.workbench.bar;
    workbenchFill.style.width = view.workbench.percent + "%";
    workbenchBar.setAttribute("aria-valuenow", String(view.workbench.percent));
    protection.textContent = view.protection.text;
    protection.hidden = !view.protection.visible;
    selfProtection.textContent = view.selfProtection.text;
    selfProtection.hidden = !view.selfProtection.visible;
  }

  function dropAfter(protection, seconds) {
    if (!isObject(protection) || protection.active !== true) {
      return protection;
    }
    var left = wholeOr0(protection.secondsLeft) - seconds;
    return Object.assign({}, protection, { active: left > 0, secondsLeft: Math.max(0, left) });
  }

  function countedDown(plugin, seconds) {
    if (!isObject(plugin) || seconds <= 0) {
      return plugin;
    }
    return Object.assign({}, plugin, {
      selfRaidProtection: dropAfter(plugin.selfRaidProtection, seconds),
      settlements: Array.isArray(plugin.settlements) ? plugin.settlements.map(function (row) {
        return isObject(row) ? Object.assign({}, row, { raidProtection: dropAfter(row.raidProtection, seconds) }) : row;
      }) : plugin.settlements
    });
  }

  function hasProtection(plugin) {
    var view = adaptPluginState(plugin);
    return view.protection.active || view.selfProtection.active;
  }

  function paintCountdown() {
    paint(describeCapture(adaptPluginState(countedDown(shown, elapsed))));
  }

  function stopCountdown() {
    if (timer !== null) {
      window.clearInterval(timer);
      timer = null;
    }
  }

  function onCountdownTick() {
    elapsed += 1;
    paintCountdown();
    if (!hasProtection(countedDown(shown, elapsed))) {
      stopCountdown();
    }
  }

  window.coCaptureSetState = function (json) {
    var plugin = null;
    try {
      plugin = JSON.parse(json);
    } catch (error) {
      plugin = null;
    }
    shown = plugin;
    elapsed = 0;
    stopCountdown();
    paintCountdown();
    if (hasProtection(plugin)) {
      timer = window.setInterval(onCountdownTick, COUNTDOWN_MS);
    }
  };
  window.coCaptureHudModel = {
    adaptPluginState: adaptPluginState,
    describeCapture: describeCapture
  };
})();
