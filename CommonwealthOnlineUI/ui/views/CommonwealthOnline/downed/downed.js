(function () {
  "use strict";

  var TICK_MS = 250;
  var MAX_MS = 600000;
  var FALLBACK_NAME = "A FRIEND";

  var root = null;
  var state = { phase: "hidden", remainingMs: 0, rescuer: null, reviving: null, prompt: null };
  var remainingMs = 0;
  var peakMs = 0;
  var lastStateKey = null;
  var timer = null;

  function esc(text) {
    return String(text == null ? "" : text)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function nameOr(value, fallback) {
    var trimmed = String(value == null ? "" : value).trim();
    return trimmed === "" ? fallback : trimmed;
  }

  function clampMs(value) {
    var number = Number(value);
    if (!isFinite(number) || number <= 0) {
      return 0;
    }
    return number >= MAX_MS ? MAX_MS : Math.floor(number);
  }

  function rescuerOf(value) {
    if (!value || typeof value !== "object" || Array.isArray(value)) {
      return null;
    }
    var progress = Number(value.progress);
    if (!isFinite(progress) || progress < 0) {
      progress = 0;
    } else if (progress > 1) {
      progress = 1;
    }
    return { name: String(value.name == null ? "" : value.name), progress: progress };
  }

  function normalize(raw) {
    var next = raw;
    if (typeof raw === "string") {
      try {
        next = JSON.parse(raw);
      } catch (err) {
        return null;
      }
    }
    if (!next || typeof next !== "object" || Array.isArray(next)) {
      return null;
    }
    var phase = next.phase === "downed" || next.phase === "dead" ? next.phase : "hidden";
    return {
      phase: phase,
      remainingMs: clampMs(next.remainingMs),
      rescuer: rescuerOf(next.rescuer),
      reviving: rescuerOf(next.reviving),
      prompt: rescuerOf(next.prompt)
    };
  }

  function remainingSeconds(ms) {
    return Math.max(0, Math.ceil(clampMs(ms) / 1000));
  }

  function formatSeconds(ms) {
    return String(remainingSeconds(ms));
  }

  function unitFor(ms) {
    return remainingSeconds(ms) === 1 ? "SECOND" : "SECONDS";
  }

  function percent(fraction) {
    var value = Math.max(0, Math.min(1, fraction));
    return Math.round(value * 100);
  }

  function fractionOf(ms) {
    return ms / (peakMs > 0 ? peakMs : MAX_MS);
  }

  function barHtml(fraction, isRevive, attribute) {
    return (
      '<span class="co-downed-bar' + (isRevive ? " is-revive" : "") + '">' +
      '<span class="co-downed-fill"' + (attribute || "") + ' style="width:' + percent(fraction) + '%"></span>' +
      "</span>"
    );
  }

  function promptHtml(prompt) {
    return (
      '<div class="co-downed-prompt" role="status" aria-live="polite">' +
      '<div class="co-downed-tag"><span class="co-downed-pulse-dot"></span>TEAMMATE DOWN · REVIVE PROTOCOL</div>' +
      '<p class="co-downed-prompt-title">HOLD <kbd>ACTIVATE</kbd> TO REVIVE ' +
      '<span class="co-downed-name">' +
      esc(nameOr(prompt.name, FALLBACK_NAME)) +
      "</span></p>" +
      barHtml(prompt.progress, true, "") +
      "</div>"
    );
  }

  function render(s, ms) {
    if (s.reviving) {
      return (
        '<div class="co-downed-card is-revive">' +
        '<div class="co-downed-tag"><span class="co-downed-pulse-dot"></span>MEDIC INCOMING · ADMINISTERING STIMPAK</div>' +
        '<p class="co-downed-title">REVIVING <span class="co-downed-name">' +
        esc(nameOr(s.reviving.name, FALLBACK_NAME)) +
        "</span></p>" +
        barHtml(s.reviving.progress, true, "") +
        '<p class="co-downed-hint">STAY CLOSE UNTIL THE STIMPAK LANDS</p>' +
        "</div>"
      );
    }
    if (s.phase === "hidden" && s.prompt) {
      return promptHtml(s.prompt);
    }
    if (s.phase !== "downed") {
      return "";
    }

    var footer = s.rescuer
      ? '<p class="co-downed-rescuer"><span class="co-downed-pulse-dot"></span>A FRIEND IS REVIVING YOU</p>' +
        barHtml(s.rescuer.progress, true, "")
      : '<p class="co-downed-hint">USE THE STIMPAK BUTTON TO GET UP</p>';
    return (
      '<div class="co-downed-card">' +
      '<div class="co-downed-tag"><span class="co-downed-pulse-dot co-downed-pulse-dot--danger"></span>CRITICAL VITALS · BLEEDOUT</div>' +
      '<p class="co-downed-title">YOU ARE UNCONSCIOUS FOR ' +
      '<span class="co-downed-clock" data-clock>' + formatSeconds(ms) + "</span> " +
      '<span class="co-downed-unit" data-unit>' + unitFor(ms) + "</span></p>" +
      barHtml(fractionOf(ms), false, " data-countdown") +
      footer +
      "</div>"
    );
  }

  function updateLive() {
    if (!root || !root.querySelector) {
      return;
    }
    var clock = root.querySelector("[data-clock]");
    if (clock) {
      clock.textContent = formatSeconds(remainingMs);
    }
    var unit = root.querySelector("[data-unit]");
    if (unit) {
      unit.textContent = unitFor(remainingMs);
    }
    var fill = root.querySelector("[data-countdown]");
    if (fill && fill.style) {
      fill.style.width = percent(fractionOf(remainingMs)) + "%";
      fill.style.background = "hsl(" + Math.round(percent(fractionOf(remainingMs)) * 1.2) + ", 75%, 45%)";
    }
  }

  function paint() {
    if (!root) {
      return;
    }
    root.innerHTML = render(state, remainingMs);
    updateLive();
  }

  function tick() {
    if (state.phase !== "downed") {
      return;
    }
    remainingMs = Math.max(0, remainingMs - TICK_MS);
    updateLive();
  }

  function syncTimer() {
    var wanted = state.phase === "downed" && remainingMs > 0 && !state.reviving;
    if (wanted && timer === null) {
      timer = setInterval(tick, TICK_MS);
    } else if (!wanted && timer !== null) {
      clearInterval(timer);
      timer = null;
    }
  }

  function setState(raw) {
    var next = normalize(raw);
    if (!next) {
      return false;
    }
    var key = JSON.stringify(next);
    state = next;
    remainingMs = next.remainingMs;
    peakMs = next.phase === "downed" ? Math.max(peakMs, next.remainingMs) : 0;
    if (key !== lastStateKey) {
      lastStateKey = key;
      paint();
    }
    syncTimer();
    return true;
  }

  function init() {
    root = document.getElementById("coDowned");
    if (!root) {
      return;
    }
    paint();
  }

  window.coDownedSetState = setState;
  window.coDownedModel = {
    normalize: normalize,
    formatSeconds: formatSeconds,
    unitFor: unitFor,
    render: render,
    tick: tick,
    maxMs: MAX_MS
  };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
