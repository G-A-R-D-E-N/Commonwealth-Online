(function () {
  "use strict";

  var ROOT_ID = "coCharacterSelect";
  var MAX_NAME_LENGTH = 48;
  var MAX_SLOTS = 16;
  var emojiPattern;
  try {
    emojiPattern = new RegExp("[\\p{Extended_Pictographic}\\p{Regional_Indicator}\\p{Emoji_Modifier}\\uFE0F\\u20E3]", "u");
  } catch (err) {
    emojiPattern = /[\u00a9\u00ae\u203c\u2049\u20e3\u2122\u2139\u2194-\u2199\u21a9-\u21aa\u231a-\u231b\u2328\u23cf\u23e9-\u23f3\u23f8-\u23fa\u24c2\u25aa-\u25ab\u25b6\u25c0\u25fb-\u25fe\u2600-\u2604\u260e\u2611\u2614-\u2615\u2618\u261d\u2620\u2622-\u2623\u2626\u262a\u262e-\u262f\u2638-\u263a\u2640\u2642\u2648-\u2653\u265f-\u2660\u2663\u2665-\u2666\u2668\u267b\u267e-\u267f\u2692-\u2697\u2699\u269b-\u269c\u26a0-\u26a1\u26a7\u26aa-\u26ab\u26b0-\u26b1\u26bd-\u26be\u26c4-\u26c5\u26c8\u26ce-\u26cf\u26d1\u26d3-\u26d4\u26e9-\u26ea\u26f0-\u26f5\u26f7-\u26fa\u26fd\u2702\u2705\u2708-\u270d\u270f\u2712\u2714\u2716\u271d\u2721\u2728\u2733-\u2734\u2744\u2747\u274c\u274e\u2753-\u2755\u2757\u2763-\u2764\u2795-\u2797\u27a1\u27b0\u27bf\u2934-\u2935\u2b05-\u2b07\u2b1b-\u2b1c\u2b50\u2b55\u3030\u303d\u3297\u3299\ufe0f]|\ud83c[\udc04\udc2c-\udc2f\udc94-\udc9f\udcaf-\udcb0\udcc0\udccf-\udcd0\udcf6-\udcff\udd70-\udd71\udd7e-\udd7f\udd8e\udd91-\udd9a\uddae-\uddff\ude01-\ude0f\ude1a\ude2f\ude32-\ude3a\ude3c-\ude3f\ude49-\ude5f\ude66-\udf21\udf24-\udf93\udf96-\udf97\udf99-\udf9b\udf9e-\udff0\udff3-\udff5\udff7-\udfff]|\ud83d[\udc00-\udcfd\udcff-\udd3d\udd49-\udd4e\udd50-\udd67\udd6f-\udd70\udd73-\udd7a\udd87\udd8a-\udd8d\udd90\udd95-\udd96\udda4-\udda5\udda8\uddb1-\uddb2\uddbc\uddc2-\uddc4\uddd1-\uddd3\udddc-\uddde\udde1\udde3\udde8\uddef\uddf3\uddfa-\ude4f\ude80-\udec5\udecb-\uded2\uded5-\udee5\udee9\udeeb-\udef0\udef3-\udeff\udfda-\udfff]|\ud83e[\udc0c-\udc0f\udc48-\udc4f\udc5a-\udc5f\udc88-\udc8f\udcae-\udcaf\udcbc-\udcbf\udcc2-\udccf\udcd9-\udcff\udd0c-\udd3a\udd3c-\udd45\udd47-\uddff\ude58-\ude5f\ude6e-\udeff]|\ud83f[\udc00-\udffd]/;
  }

  var REASON_TEXT = {
    invalidRequest: "That request was not valid. Check the name and try again.",
    badSlot: "That character slot does not exist.",
    slotEmpty: "That slot is empty.",
    slotOccupied: "That slot already has a character.",
    slotsFull: "All character slots are full.",
    nameTaken: "That name is already taken. Choose another.",
    alreadyActive: "Another session is already using this account.",
    slotActive: "That character is currently in use and cannot be deleted.",
    badToken: "The delete confirmation did not match. Start over to delete.",
    tokenExpired: "The delete confirmation expired. Start over to delete.",
    rateLimited: "Too many requests. Wait a moment and try again.",
    persistenceUnavailable: "Character data is unavailable right now. Try again later.",
    slotsUnavailable: "Characters are unavailable on this server right now."
  };

  var OP_DONE_TEXT = {
    create: "Character created.",
    select: "Character selected.",
    list: "",
    hello: ""
  };

  var state = null;
  var ui = { createNames: {}, createError: "", confirmArmed: false };
  var lastRefreshKey = "";
  var refreshRetry = null;
  var refreshAttempts = 0;
  var REFRESH_RETRY_MS = 1000;
  var MAX_REFRESH_ATTEMPTS = 3;
  var lastHtml = null;

  function hasOwn(object, key) {
    return Object.prototype.hasOwnProperty.call(object, key);
  }

  function esc(text) {
    return String(text == null ? "" : text)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function trimmed(value) {
    return String(value == null ? "" : value).trim();
  }

  function list(value) {
    if (!Array.isArray(value)) {
      return [];
    }
    return value.filter(function (item) {
      return item && typeof item === "object";
    });
  }

  function toSlotNumber(value) {
    if (typeof value !== "number" || !Number.isSafeInteger(value) || value < 0 || value > 255) {
      return null;
    }
    return value;
  }

  function validateName(name) {
    var value = String(name == null ? "" : name);
    if (value.length < 1 || value.length > MAX_NAME_LENGTH) {
      return "Use 1 to 48 characters.";
    }
    if (value !== trimmed(value)) {
      return "The name cannot start or end with a space.";
    }
    if (/[#\x00-\x1F\x7F]/.test(value)) {
      return "The name cannot contain # or control characters.";
    }
    if (emojiPattern.test(value)) {
      return "The name cannot contain emoji.";
    }
    return "";
  }

  function normalizeSlot(raw) {
    if (!raw || typeof raw !== "object") {
      return null;
    }
    var slot = toSlotNumber(raw.slot);
    if (slot == null) {
      return null;
    }
    if (raw.empty === true) {
      return { slot: slot, empty: true, name: "" };
    }
    if (typeof raw.name !== "string" || raw.name.length > 128) {
      return null;
    }
    return { slot: slot, empty: false, name: raw.name };
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
    var block = next.characterSlots;
    if (!block || typeof block !== "object" || Array.isArray(block)) {
      return null;
    }
    if (block.supported !== true && block.loading !== true) {
      return { enabled: false };
    }
    var slots = list(block.slots)
      .map(normalizeSlot)
      .filter(function (entry) {
        return entry != null;
      })
      .slice(0, MAX_SLOTS);
    var selected = toSlotNumber(block.selectedSlot);
    var pending = null;
    if (block.pendingDelete && typeof block.pendingDelete === "object") {
      var deleteSlot = toSlotNumber(block.pendingDelete.slot);
      var expires = block.pendingDelete.expiresInSeconds;
      if (deleteSlot != null && typeof block.pendingDelete.token === "string" && block.pendingDelete.token !== "" &&
        typeof expires === "number" && Number.isFinite(expires) && expires > 0) {
        pending = { slot: deleteSlot, token: block.pendingDelete.token, expiresInSeconds: Math.floor(expires) };
      }
    }
    var result = null;
    if (block.lastResult && typeof block.lastResult === "object" && typeof block.lastResult.op === "string" &&
      typeof block.lastResult.ok === "boolean") {
      result = {
        op: block.lastResult.op,
        ok: block.lastResult.ok,
        reason: typeof block.lastResult.reason === "string" ? block.lastResult.reason : "",
        slot: toSlotNumber(block.lastResult.slot)
      };
    }
    return {
      enabled: true,
      needsSelection: block.needsSelection === true,
      legacy: block.legacy === true,
      loading: block.loading === true,
      notice: typeof block.notice === "string" ? block.notice.slice(0, 512) : "",
      truncated: block.truncated === true,
      selectedSlot: selected,
      slots: slots,
      pendingDelete: pending,
      lastResult: result
    };
  }

  function reasonText(result) {
    if (!result || typeof result !== "object" || result.ok !== false) {
      return "";
    }
    if (hasOwn(REASON_TEXT, result.reason)) {
      return REASON_TEXT[result.reason];
    }
    return "The server refused that request.";
  }

  function doneText(result) {
    if (!result || typeof result !== "object" || result.ok !== true) {
      return "";
    }
    if (hasOwn(OP_DONE_TEXT, result.op)) {
      return OP_DONE_TEXT[result.op];
    }
    return "";
  }

  function actionFor(op, slot, extra) {
    var action = { op: op };
    if (slot != null) {
      action.slot = slot;
    }
    if (extra && typeof extra === "object") {
      Object.keys(extra).forEach(function (key) {
        action[key] = extra[key];
      });
    }
    return action;
  }

  function send(action) {
    if (typeof window === "undefined" || typeof window.coCharacterSelectAction !== "function") {
      return false;
    }
    window.coCharacterSelectAction(JSON.stringify(action));
    return true;
  }

  function maybeRefresh(result) {
    if (!result || result.ok !== true) {
      return;
    }
    if (result.op !== "create" && result.op !== "delete") {
      return;
    }
    var key = result.op + ":" + String(result.slot);
    if (key === lastRefreshKey) {
      return;
    }
    lastRefreshKey = key;
    refreshAttempts = 0;
    requestList();
  }

  function requestList() {
    var before = JSON.stringify(state.slots);
    refreshAttempts++;
    send(actionFor("slotList"));
    clearTimeout(refreshRetry);
    refreshRetry = setTimeout(function () {
      refreshRetry = null;
      if (state && JSON.stringify(state.slots) === before && refreshAttempts < MAX_REFRESH_ATTEMPTS) {
        requestList();
      }
    }, REFRESH_RETRY_MS);
  }

  function deleteConfirm(entry, pending) {
    var name = entry.empty ? "slot " + (entry.slot + 1) : entry.name;
    return '<div class="co-select__confirm" role="alertdialog" aria-label="Confirm delete">' +
      "<p>Delete <strong>" + esc(name) + "</strong>? This cannot be undone.</p>" +
      '<p class="co-slot__note">Confirm within ' + pending.expiresInSeconds + " seconds.</p>" +
      '<button type="button" class="co-btn co-btn--danger" data-op="slotDeleteConfirm" data-slot="' + pending.slot + '">Yes, delete</button> ' +
      '<button type="button" class="co-btn" data-op="slotDeleteCancel">Keep</button>' +
      "</div>";
  }

  function renderCard(entry, candidate) {
    var selected = candidate.selectedSlot != null && candidate.selectedSlot === entry.slot;
    var confirm = candidate.pendingDelete && candidate.pendingDelete.slot === entry.slot ? deleteConfirm(entry, candidate.pendingDelete) : "";
    if (entry.empty) {
      var card = '<section class="co-slot co-slot--empty" data-slot="' + entry.slot + '">' +
        '<h3>Empty slot ' + (entry.slot + 1) + "</h3>";
      card += '<label class="co-slot__label" for="coSlotName-' + entry.slot + '">New character name</label>' +
        '<input id="coSlotName-' + entry.slot + '" type="text" maxlength="48" autocomplete="off" spellcheck="false">' +
        '<button type="button" class="co-btn co-btn--primary" data-op="slotCreate" data-slot="' + entry.slot + '">Create</button>' +
        confirm + "</section>";
      return card;
    }
    var badge = selected ? ' <span class="co-slot__badge">Current</span>' : "";
    return '<section class="co-slot" data-slot="' + entry.slot + '">' +
      "<h3>" + esc(entry.name) + badge + "</h3>" +
      '<button type="button" class="co-btn co-btn--primary" data-op="slotSelect" data-slot="' + entry.slot + '"' +
      (selected ? " disabled" : "") + ">Play</button> " +
      '<button type="button" class="co-btn co-btn--danger" data-op="slotDelete" data-slot="' + entry.slot + '">Delete</button>' +
      confirm + "</section>";
  }

  function render(candidate) {
    if (!candidate || candidate.enabled !== true) {
      return "";
    }
    if (candidate.needsSelection !== true) {
      return '<div class="co-select" role="status" aria-live="polite">' +
        '<p class="co-select__status">' + (candidate.loading === true ? "Loading character..." : "Loading into the Commonwealth") + "</p>" +
        "</div>";
    }
    var html = '<div class="co-select" role="dialog" aria-label="Choose your character">';
    html += "<h2>Choose your character</h2>";
    if (candidate.legacy) {
      html += '<p class="co-select__notice">This server runs an older mod build. Update the mod to use multiple characters; you are playing your original character.</p>';
    }
    if (candidate.notice !== "") {
      html += '<p class="co-select__notice">' + esc(candidate.notice) + "</p>";
    }
    if (candidate.truncated) {
      html += '<p class="co-select__notice">The character list did not fit. Showing part of it.</p>';
    }
    var i;
    for (i = 0; i < candidate.slots.length; i++) {
      html += renderCard(candidate.slots[i], candidate);
    }
    if (ui.createError !== "") {
      html += '<p class="co-select__error" role="alert">' + esc(ui.createError) + "</p>";
    }
    var failure = reasonText(candidate.lastResult);
    if (failure !== "") {
      html += '<p class="co-select__error" role="alert">' + esc(failure) + "</p>";
    }
    var done = doneText(candidate.lastResult);
    if (done !== "") {
      html += '<p class="co-select__done" role="status">' + esc(done) + "</p>";
    }
    html += '<div class="co-select__footer">' +
      '<button type="button" class="co-btn" data-op="refresh">Refresh</button> ' +
      '<button type="button" class="co-btn" data-op="disconnect">Disconnect</button>' +
      "</div></div>";
    return html;
  }

  function createNameInput(slot) {
    if (typeof document === "undefined" || !document.querySelector) {
      return null;
    }
    var input = document.querySelector("#coSlotName-" + slot);
    if (!input || typeof input.value !== "string") {
      return null;
    }
    return input;
  }

  function readCreateName(slot) {
    var input = createNameInput(slot);
    return input ? input.value : null;
  }

  function captureNames() {
    if (!state || !state.slots) {
      return;
    }
    state.slots.forEach(function (entry) {
      if (!entry.empty) {
        return;
      }
      var typed = readCreateName(entry.slot);
      if (typed !== null) {
        ui.createNames[String(entry.slot)] = typed;
      }
    });
  }

  function restoreNames() {
    Object.keys(ui.createNames).forEach(function (slot) {
      var input = createNameInput(slot);
      if (input) {
        input.value = ui.createNames[slot];
      }
    });
  }

  function focusedName() {
    var active = document.activeElement;
    if (!active || typeof active.id !== "string" || active.id.indexOf("coSlotName-") !== 0) {
      return null;
    }
    return { slot: active.id.slice("coSlotName-".length), start: active.selectionStart, end: active.selectionEnd };
  }

  function restoreFocusedName(focused) {
    var input = focused ? createNameInput(focused.slot) : null;
    if (!input) {
      return;
    }
    input.focus();
    input.setSelectionRange(focused.start, focused.end);
  }

  function onAction(op, slot, token) {
    if ((op === "slotCreate" || op === "slotSelect" || op === "slotDelete" ||
      op === "slotDeleteConfirm") && slot == null) {
      return false;
    }
    if (op === "slotCreate") {
      var name = readCreateName(slot) || "";
      var error = validateName(name);
      if (error !== "") {
        ui.createError = error;
        paint();
        return false;
      }
      ui.createError = "";
      ui.createNames[String(slot)] = name;
      return send(actionFor("slotCreate", slot, { name: name }));
    }
    if (op === "slotDeleteConfirm") {
      var pending = state && state.pendingDelete ? state.pendingDelete : null;
      if (!pending || pending.slot !== slot) {
        return false;
      }
      return send(actionFor("slotDelete", slot, { token: token || pending.token }));
    }
    if (op === "slotDeleteCancel") {
      if (state) {
        state.pendingDelete = null;
      }
      paint();
      return send(actionFor("slotDeleteCancel"));
    }
    if (op === "slotSelect" || op === "slotDelete") {
      return send(actionFor(op, slot));
    }
    if (op === "refresh") {
      lastRefreshKey = "";
      ui.createError = "";
      paint();
      return send(actionFor("slotList"));
    }
    return false;
  }

  var root = null;

  function isVisible(candidate) {
    return !!candidate && candidate.enabled === true;
  }

  function paint() {
    if (!root) {
      return;
    }
    if (!isVisible(state)) {
      root.innerHTML = "";
      root.hidden = true;
      if (root.setAttribute) {
        root.setAttribute("hidden", "");
      }
      return;
    }
    maybeRefresh(state.lastResult);
    captureNames();
    var html = render(state);
    if (!root.hidden && html === lastHtml) {
      return;
    }
    var focused = focusedName();
    lastHtml = html;
    root.innerHTML = html;
    restoreNames();
    restoreFocusedName(focused);
    root.hidden = false;
    if (root.removeAttribute) {
      root.removeAttribute("hidden");
    }
  }

  function setState(raw) {
    var next = normalize(raw);
    if (!next) {
      return false;
    }
    state = next;
    if (next.enabled === true && next.lastResult && next.lastResult.ok === true && next.lastResult.op === "create") {
      ui.createNames = {};
    }
    paint();
    return true;
  }

  function hide() {
    state = null;
    ui.createError = "";
    lastHtml = null;
    if (!root) {
      return true;
    }
    root.innerHTML = "";
    root.hidden = true;
    if (root.setAttribute) {
      root.setAttribute("hidden", "");
    }
    return true;
  }

  function handleClick(target) {
    if (!target || typeof target.getAttribute !== "function") {
      var plain = target && typeof target === "object" ? target : {};
      return onAction(plain.op, plain.slot, plain.token);
    }
    var op = target.getAttribute("data-op");
    var slot = toSlotNumber(Number(target.getAttribute("data-slot")));
    if (op === "disconnect" || op === "close") {
      if (typeof window !== "undefined" && typeof window.coCharacterSelectAction === "function") {
        window.coCharacterSelectAction(JSON.stringify(actionFor("disconnect")));
      }
      return true;
    }
    if (!op) {
      return false;
    }
    return onAction(op, slot, target.getAttribute("data-token"));
  }

  function controlButtons() {
    if (!root || !root.querySelectorAll) {
      return [];
    }
    return Array.prototype.filter.call(root.querySelectorAll("button[data-op]"), function (button) {
      return !button.disabled;
    });
  }

  function moveControlFocus(step) {
    var buttons = controlButtons();
    if (buttons.length === 0) {
      return;
    }
    var index = buttons.indexOf(document.activeElement);
    if (index < 0) {
      index = step > 0 ? -1 : buttons.length;
    }
    buttons[Math.max(0, Math.min(buttons.length - 1, index + step))].focus();
  }

  function handleControllerInput(button, phase) {
    if (phase !== "down") {
      return;
    }
    if (button === "dpad_up" || button === "dpad_left") {
      moveControlFocus(-1);
    } else if (button === "dpad_down" || button === "dpad_right") {
      moveControlFocus(1);
    } else if (button === "a") {
      var focused = document.activeElement;
      if (controlButtons().indexOf(focused) < 0) {
        moveControlFocus(1);
      } else {
        handleClick(focused);
      }
    } else if (button === "b") {
      var cancel = root && root.querySelector ? root.querySelector('button[data-op="slotDeleteCancel"]') : null;
      if (cancel) {
        handleClick(cancel);
      }
    }
  }

  function init() {
    if (typeof document === "undefined" || !document.getElementById) {
      return;
    }
    root = document.getElementById(ROOT_ID);
    if (!root) {
      return;
    }
    if (root.addEventListener) {
      root.addEventListener("click", function (event) {
        var target = event && event.target ? event.target : null;
        var button = target && target.closest ? target.closest("[data-op]") : target;
        handleClick(button);
      });
    }
    paint();
  }

  if (typeof window !== "undefined") {
    window.coCharacterSelectSetState = setState;
    window.coCharacterSelectHide = hide;
    window.handleControllerInput = handleControllerInput;
    window.coCharacterSelectModel = {
      ROOT_ID: ROOT_ID,
      REASON_TEXT: REASON_TEXT,
      normalize: normalize,
      render: render,
      reasonText: reasonText,
      validateName: validateName,
      actionFor: actionFor,
      setState: setState,
      hide: hide,
      handleClick: handleClick,
      resetForTests: function () {
        state = null;
        ui = { createNames: {}, createError: "", confirmArmed: false };
        lastRefreshKey = "";
        refreshAttempts = 0;
        lastHtml = null;
      }
    };
  }

  if (typeof document !== "undefined" && document.addEventListener) {
    if (document.readyState === "loading") {
      document.addEventListener("DOMContentLoaded", init);
    } else {
      init();
    }
  }
})();
