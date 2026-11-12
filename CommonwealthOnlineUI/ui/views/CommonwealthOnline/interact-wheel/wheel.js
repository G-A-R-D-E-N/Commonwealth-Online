(function () {
  "use strict";

  var KNOWN = {
    addFriend: true,
    acceptFriend: true,
    requestSent: true,
    removeFriend: true,
    invite: true,
    mute: true,
    unmute: true,
    trade: true,
    tradeAccept: true,
    tradeDecline: true,
    tradeConfirm: true,
    tradeCancel: true,
    close: true
  };
  var DEADZONE = 0.35;
  var ICONS = {
    addFriend: '<circle cx="9" cy="8" r="3"/><path d="M3 20v-2a6 6 0 0 1 12 0v2M19 8v6m-3-3h6"/>',
    acceptFriend: '<circle cx="9" cy="8" r="3"/><path d="M3 20v-2a6 6 0 0 1 12 0v2m1-9 2 2 4-4"/>',
    requestSent: '<circle cx="9" cy="8" r="3"/><path d="M3 20v-2a6 6 0 0 1 12 0v2m3-12v3l2 1"/><circle cx="18" cy="11" r="4"/>',
    removeFriend: '<circle cx="9" cy="8" r="3"/><path d="M3 20v-2a6 6 0 0 1 12 0v2m1-9h6"/>',
    invite: '<circle cx="9" cy="8" r="3"/><path d="M3 20v-2a6 6 0 0 1 12 0v2m1-8h6m-3-3 3 3-3 3"/>',
    mute: '<path d="M4 10v4h4l5 4V6L8 10H4zm12-1 5 6m0-6-5 6"/>',
    unmute: '<path d="M4 10v4h4l5 4V6L8 10H4zm12-1a5 5 0 0 1 0 6m3-9a9 9 0 0 1 0 12"/>',
    trade: '<path d="M4 8h15m-4-4 4 4-4 4M20 16H5m4-4-4 4 4 4"/>',
    tradeAccept: '<path d="M4 8h10m-3-3 3 3-3 3M4 16h7m4 0 2 2 4-5"/>',
    tradeDecline: '<path d="M4 8h10m-3-3 3 3-3 3M4 16h7m5-2 5 5m0-5-5 5"/>',
    tradeConfirm: '<path d="m4 12 5 5L20 6"/>',
    tradeCancel: '<path d="M5 5l14 14M19 5 5 19"/>',
    close: '<path d="M6 6l12 12M18 6 6 18"/>'
  };

  function normalize(raw) {
    var state = raw;
    if (typeof state === "string") {
      try {
        state = JSON.parse(state);
      } catch (err) {
        state = null;
      }
    }
    state = state && typeof state === "object" ? state : {};
    var title = typeof state.title === "string" && state.title.length > 0 && state.title.length <= 128 ? state.title : "Unknown player";
    var slices = [];
    if (Array.isArray(state.slices)) {
      for (var i = 0; i < state.slices.length && slices.length < 8; i += 1) {
        var slice = state.slices[i];
        if (!slice || typeof slice !== "object") continue;
        var id = typeof slice.id === "string" ? slice.id : "";
        var label = typeof slice.label === "string" ? slice.label : "";
        if (!Object.prototype.hasOwnProperty.call(KNOWN, id) || label.length === 0 || label.length > 48) continue;
        slices.push({ id: id, label: label, disabled: slice.disabled === true });
      }
    }
    return { title: title, slices: slices };
  }

  function angleForPoint(dx, dy) {
    if (typeof dx !== "number" || typeof dy !== "number" || !isFinite(dx) || !isFinite(dy)) return 0;
    var degrees = (Math.atan2(dx, -dy) * 180) / Math.PI;
    return (degrees + 360) % 360;
  }

  function sliceIndexForAngle(count, angle) {
    if (!(count > 0) || typeof angle !== "number" || !isFinite(angle)) return -1;
    var step = 360 / count;
    var wrapped = ((angle % 360) + 360) % 360;
    return Math.floor(((wrapped + step / 2) % 360) / step);
  }

  function sliceIndexForAxes(count, x, y, deadzone) {
    var limit = typeof deadzone === "number" && deadzone >= 0 && deadzone < 1 ? deadzone : DEADZONE;
    if (typeof x !== "number" || typeof y !== "number" || !isFinite(x) || !isFinite(y)) return -1;
    if (Math.sqrt(x * x + y * y) < limit) return -1;
    return sliceIndexForAngle(count, angleForPoint(x, -y));
  }

  function enabledIndex(state, index) {
    var slices = state && Array.isArray(state.slices) ? state.slices : [];
    if (index < 0 || index >= slices.length) return -1;
    return slices[index].disabled ? -1 : index;
  }

  function stepIndex(state, index, direction) {
    var slices = state && Array.isArray(state.slices) ? state.slices : [];
    if (slices.length === 0) return -1;
    var next = index;
    for (var attempt = 0; attempt < slices.length; attempt += 1) {
      next = (next + direction + slices.length) % slices.length;
      if (!slices[next].disabled) return next;
    }
    return -1;
  }

  var state = { title: "Unknown player", slices: [] };
  var selected = -1;
  var root = null;
  var ring = null;
  var titleNode = null;

  function esc(value) {
    return String(value).replace(/[&<>"']/g, function (ch) {
      return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[ch];
    });
  }

  function point(radius, angle) {
    var radians = angle * Math.PI / 180;
    return [250 + radius * Math.sin(radians), 250 - radius * Math.cos(radians)];
  }

  function sectorPath(index, count) {
    var step = 360 / count;
    var start = index * step - step / 2 + 1;
    var end = index * step + step / 2 - 1;
    var outerStart = point(232, start);
    var outerEnd = point(232, end);
    var innerEnd = point(117, end);
    var innerStart = point(117, start);
    var large = end - start > 180 ? 1 : 0;
    return 'M ' + outerStart.join(' ') + ' A 232 232 0 ' + large + ' 1 ' + outerEnd.join(' ') +
      ' L ' + innerEnd.join(' ') + ' A 117 117 0 ' + large + ' 0 ' + innerStart.join(' ') + ' Z';
  }

  function paint() {
    if (!ring || !titleNode) return;
    titleNode.textContent = state.title;
    if (state.slices.length === 0) {
      ring.innerHTML = "";
      return;
    }
    var step = 360 / state.slices.length;
    var paths = [];
    var buttons = [];
    for (var i = 0; i < state.slices.length; i += 1) {
      var slice = state.slices[i];
      var angle = i * step;
      var position = point(174, angle);
      var selectedClass = i === selected ? " is-selected" : "";
      var disabledClass = slice.disabled ? " is-disabled" : "";
      paths.push('<path class="wheel-sector wheel-slice' + selectedClass + disabledClass + '" data-index="' + i + '" d="' + sectorPath(i, state.slices.length) + '"/>');
      buttons.push(
        '<button type="button" class="wheel-slice' +
          selectedClass + disabledClass +
          ' wheel-action" role="menuitem" aria-label="' + esc(slice.label) + '" data-index="' + i + '" style="left:' + (position[0] / 5) + '%;top:' + (position[1] / 5) + '%"' +
          (slice.disabled ? " disabled" : "") +
          '><svg viewBox="0 0 24 24" aria-hidden="true">' + ICONS[slice.id] + '</svg></button>'
      );
    }
    var current = selected >= 0 && state.slices[selected] ? state.slices[selected].label : "Choose an action";
    ring.innerHTML = '<svg class="wheel-sectors" viewBox="0 0 500 500" aria-hidden="true">' + paths.join("") + '</svg>' + buttons.join("") +
      '<div class="wheel-center"><span class="wheel-center-mark">CO</span><span class="wheel-center-label">' + esc(current) + '</span></div>';
  }

  function setState(raw) {
    var hadFocus = ring && typeof document !== "undefined" && ring.contains(document.activeElement);
    var next = normalize(raw);
    state = next;
    if (enabledIndex(state, selected) < 0) {
      selected = enabledIndex(state, 0);
      if (selected < 0) selected = stepIndex(state, -1, 1);
    }
    paint();
    if (hadFocus) focusSelected();
  }

  function send(op) {
    if (typeof window.coInteractWheelAction !== "function") return;
    window.coInteractWheelAction(JSON.stringify({ op: op }));
  }

  function activate() {
    if (selected < 0 || selected >= state.slices.length) return;
    var slice = state.slices[selected];
    if (slice.disabled) return;
    send(slice.id);
  }

  function focusSelected() {
    var button = ring && ring.querySelector('.wheel-action[data-index="' + selected + '"]');
    if (button) button.focus();
  }

  function select(index, focus) {
    if (index < 0 || index >= state.slices.length) return;
    if (state.slices[index].disabled) return;
    if (index === selected) return;
    selected = index;
    paint();
    if (focus) focusSelected();
  }

  function handleController(button, phase) {
    if (phase !== "down") return;
    if (button === "b") {
      send("close");
    } else if (button === "a") {
      activate();
    } else if (button === "dpad_right" || button === "rb") {
      select(stepIndex(state, selected, 1));
    } else if (button === "dpad_left" || button === "lb") {
      select(stepIndex(state, selected, -1));
    } else if (button === "dpad_up") {
      select(stepIndex(state, selected, -1));
    } else if (button === "dpad_down") {
      select(stepIndex(state, selected, 1));
    }
  }

  function handleAxes(x, y) {
    var index = sliceIndexForAxes(state.slices.length, x, y);
    if (index >= 0) select(index);
  }

  function init() {
    root = document.getElementById("coWheel");
    ring = document.getElementById("coWheelRing");
    titleNode = document.getElementById("coWheelTitle");
    if (!root || !ring) return;
    ring.addEventListener("click", function (event) {
      var target = event.target.closest(".wheel-slice");
      if (!target) return;
      var index = Number(target.getAttribute("data-index"));
      if (!isFinite(index) || !state.slices[index] || state.slices[index].disabled) return;
      select(index);
      activate();
    });
    ring.addEventListener("mousemove", function (event) {
      var target = event.target.closest(".wheel-slice");
      if (!target) return;
      var index = Number(target.getAttribute("data-index"));
      if (!isFinite(index)) return;
      select(index);
    });
    ring.addEventListener("focusin", function (event) {
      var target = event.target.closest(".wheel-action");
      if (!target) return;
      var index = Number(target.getAttribute("data-index"));
      if (isFinite(index)) select(index, true);
    });
    document.addEventListener("keydown", function (event) {
      if (event.key === "Escape") {
        send("close");
      } else if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        activate();
      } else if (event.key === "ArrowRight" || event.key === "ArrowDown") {
        event.preventDefault();
        select(stepIndex(state, selected, 1), true);
      } else if (event.key === "ArrowLeft" || event.key === "ArrowUp") {
        event.preventDefault();
        select(stepIndex(state, selected, -1), true);
      }
    });
    paint();
  }

  window.coInteractWheelSetState = setState;
  window.coInteractWheelController = handleController;
  window.coInteractWheelAxes = handleAxes;
  window.coWheelModel = {
    normalize: normalize,
    angleForPoint: angleForPoint,
    sliceIndexForAngle: sliceIndexForAngle,
    sliceIndexForAxes: sliceIndexForAxes,
    enabledIndex: enabledIndex,
    stepIndex: stepIndex
  };

  if (typeof document !== "undefined") {
    if (document.readyState === "loading") {
      document.addEventListener("DOMContentLoaded", init);
    } else {
      init();
    }
  }
})();
