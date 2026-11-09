(function () {
  "use strict";

  var NOTE_MAX = 200;
  var REASONS = [["harassment", "Harassment"], ["cheating", "Cheating"], ["griefing", "Griefing"], ["other", "Other"]];
  var TABS = [["player", "Report a player"], ["support", "Get admin help"]];
  var SENT = "Thanks. Your report was sent.";
  var FALLBACK = "The report could not be sent.";
  var ERRORS = {
    invalidKind: "That player is not in this session.",
    invalidTarget: "That player is not in this session.",
    targetUnavailable: "That player is not in this session.",
    selfTarget: "You cannot report yourself.",
    invalidReason: "Pick a reason.",
    noteRequired: "Describe the problem first.",
    noteTooLong: "Keep the note to 200 UTF-8 bytes or less.",
    unsafeText: "The note has characters that cannot be sent.",
    rateLimited: "You sent too many reports. Try again in a minute."
  };

  var root = null;
  var mode = "player";
  var players = [];
  var draft = { playerId: "", reason: "harassment", playerNote: "", supportNote: "" };
  var pending = false;
  var pendingMode = "";
  var result = null;
  var focusIndex = 0;

  function esc(text) {
    return String(text == null ? "" : text)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function sentenceFor(error) {
    return Object.prototype.hasOwnProperty.call(ERRORS, error) ? ERRORS[error] : FALLBACK;
  }

  function hasPlayer(playerId) {
    return players.some(function (player) {
      return String(player.playerId) === String(playerId);
    });
  }

  function trimNote(note) {
    return note.replace(/^[ \t\r\n]+|[ \t\r\n]+$/g, "");
  }

  function noteBytes(note) {
    try {
      return encodeURIComponent(trimNote(note)).replace(/%[0-9A-F]{2}/g, "x").length;
    } catch (_) {
      return -1;
    }
  }

  function validate(kind, form) {
    var note = typeof form.note === "string" ? form.note : "";
    if (noteBytes(note) < 0) {
      return "unsafeText";
    }
    if (kind === "player") {
      if (!hasPlayer(form.playerId)) {
        return "targetUnavailable";
      }
      if (!REASONS.some(function (reason) { return reason[0] === form.reason; })) {
        return "invalidReason";
      }
      return noteBytes(note) > NOTE_MAX ? "noteTooLong" : "";
    }
    if (kind === "support") {
      if (!trimNote(note)) {
        return "noteRequired";
      }
      return noteBytes(note) > NOTE_MAX ? "noteTooLong" : "";
    }
    return "invalidKind";
  }

  function actionFor(kind, form) {
    if (kind === "player") {
      return { action: "submit", kind: "player", targetPlayerId: Number(form.playerId), reason: form.reason, note: trimNote(form.note) };
    }
    return { action: "submit", kind: "support", note: trimNote(form.note) };
  }

  function send(kind, form) {
    if (pending) {
      return false;
    }
    var error = validate(kind, form);
    if (error) {
      result = { mode: kind, ok: false, text: sentenceFor(error) };
      paint();
      return false;
    }
    pending = true;
    pendingMode = kind;
    result = null;
    if (typeof window.coInfoAction === "function") {
      window.coInfoAction(JSON.stringify(actionFor(kind, form)));
    }
    paint();
    return true;
  }

  function submit() {
    if (mode === "player") {
      send("player", { playerId: draft.playerId, reason: draft.reason, note: draft.playerNote });
    } else {
      send("support", { note: draft.supportNote });
    }
  }

  function close() {
    if (typeof window.coInfoAction === "function") {
      window.coInfoAction(JSON.stringify({ action: "close" }));
    }
  }

  function setState(payload) {
    players = payload && Array.isArray(payload.players)
      ? payload.players.filter(function (player) { return player && typeof player.name === "string"; })
      : [];
    if (!hasPlayer(draft.playerId)) {
      draft.playerId = players.length ? String(players[0].playerId) : "";
    }
    paint();
  }

  function onEvent(event) {
    if (!event || event.type !== "reportResult") {
      return;
    }
    pending = false;
    result = { mode: pendingMode, ok: event.ok === true, text: event.ok === true ? SENT : sentenceFor(event.error) };
    paint();
  }

  function field(label, control) {
    return '<label class="info-field"><span class="info-label">' + esc(label) + "</span>" + control + "</label>";
  }

  function noteField(name, label, value) {
    return field(label,
      '<textarea class="info-input info-note" data-field="' + name + '">' + esc(value) + "</textarea>" +
      '<span class="info-counter" data-counter="' + name + '">' + (noteBytes(value) < 0 ? "Invalid text" : noteBytes(value)) + " / " + NOTE_MAX + " bytes" + "</span>");
  }

  function submitButton(label, blocked) {
    return '<button type="button" class="info-btn info-submit" data-action="submit"' +
      (pending || blocked ? " disabled" : "") + ">" + esc(label) + "</button>";
  }

  function playerSection() {
    var picker = players.length
      ? '<select class="info-input" data-field="playerId">' + players.map(function (player) {
        var id = String(player.playerId);
        return '<option value="' + esc(id) + '"' + (id === draft.playerId ? " selected" : "") + ">" + esc(player.name) + "</option>";
      }).join("") + "</select>"
      : '<p class="info-line">No other players are in this session</p>';
    var reasons = '<select class="info-input" data-field="reason">' + REASONS.map(function (reason) {
      return '<option value="' + reason[0] + '"' + (reason[0] === draft.reason ? " selected" : "") + ">" + reason[1] + "</option>";
    }).join("") + "</select>";
    return '<section class="info-section">' + field("Player", picker) + field("Reason", reasons) +
      noteField("playerNote", "Note (optional)", draft.playerNote) + submitButton("Send report", !players.length) + "</section>";
  }

  function supportSection() {
    return '<section class="info-section">' + noteField("supportNote", "What do you need help with?", draft.supportNote) +
      submitButton("Send to admins", false) + "</section>";
  }

  function tabs() {
    return '<div class="info-segment" role="group" aria-label="Report type">' + TABS.map(function (tab) {
      return '<button type="button" class="info-btn" data-tab="' + tab[0] + '" aria-pressed="' + (mode === tab[0] ? "true" : "false") + '">' +
        esc(tab[1]) + "</button>";
    }).join("") + "</div>";
  }

  function statusLine() {
    var shown = result !== null && result.mode === mode;
    return '<p class="info-status' + (shown && !result.ok ? " is-error" : "") + '" role="status">' + (shown ? esc(result.text) : "") + "</p>";
  }

  function renderReport() {
    return '<section class="info-panel"><header class="info-head"><h1 class="info-title">REPORT</h1>' +
      '<button type="button" class="info-btn" data-action="close" aria-label="Close report">Close</button>' +
      "</header>" + tabs() + (mode === "player" ? playerSection() : supportSection()) + statusLine() + "</section>";
  }

  function paint() {
    if (root) {
      root.innerHTML = renderReport();
    }
  }

  function onField(event) {
    var target = event.target;
    var name = target && target.getAttribute ? target.getAttribute("data-field") : null;
    if (!name || !Object.prototype.hasOwnProperty.call(draft, name)) {
      return;
    }
    draft[name] = String(target.value);
    var counter = root.querySelector('[data-counter="' + name + '"]');
    if (counter) {
      counter.textContent = (noteBytes(draft[name]) < 0 ? "Invalid text" : noteBytes(draft[name])) + " / " + NOTE_MAX + " bytes";
    }
  }

  function onClick(event) {
    var target = event.target && event.target.closest ? event.target.closest("[data-action],[data-tab]") : null;
    if (!target) {
      return;
    }
    if (target.getAttribute("data-tab")) {
      mode = target.getAttribute("data-tab");
      paint();
    } else if (target.getAttribute("data-action") === "close") {
      close();
    } else if (target.getAttribute("data-action") === "submit") {
      submit();
    }
  }

  function onKeydown(event) {
    if (event.key === "Escape") {
      event.preventDefault();
      close();
    }
  }

  function controls() {
    return Array.prototype.slice.call(root.querySelectorAll('[data-tab],select[data-field],textarea[data-field],[data-action="submit"]'))
      .filter(function (el) { return !el.disabled; });
  }

  function syncIndex() {
    var at = controls().indexOf(document.activeElement);
    if (at >= 0) {
      focusIndex = at;
    }
  }

  function focusAt(index) {
    var items = controls();
    if (!items.length) {
      return;
    }
    focusIndex = Math.max(0, Math.min(index, items.length - 1));
    items[focusIndex].focus();
  }

  function moveFocus(step) {
    if (controls().indexOf(document.activeElement) < 0) {
      focusAt(step > 0 ? 0 : controls().length - 1);
      return;
    }
    syncIndex();
    focusAt(focusIndex + step);
  }

  function cycleSelect(step) {
    var el = document.activeElement;
    if (!el || el.tagName !== "SELECT" || el.disabled) {
      return;
    }
    var next = el.selectedIndex + step;
    if (next < 0 || next >= el.options.length) {
      return;
    }
    el.selectedIndex = next;
    el.dispatchEvent(new Event("change", { bubbles: true }));
  }

  function activateFocused() {
    syncIndex();
    var el = controls()[focusIndex];
    if (!el || document.activeElement !== el) {
      return;
    }
    if (el.tagName === "BUTTON") {
      el.click();
      focusAt(focusIndex);
    } else {
      el.focus();
    }
  }

  function handleControllerInput(button, phase) {
    if (phase !== "down") {
      return;
    }
    if (button === "b") {
      close();
      return;
    }
    if (!root) {
      return;
    }
    if (button === "dpad_up") {
      moveFocus(-1);
    } else if (button === "dpad_down") {
      moveFocus(1);
    } else if (button === "dpad_left") {
      cycleSelect(-1);
    } else if (button === "dpad_right") {
      cycleSelect(1);
    } else if (button === "a") {
      activateFocused();
    }
  }

  window.coReportSetState = setState;
  window.coReportEvent = onEvent;
  window.handleControllerInput = handleControllerInput;
  window.coReportModel = {
    validate: validate,
    actionFor: actionFor,
    sentenceFor: sentenceFor,
    send: send,
    render: renderReport,
    setMode: function (next) {
      mode = next;
      paint();
    }
  };

  function init() {
    root = document.getElementById("coInfo");
    if (!root) {
      return;
    }
    root.addEventListener("click", onClick);
    root.addEventListener("input", onField);
    root.addEventListener("change", onField);
    document.addEventListener("keydown", onKeydown);
    document.documentElement.style.setProperty("--co-ui-scale", String(Math.max(0.75, Math.min(1.35, window.innerWidth / 1920))));
    paint();
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
