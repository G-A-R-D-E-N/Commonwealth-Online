(function () {
  "use strict";

  var MAX_VISIBLE = 4;
  var CLOSE_MS = 0; // Instant dismiss — keep HUD alert exits snappy
  var PROGRESS_TICK_MS = 100;
  var queue = [];
  var revisions = Object.create(null);
  var clearSequence = 0;
  var revisionCount = 0;
  var compactPending = false;
  var compactTimer = null;
  var interactionMode = "hotkey"; // "hotkey" | "mouse"
  var timers = Object.create(null);
  var progressTimers = Object.create(null);
  var stackEl = null;
  var gamepadInput = false;
  var CONTROLLER_BUTTON_FOR_HOTKEY = { Y: "X", N: "Y", F1: "A", T: "A" };

  function rememberRevision(id, sequence) {
    if (!Object.prototype.hasOwnProperty.call(revisions, id)) revisionCount++;
    revisions[id] = sequence;
    if (revisionCount >= 256 && !compactPending && typeof window.coAlertCompact === "function") {
      compactPending = true;
      window.coAlertCompact("{}");
      compactTimer = setTimeout(function () { compactPending = false; compactTimer = null; }, 5000);
    }
  }

  function esc(text) {
    return String(text == null ? "" : text)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function toneClass(tone) {
    var t = String(tone || "info").toLowerCase();
    if (
      t === "success" ||
      t === "warning" ||
      t === "error" ||
      t === "invite" ||
      t === "info"
    ) {
      return t;
    }
    return "info";
  }

  function actionVariant(action) {
    var id = String((action && action.id) || "").toLowerCase();
    if (id === "accept" || id === "join" || id === "confirm" || id === "yes") {
      return "primary";
    }
    if (id === "decline" || id === "deny" || id === "reject" || id === "no") {
      return "danger";
    }
    return "";
  }

  function sendAction(alertId, actionId, sequence) {
    var payload = JSON.stringify({
      id: alertId,
      action: actionId,
      sequence: sequence || 0
    });
    if (typeof window.coAlertAction === "function") {
      window.coAlertAction(payload);
    }
  }

  function clearTimer(id) {
    if (timers[id]) {
      clearTimeout(timers[id]);
      delete timers[id];
    }
    if (progressTimers[id]) {
      clearInterval(progressTimers[id]);
      delete progressTimers[id];
    }
  }

  function findAlert(id) {
    for (var i = 0; i < queue.length; i++) {
      if (queue[i].id === id) {
        return queue[i];
      }
    }
    return null;
  }

  function scheduleAutoDismiss(alert) {
    clearTimer(alert.id);
    if (!alert.durationMs || alert.durationMs <= 0) {
      return;
    }

    var started = Date.now();
    var duration = alert.durationMs;
    progressTimers[alert.id] = setInterval(function () {
      var card =
        stackEl &&
        stackEl.querySelector(
          '.co-alert[data-id="' + alert.id + '"] .co-alert__progress-bar'
        );
      if (!card) {
        return;
      }
      var remaining = Math.max(0, 1 - (Date.now() - started) / duration);
      card.style.width = remaining * 100 + "%";
    }, PROGRESS_TICK_MS);

    timers[alert.id] = setTimeout(function () {
      dismissAlert(alert.id, "expire");
    }, duration);
  }

  function keyCap(hotkey) {
    var button = CONTROLLER_BUTTON_FOR_HOTKEY[hotkey];
    if (gamepadInput && button && window.CO_ControllerGlyphs) {
      return CO_ControllerGlyphs.render(button);
    }
    return '<span class="co-alert__key">' + esc(hotkey) + "</span>";
  }

  function renderActions(alert) {
    var actions = alert.actions || [];
    if (!actions.length) {
      return "";
    }

    if (interactionMode === "mouse") {
      return (
        '<div class="co-alert__actions">' +
        actions
          .map(function (action) {
            var variant = actionVariant(action);
            var cls =
              "co-alert__btn" +
              (variant ? " co-alert__btn--" + variant : "");
            return (
              '<button type="button" class="' +
              cls +
              '" data-action="' +
              esc(action.id) +
              '">' +
              esc(action.label || action.id) +
              "</button>"
            );
          })
          .join("") +
        "</div>"
      );
    }

    return (
      '<div class="co-alert__actions co-alert__actions--hotkeys">' +
      actions
        .map(function (action) {
          var key = action.hotkey || "";
          if (!key) {
            return "";
          }
          return (
            '<span class="co-alert__prompt">' +
            keyCap(key) +
            '<span class="co-alert__prompt-label">' +
            esc(action.label || action.id) +
            "</span>" +
            "</span>"
          );
        })
        .join("") +
      "</div>"
    );
  }

  function renderProgress(alert) {
    if (!alert.durationMs || alert.durationMs <= 0) {
      return "";
    }
    return (
      '<div class="co-alert__progress" aria-hidden="true">' +
      '<div class="co-alert__progress-bar" style="width:100%;"></div>' +
      "</div>"
    );
  }

  function renderAlert(alert) {
    var category = alert.category || "ALERT";
    var title = alert.title || "";
    var message = alert.message || "";
    var tone = toneClass(alert.tone);

    return (
      '<article class="co-alert co-alert--' +
      tone +
      '" data-id="' +
      esc(alert.id) +
      '" role="status" aria-live="polite">' +
      '<div class="co-alert__rail" aria-hidden="true"></div>' +
      '<div class="co-alert__body">' +
      '<div class="co-alert__header">' +
      '<div class="co-alert__meta">' +
      '<span class="co-alert__category">' +
      esc(category) +
      "</span>" +
      '<div class="co-alert__title">' +
      esc(title) +
      "</div>" +
      "</div>" +
      '<button type="button" class="co-alert__dismiss" data-action="dismiss" aria-label="Dismiss">✕</button>' +
      "</div>" +
      (message
        ? '<p class="co-alert__message">' + esc(message) + "</p>"
        : "") +
      renderActions(alert) +
      "</div>" +
      renderProgress(alert) +
      "</article>"
    );
  }

  function reportHitRegion() {
    if (typeof window.coAlertHitRegion !== "function") {
      return;
    }

    if (!stackEl) {
      window.coAlertHitRegion(JSON.stringify({ active: false }));
      return;
    }

    var cards = stackEl.querySelectorAll(".co-alert");
    if (!cards.length) {
      window.coAlertHitRegion(JSON.stringify({ active: false }));
      return;
    }

    var left = Infinity;
    var top = Infinity;
    var right = -Infinity;
    var bottom = -Infinity;
    for (var i = 0; i < cards.length; i++) {
      var rect = cards[i].getBoundingClientRect();
      left = Math.min(left, rect.left);
      top = Math.min(top, rect.top);
      right = Math.max(right, rect.right);
      bottom = Math.max(bottom, rect.bottom);
    }

    // Small pad so focus switches just before the cursor reaches the card edge.
    var pad = 6;
    window.coAlertHitRegion(
      JSON.stringify({
        active: true,
        left: Math.max(0, Math.floor(left) - pad),
        top: Math.max(0, Math.floor(top) - pad),
        right: Math.ceil(right) + pad,
        bottom: Math.ceil(bottom) + pad
      })
    );
  }

  function paint() {
    if (!stackEl) {
      return;
    }
    var visible = queue.slice(0, MAX_VISIBLE);
    stackEl.innerHTML = visible.map(renderAlert).join("");
    if (window.CO_ControllerGlyphs) CO_ControllerGlyphs.hydrate(stackEl);
    reportHitRegion();
  }

  function normalizeAlert(raw) {
    var alert = raw || {};
    var id =
      alert.id ||
      "alert-" + Date.now().toString(36) + "-" + Math.random().toString(36).slice(2, 7);
    var actions = Array.isArray(alert.actions) ? alert.actions : [];
    return {
      id: String(id),
      sequence: Number(alert.sequence) || 0,
      actionSequence: Number(alert.actionSequence) || Number(alert.sequence) || 0,
      category: alert.category || "ALERT",
      title: alert.title || "",
      message: alert.message || "",
      tone: toneClass(alert.tone),
      durationMs: Number(alert.durationMs) || 0,
      actions: actions.map(function (action, index) {
        return {
          id: String(action.id || "action-" + index),
          label: String(action.label || action.id || "Action"),
          hotkey: action.hotkey ? String(action.hotkey).toUpperCase() : ""
        };
      })
    };
  }

  function showAlert(raw) {
    var alert = normalizeAlert(raw);
    if (alert.sequence > 0) {
      if (alert.sequence <= Math.max(clearSequence, revisions[alert.id] || 0)) {
        return alert.id;
      }
      rememberRevision(alert.id, alert.sequence);
    }
    var existing = findAlert(alert.id);
    if (existing) {
      Object.assign(existing, alert);
      queue.sort(function (a, b) { return b.sequence - a.sequence; });
      clearTimer(alert.id);
      paint();
      scheduleAutoDismiss(existing);
      return alert.id;
    }

    queue.unshift(alert);
    queue.sort(function (a, b) { return b.sequence - a.sequence; });
    if (queue.length > MAX_VISIBLE + 4) {
      var dropped = queue.pop();
      clearTimer(dropped.id);
      sendAction(dropped.id, "expire", dropped.actionSequence);
    }
    paint();
    scheduleAutoDismiss(alert);
    return alert.id;
  }

  function dismissAlert(id, reason, sequence) {
    sequence = Number(sequence) || 0;
    if (sequence > 0) {
      if (sequence <= Math.max(clearSequence, revisions[id] || 0)) {
        return;
      }
      rememberRevision(id, sequence);
    }
    var alert = findAlert(id);
    if (!alert) {
      return;
    }

    clearTimer(id);
    var card = stackEl && stackEl.querySelector('.co-alert[data-id="' + id + '"]');
    var finish = function () {
      queue = queue.filter(function (item) {
        return item.id !== id;
      });
      paint();
      if (reason && reason !== "silent") {
        sendAction(id, reason === "expire" ? "expire" : reason, alert.actionSequence);
      }
    };

    if (!card || CLOSE_MS <= 0) {
      finish();
      return;
    }

    card.classList.add("is-closing");
    setTimeout(finish, CLOSE_MS);
  }

  function dismissAll(silent, sequence) {
    sequence = Number(sequence) || 0;
    if (sequence > 0) {
      if (sequence <= clearSequence) return;
      clearSequence = sequence;
      Object.keys(revisions).forEach(function (id) {
        if (revisions[id] <= clearSequence) {
          delete revisions[id];
          revisionCount--;
        }
      });
    }
    var removed = queue.filter(function (item) {
      return !sequence || !item.sequence || item.sequence <= sequence;
    });
    removed.forEach(function (item) {
      clearTimer(item.id);
    });
    queue = queue.filter(function (item) { return removed.indexOf(item) === -1; });
    paint();
    if (!silent) {
      removed.forEach(function (item) {
        sendAction(item.id, "dismiss", item.actionSequence);
      });
    }
  }

  function setInteractionMode(mode) {
    interactionMode = mode === "mouse" ? "mouse" : "hotkey";
    paint();
  }

  function resolveHotkey(keyName, allowMouse) {
    if (!keyName || (interactionMode !== "hotkey" && !allowMouse)) {
      return false;
    }
    var key = String(keyName).toUpperCase();
    var keys = key === "A" ? ["F1", "T"] : [key];
    for (var i = 0; i < Math.min(queue.length, MAX_VISIBLE); i++) {
      var alert = queue[i];
      var actions = alert.actions || [];
      for (var j = 0; j < actions.length; j++) {
        if (keys.indexOf(actions[j].hotkey) >= 0) {
          var actionId = actions[j].id;
          dismissAlert(alert.id, "silent");
          sendAction(alert.id, actionId, alert.actionSequence);
          return true;
        }
      }
    }
    return false;
  }

  function onStackClick(event) {
    var button = event.target.closest("[data-action]");
    if (!button) {
      return;
    }
    var card = button.closest(".co-alert");
    if (!card) {
      return;
    }
    var id = card.getAttribute("data-id");
    var action = button.getAttribute("data-action");
    if (!id || !action) {
      return;
    }

    if (action === "dismiss") {
      dismissAlert(id, "dismiss");
      return;
    }

    var alert = findAlert(id);
    dismissAlert(id, "silent");
    sendAction(id, action, alert && alert.actionSequence);
  }

  function updateUiScale() {
    var scale = Math.max(0.75, Math.min(1.35, window.innerWidth / 1920));
    document.documentElement.style.setProperty("--co-ui-scale", String(scale));
  }

  function init() {
    stackEl = document.getElementById("coAlerts");
    if (!stackEl) {
      return;
    }
    stackEl.addEventListener("click", onStackClick);
    document.addEventListener("keydown", function (event) {
      if (interactionMode !== "mouse" || event.altKey || event.ctrlKey || event.metaKey || event.repeat) return;
      var key = String(event.key || "").toUpperCase();
      if ((key === "Y" || key === "N") && resolveHotkey(key, true)) event.preventDefault();
    });
    updateUiScale();
    window.addEventListener("resize", function () {
      updateUiScale();
      reportHitRegion();
    });
  }

  window.showCoAlert = function (raw) {
    return showAlert(raw);
  };

  window.dismissCoAlert = function (id, sequence) {
    dismissAlert(id, "silent", sequence);
  };

  window.dismissAllCoAlerts = function (silent, sequence) {
    dismissAll(!!silent, sequence);
  };

  window.compactCoAlerts = function (sequence, active) {
    dismissAll(true, sequence);
    if (Array.isArray(active)) active.forEach(showAlert);
    if (compactTimer) clearTimeout(compactTimer);
    compactTimer = null;
    compactPending = false;
  };

  window.setCoAlertInteractionMode = function (mode) {
    setInteractionMode(mode);
  };

  window.setCoAlertInputMode = function (gamepad, style) {
    gamepadInput = !!gamepad;
    if (window.CO_ControllerGlyphs) {
      CO_ControllerGlyphs.setPlatform(style === 1 ? "Playstation" : "Xbox");
    }
    paint();
  };

  window.resolveCoAlertHotkey = function (keyName, allowMouse) {
    return resolveHotkey(keyName, allowMouse);
  };

  window.getCoAlertQueue = function () {
    return queue.map(function (item) {
      return {
        id: item.id,
        category: item.category,
        title: item.title,
        hasActions: !!(item.actions && item.actions.length)
      };
    });
  };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }

  function shouldAutoDemo() {
    return /\bdemo=1\b/.test(window.location.search || "");
  }

  if (shouldAutoDemo()) {
    document.documentElement.classList.add("is-preview");
    setInteractionMode("mouse");
    var samples = [
      {
        id: "demo-join",
        category: "Social",
        title: "Player Requested To Join",
        message: "VaultDweller_92 wants to join your session.",
        tone: "invite",
        durationMs: 0,
        actions: [
          { id: "accept", label: "Accept", hotkey: "Y" },
          { id: "decline", label: "Decline", hotkey: "N" }
        ]
      },
      {
        id: "demo-friend",
        category: "Social",
        title: "Friend Invite Received",
        message: "LoneWanderer invited you to Commonwealth Eastern.",
        tone: "invite",
        durationMs: 0,
        actions: [
          { id: "accept", label: "Accept", hotkey: "Y" },
          { id: "decline", label: "Decline", hotkey: "N" }
        ]
      },
      {
        id: "demo-connect",
        category: "Network",
        title: "Can't Connect To Server",
        message: "Timed out reaching 192.168.1.40:7777.",
        tone: "error",
        durationMs: 0
      },
      {
        id: "demo-dc",
        category: "Network",
        title: "Disconnected From Server",
        message: "Connection lost. Returning to local session.",
        tone: "warning",
        durationMs: 0
      }
    ];

    samples.forEach(function (sample, index) {
      setTimeout(function () {
        showAlert(sample);
      }, index * 200);
    });
  }
})();
