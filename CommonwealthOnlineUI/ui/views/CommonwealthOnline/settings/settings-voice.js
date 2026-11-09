(function () {
  "use strict";

  var KEYBIND_KEYS = ["voice.pushToTalkKey", "voice.whisperKey", "voice.shoutKey"];
  var SENSITIVITY_KEY = "voice.openMicSensitivity";
  var STATUS_KEY = "voice.status";
  var CAPTURE_PROMPT = "Press a key (Esc cancels)";
  var STATUS_TEXT = {
    ready: "Ready",
    transmitting: "Transmitting",
    idle: "Idle",
    serverNoVoice: "This server does not support voice chat",
    micFailed: "Microphone not available",
    micRecovering: "Reconnecting microphone",
    micSilentHint: "Your microphone may be muted in Windows",
    noOutput: "Voice playback failed: no audio output device",
    serverMuted: "You are muted by the server",
    rateLimited: "Voice is rate limited by the server",
    profileMismatch: "Voice profile mismatch with the server",
    deafened: "Deafened",
    micMuted: "Microphone muted",
    codecFailed: "Voice encoding failed; retry voice"
  };

  var controller = null;
  var capture = null;
  var backlog = window.CO_VoiceEvents || [];

  function sendAction(action) {
    if (typeof window.coVoiceAction !== "function") {
      console.warn("[CO Voice] coVoiceAction bridge unavailable:", action);
      return false;
    }
    window.coVoiceAction(JSON.stringify(action));
    return true;
  }

  function forwardSetting(key, value) {
    if (typeof key !== "string" || key.indexOf("voice.") !== 0 || key === STATUS_KEY) {
      return false;
    }
    sendAction({ op: "setSetting", key: key, value: value });
    return true;
  }

  function findRow(key) {
    var found = null;
    if (controller) {
      controller.getSettingsData().forEach(function (section) {
        section.items.forEach(function (item) {
          if (item.key === key) {
            found = item;
          }
        });
      });
    }
    return found;
  }

  function render() {
    if (controller) {
      controller.render();
    }
  }

  function valueFits(row, value) {
    if (row.type === "toggle") {
      return typeof value === "boolean";
    }
    if (row.type === "keybind") {
      return typeof value === "string";
    }
    return row.type === "slider" && typeof value === "number" && isFinite(value) && value >= row.min && value <= row.max;
  }

  function endCapture(restore) {
    var row = findRow(capture.key);
    if (row && restore) {
      row.value = capture.previous;
    }
    capture = null;
  }

  function beginKeyCapture(key) {
    if (KEYBIND_KEYS.indexOf(key) < 0 || !sendAction({ op: "beginKeyCapture", target: key })) {
      return false;
    }
    if (capture) {
      endCapture(true);
    }
    var row = findRow(key);
    capture = { key: key, previous: row ? row.value : null };
    if (row) {
      row.value = CAPTURE_PROMPT;
    }
    render();
    return true;
  }

  function applyKeyCapture(event) {
    if (KEYBIND_KEYS.indexOf(event.target) < 0) {
      return;
    }
    var captured = event.state === "captured" && typeof event.name === "string";
    if (!captured && event.state !== "cancelled") {
      return;
    }
    if (capture && capture.key === event.target) {
      endCapture(!captured);
    }
    var row = captured ? findRow(event.target) : null;
    if (row) {
      row.value = event.name;
    }
    render();
  }

  function applySettings(values) {
    Object.keys(values).forEach(function (key) {
      var row = key.indexOf("voice.") === 0 ? findRow(key) : null;
      if (row && valueFits(row, values[key])) {
        if (capture && capture.key === key) {
          capture.previous = values[key];
        } else {
          row.value = values[key];
        }
      }
    });
    render();
  }

  function statusText(event) {
    if (event.active !== true) {
      return "Not connected";
    }
    var text = Object.prototype.hasOwnProperty.call(STATUS_TEXT, event.code) ? STATUS_TEXT[event.code] : "Unknown voice state";
    return event.pending === true ? text + " (syncing)" : text;
  }

  function applyStatus(event) {
    var row = findRow(STATUS_KEY);
    if (row) {
      row.value = statusText(event);
    }
    render();
  }

  function percentOf(row, dbfs) {
    return Math.max(0, Math.min(100, ((dbfs - row.min) / (row.max - row.min)) * 100));
  }

  function drawMeter(dbfs) {
    var row = findRow(SENSITIVITY_KEY);
    var rowEl = document.querySelector('[data-setting-key="' + SENSITIVITY_KEY + '"]');
    if (!row || !rowEl) {
      return;
    }
    var meter = rowEl.querySelector(".settings-meter");
    if (!meter) {
      meter = document.createElement("span");
      meter.className = "settings-meter";
      rowEl.appendChild(meter);
    }
    meter.style.setProperty("--co-meter-level", percentOf(row, dbfs) + "%");
    meter.style.setProperty("--co-meter-threshold", percentOf(row, row.value) + "%");
  }

  function applyLevel(event) {
    if (typeof event.dbfs === "number" && isFinite(event.dbfs)) {
      drawMeter(event.dbfs);
    }
  }

  function handleEvent(event) {
    if (!controller) {
      if (event && typeof event === "object" && event.kind !== "level") {
        var kept = backlog.findIndex(function (entry) {
          return entry && entry.kind === event.kind;
        });
        backlog[kept < 0 ? backlog.length : kept] = event;
      }
      return;
    }
    if (!event || typeof event !== "object") {
      return;
    }
    if (event.kind === "settings" && event.values && typeof event.values === "object") {
      applySettings(event.values);
    } else if (event.kind === "status" && typeof event.active === "boolean") {
      applyStatus(event);
    } else if (event.kind === "keyCapture") {
      applyKeyCapture(event);
    } else if (event.kind === "level") {
      applyLevel(event);
    }
  }

  function attach(settingsController, bridge) {
    controller = settingsController;
    bridge.beginKeyCapture = beginKeyCapture;
    backlog.splice(0).forEach(handleEvent);
    var hideSettings = settingsController.hide;
    settingsController.hide = function () {
      if (capture) {
        endCapture(true);
      }
      return hideSettings.apply(settingsController, arguments);
    };
    ["back", "handleKeydown", "handleControllerInput"].forEach(function (name) {
      var navigate = settingsController[name];
      settingsController[name] = function () {
        return capture ? false : navigate.apply(settingsController, arguments);
      };
    });
  }

  window.CO_VoiceSettings = {
    forwardSetting: forwardSetting,
    beginKeyCapture: beginKeyCapture,
    attach: attach,
    handleEvent: handleEvent
  };
  window.coVoiceEvent = handleEvent;
})();
