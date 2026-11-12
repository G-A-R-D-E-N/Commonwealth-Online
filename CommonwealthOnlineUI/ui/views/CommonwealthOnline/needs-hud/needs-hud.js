(function () {
  "use strict";

  var NEEDS = ["hunger", "thirst", "sleep"];
  var ROW_IDS = { hunger: "coNeedsHunger", thirst: "coNeedsThirst", sleep: "coNeedsSleep" };
  var TONES = ["ok", "dim", "warn", "crit"];
  var THRESHOLDS = [0.5, 0.75, 0.9];
  var FADE_THRESHOLD = 0.5;
  var CHANGE_EPSILON = 0.02;
  var FULL_MS = 4000;
  var IDLE_OPACITY = 0.8;
  var SCALE_REF_WIDTH = 1920;
  var SCALE_REF_HEIGHT = 1080;
  var SCALE_MIN = 1;
  var SCALE_MAX = 2;

  function isObject(value) {
    return !!value && typeof value === "object" && !Array.isArray(value);
  }

  function isUnit(value) {
    return typeof value === "number" && value >= 0 && value <= 1;
  }

  function parseState(json) {
    var state;
    try {
      state = JSON.parse(json);
    } catch (error) {
      return null;
    }
    if (!isObject(state) || typeof state.enabled !== "boolean") {
      return null;
    }
    var needs = {};
    for (var i = 0; i < NEEDS.length; i++) {
      if (!isUnit(state[NEEDS[i]])) {
        return null;
      }
      needs[NEEDS[i]] = state[NEEDS[i]];
    }
    return { enabled: state.enabled, needs: needs };
  }

  function satisfactionOf(need) {
    return 1 - need;
  }

  function uiScaleFor(width, height) {
    var w = typeof width === "number" && width > 0 ? width : SCALE_REF_WIDTH;
    var h = typeof height === "number" && height > 0 ? height : SCALE_REF_HEIGHT;
    var scale = Math.min(w / SCALE_REF_WIDTH, h / SCALE_REF_HEIGHT);
    return Math.max(SCALE_MIN, Math.min(SCALE_MAX, scale));
  }

  function applyUiScale() {
    var root = document && document.documentElement;
    if (!root || !root.style || typeof root.style.setProperty !== "function") {
      return;
    }
    root.style.setProperty("--co-ui-scale", String(uiScaleFor(window.innerWidth, window.innerHeight)));
  }

  function tierOf(need) {
    var tier = 0;
    for (var i = 0; i < THRESHOLDS.length; i++) {
      if (need >= THRESHOLDS[i]) {
        tier = i + 1;
      }
    }
    return tier;
  }

  function colorStateOf(need) {
    return TONES[tierOf(need)];
  }

  function nextFade(previous, state, now) {
    var changed = !previous || NEEDS.some(function (name) {
      var before = previous.needs[name];
      var after = state.needs[name];
      return Math.abs(after - before) >= CHANGE_EPSILON - Number.EPSILON || tierOf(after) !== tierOf(before);
    });
    var fullUntil = changed ? now + FULL_MS : previous.fullUntil;
    var high = NEEDS.some(function (name) {
      return state.needs[name] >= FADE_THRESHOLD;
    });
    var full = high || now < fullUntil;
    return { needs: state.needs, fullUntil: fullUntil, high: high, full: full, opacity: full ? 1 : IDLE_OPACITY };
  }

  var lastState = null;
  var previous = null;
  var timer = null;

  function paint(visible, opacity, needs) {
    var root = document.getElementById("coNeedsHud");
    if (!root) {
      return;
    }
    root.hidden = !visible;
    root.style.opacity = String(opacity);
    if (!needs) {
      return;
    }
    NEEDS.forEach(function (name) {
      var row = document.getElementById(ROW_IDS[name]);
      if (!row) {
        return;
      }
      row.setAttribute("data-tone", colorStateOf(needs[name]));
      row.style.setProperty("--co-needs-satisfaction", String(satisfactionOf(needs[name])));
    });
  }

  function render(now) {
    if (timer !== null) {
      clearTimeout(timer);
      timer = null;
    }
    if (!lastState || !lastState.enabled) {
      previous = null;
      paint(false, 1, null);
      return;
    }
    previous = nextFade(previous, lastState, now);
    paint(true, previous.opacity, previous.needs);
    if (previous.full && !previous.high) {
      timer = setTimeout(function () {
        timer = null;
        render(Date.now());
      }, previous.fullUntil - now);
    }
  }

  window.coNeedsSetState = function (json) {
    lastState = parseState(json);
    render(Date.now());
  };
  window.coNeedsHudModel = {
    parseState: parseState,
    satisfactionOf: satisfactionOf,
    uiScaleFor: uiScaleFor,
    colorStateOf: colorStateOf,
    nextFade: nextFade,
    FULL_MS: FULL_MS,
    IDLE_OPACITY: IDLE_OPACITY
  };

  applyUiScale();
  if (typeof window.addEventListener === "function") {
    window.addEventListener("resize", applyUiScale);
  }
})();
