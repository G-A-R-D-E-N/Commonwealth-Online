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
    close: true
  };
  var DEADZONE = 0.35;

  function normalize(raw) {
    var state = raw && typeof raw === "object" ? raw : {};
    var title = typeof state.title === "string" && state.title.length > 0 && state.title.length <= 128 ? state.title : "Unknown player";
    var slices = [];
    if (Array.isArray(state.slices)) {
      for (var i = 0; i < state.slices.length && slices.length < 8; i += 1) {
        var slice = state.slices[i];
        if (!slice || typeof slice !== "object") continue;
        var id = typeof slice.id === "string" ? slice.id : "";
        var label = typeof slice.label === "string" ? slice.label : "";
        if (!KNOWN[id] || label.length === 0 || label.length > 48) continue;
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

  function paint() {
    if (!ring || !titleNode) return;
    titleNode.textContent = state.title;
    if (state.slices.length === 0) {
      ring.innerHTML = "";
      return;
    }
    var step = 360 / state.slices.length;
    var html = [];
    for (var i = 0; i < state.slices.length; i += 1) {
      var slice = state.slices[i];
      var angle = i * step;
      html.push(
        '<button type="button" class="wheel-slice' +
          (i === selected ? " is-selected" : "") +
          (slice.disabled ? " is-disabled" : "") +
          '" role="menuitem" data-index="' + i + '" style="transform: rotate(' + angle + 'deg) translateY(-7.5rem) rotate(' + -angle + 'deg)"' +
          (slice.disabled ? " disabled" : "") +
          ">" + esc(slice.label) + "</button>"
      );
    }
    ring.innerHTML = html.join("");
  }

  function setState(raw) {
    var next = normalize(raw);
    state = next;
    if (selected < 0 || selected >= state.slices.length) {
      selected = enabledIndex(state, 0);
      if (selected < 0) selected = state.slices.length > 0 ? 0 : -1;
    }
    paint();
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

  function select(index) {
    if (index < 0 || index >= state.slices.length) return;
    if (state.slices[index].disabled) return;
    selected = index;
    paint();
  }

  function handleController(button, phase) {
    if (phase !== "press") return;
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
      if (!isFinite(index)) return;
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
    document.addEventListener("keydown", function (event) {
      if (event.key === "Escape") {
        send("close");
      } else if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        activate();
      } else if (event.key === "ArrowRight" || event.key === "ArrowDown") {
        select(stepIndex(state, selected, 1));
      } else if (event.key === "ArrowLeft" || event.key === "ArrowUp") {
        select(stepIndex(state, selected, -1));
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
