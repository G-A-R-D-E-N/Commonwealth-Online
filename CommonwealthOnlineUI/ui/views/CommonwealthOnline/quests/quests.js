(function () {
  "use strict";

  var SCOPE_LABEL = { shared: "SHARED", solo: "SOLO", party: "PARTY", held: "HELD" };

  var MODE_LABEL = {
    "coop-story": "CO-OP STORY",
    "faction-war": "FACTION WAR",
    "instanced-story": "INSTANCED STORY",
    "free-roam": "FREE ROAM"
  };

  var PENDING_TIMEOUT_MS = 3000;

  var state = null;
  var lastStateKey = null;
  var root = null;
  var pending = null;
  var selected = "";

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

  function scopeOf(quest) {
    var scope = trimmed(quest && quest.scope).toLowerCase();
    return hasOwn(SCOPE_LABEL, scope) ? scope : "shared";
  }

  function modeLabel(mode) {
    var key = trimmed(mode).toLowerCase();
    return hasOwn(MODE_LABEL, key) ? MODE_LABEL[key] : "UNKNOWN MODE";
  }

  function stageText(stage) {
    var value = Number(stage);
    if (!isFinite(value) || value <= 0) {
      return "—";
    }
    return "STAGE " + value;
  }

  function actionFor(data) {
    if (!data) {
      return null;
    }
    var name = trimmed(data.action);
    if (name === "close" || name === "refresh") {
      return { action: name };
    }
    var quest = trimmed(data.quest);
    if (!quest) {
      return null;
    }
    if (name === "select") {
      return { action: "select", quest: quest };
    }
    if (name === "setScope") {
      var scope = trimmed(data.scope).toLowerCase();
      if (scope !== "shared" && scope !== "solo" && scope !== "party") {
        return null;
      }
      return { action: "setScope", quest: quest, scope: scope };
    }
    return null;
  }

  function friendRow(friend) {
    var name = trimmed(friend.name) || ("Player " + trimmed(friend.id)).trim();
    return (
      '<li class="quest-friend">' +
      '<span class="quest-friend__name">' + esc(name) + "</span>" +
      '<span class="quest-muted">' + esc(stageText(friend.stage)) + "</span>" +
      "</li>"
    );
  }

  function scopeButton(key, scope, label, current, pendingRow, unavailable) {
    var pressed = current === scope;
    var text = pendingRow && pendingRow.scope === scope ? "Updating..." : label;
    var lock = current === "held" || (unavailable && !pressed) ? " disabled" : pendingRow ? ' aria-disabled="true"' : "";
    return (
      '<button type="button" class="quest-btn' + (scope === "shared" ? " is-primary" : "") +
      (pressed ? " is-pressed" : "") + '" data-action="setScope" data-quest="' + esc(key) +
      '" data-scope="' + scope + '" aria-pressed="' + (pressed ? "true" : "false") + '"' + lock + ">" +
      text + "</button>"
    );
  }

  var CATEGORIES = [
    { id: "main", label: "MAIN STORY" },
    { id: "faction", label: "FACTION" },
    { id: "side", label: "SIDE QUESTS" },
    { id: "misc", label: "OTHER" }
  ];

  function categoryOf(quest) {
    var id = quest && quest.nextStep !== true ? trimmed(quest.category).toLowerCase() : "main";
    return CATEGORIES.some(function (category) { return category.id === id; }) ? id : "main";
  }

  function orderedQuests(quests) {
    var out = [];
    CATEGORIES.forEach(function (category) {
      quests.forEach(function (quest) {
        if (categoryOf(quest) === category.id) {
          out.push(quest);
        }
      });
    });
    return out;
  }

  function selectedQuest(ordered) {
    var found = selected && ordered.find(function (quest) { return trimmed(quest.quest) === selected; });
    return found || ordered[0];
  }

  function questIndex(ordered, chosen) {
    var lastCategory = "";
    return ordered.map(function (quest) {
      var category = categoryOf(quest);
      var head = "";
      if (category !== lastCategory) {
        lastCategory = category;
        head = '<li class="quest-cat">' + CATEGORIES.filter(function (c) { return c.id === category; })[0].label + "</li>";
      }
      var key = trimmed(quest.quest);
      var on = quest === chosen;
      return (
        head + '<li><button type="button" class="quest-item' + (on ? " is-selected" : "") +
        (quest.nextStep === true ? " is-next" : "") + '" data-action="select" data-quest="' + esc(key) +
        '" aria-pressed="' + (on ? "true" : "false") + '">' + esc(quest.name) + "</button></li>"
      );
    }).join("");
  }

  function questDetail(quest, inFaction) {
    var key = trimmed(quest.quest);
    if (quest.nextStep === true) {
      return (
        '<div class="quest-detail__head"><h2 class="quest-detail__name">' + esc(quest.name) + "</h2>" +
        '<span class="quest-badge is-next">NEXT</span></div>' +
        '<p class="quest-key">' + esc(key) + "</p>" +
        '<p class="quest-muted">Finish the previous story quest to unlock this step.</p>'
      );
    }
    var scope = scopeOf(quest);
    var pendingRow = pending && pending.quest === key ? pending : null;
    var friends = list(quest.friends).map(friendRow);
    return (
      '<div class="quest-detail__head"><h2 class="quest-detail__name">' + esc(quest.name) + "</h2>" +
      '<span class="quest-badge is-' + scope + '">' + SCOPE_LABEL[scope] + "</span>" +
      '<span class="quest-stage">' + esc(stageText(quest.stage)) + "</span></div>" +
      '<p class="quest-key">' + esc(key) + "</p>" +
      (friends.length
        ? '<ul class="quest-friends">' + friends.join("") + "</ul>"
        : '<p class="quest-muted">No friends on this quest yet.</p>') +
      '<div class="quest-actions">' +
      scopeButton(key, "shared", "Share", scope, pendingRow, false) +
      scopeButton(key, "party", "Party", scope, pendingRow, !inFaction) +
      scopeButton(key, "solo", "Go solo", scope, pendingRow, false) +
      "</div>" +
      (inFaction ? "" : '<p class="quest-muted">Join a faction to share a quest with a party.</p>')
    );
  }

  function noticesSection(notices) {
    var rows = list(notices).map(function (notice) {
      return (
        '<li class="quest-notice">' + esc(notice.text) +
        '<span class="quest-muted">' + esc(notice.time) + "</span></li>"
      );
    });
    return (
      '<div class="quest-section"><h2>NOTICES</h2>' +
      (rows.length
        ? '<ul class="quest-notices" aria-live="polite">' + rows.join("") + "</ul>"
        : '<p class="quest-muted">No notices.</p>') +
      "</div>"
    );
  }

  function panel(mode, body) {
    return (
      '<section class="quest-panel" aria-label="Quests">' +
      '<header class="quest-head"><h1 class="quest-title">QUESTS</h1>' +
      '<div class="quest-head__side">' +
      '<span class="quest-mode">' + esc(mode) + "</span>" +
      '<button type="button" class="quest-btn quest-close" data-action="close" aria-label="Close quests">Close</button>' +
      "</div></header>" +
      body +
      "</section>"
    );
  }

  function renderState(s) {
    if (!s) {
      return panel("—", '<p class="quest-note">Loading quest status...</p>');
    }
    if (s.enabled !== true) {
      if (s.tutorial === true) {
        return panel(
          "—",
          '<p class="quest-note">Complete the intro to unlock quests</p>' +
          '<div class="quest-actions"><button type="button" class="quest-btn" data-action="close">Close</button></div>'
        );
      }
      return panel(
        "—",
        '<p class="quest-note">Quests are not available on this server</p>' +
        '<div class="quest-actions"><button type="button" class="quest-btn" data-action="close">Close</button></div>'
      );
    }
    if (trimmed(s.mode).toLowerCase() === "free-roam") {
      return panel(
        modeLabel(s.mode),
        '<p class="quest-note">Quests are disabled on this server</p>' +
        '<div class="quest-actions"><button type="button" class="quest-btn" data-action="close">Close</button></div>'
      );
    }

    var ordered = orderedQuests(list(s.quests));
    var chosen = selectedQuest(ordered);
    return panel(
      modeLabel(s.mode),
      '<div class="quest-body"><div class="quest-index"><h2>QUESTS</h2>' +
      (ordered.length
        ? '<ul class="quest-list">' + questIndex(ordered, chosen) + "</ul>"
        : '<p class="quest-muted">No quests yet.</p>') +
      '</div><div class="quest-detail">' +
      (chosen ? questDetail(chosen, s.inFaction === true) : "") +
      noticesSection(s.notices) +
      "</div></div>" +
      '<div class="quest-actions quest-footer">' +
      '<button type="button" class="quest-btn" data-action="refresh">Refresh</button>' +
      '<button type="button" class="quest-btn" data-action="close">Close</button>' +
      "</div>"
    );
  }

  function focusTarget() {
    var active = document.activeElement;
    if (!active || !active.dataset || !active.dataset.action) {
      return null;
    }
    return {
      action: active.dataset.action,
      quest: active.dataset.quest || "",
      scope: active.dataset.scope || ""
    };
  }

  function restoreFocus(target) {
    if (!target) {
      return;
    }
    var match = Array.from(root.querySelectorAll("button[data-action]")).find(function (el) {
      return !el.disabled && el.dataset.action === target.action &&
        (el.dataset.quest || "") === target.quest && (el.dataset.scope || "") === target.scope;
    });
    if (match) {
      match.focus();
    }
  }

  function paint() {
    if (!root) {
      return;
    }
    var html;
    try {
      html = renderState(state);
    } catch (err) {
      html = renderState({ enabled: false });
    }
    var scrollers = [".quest-panel", ".quest-index", ".quest-detail"];
    var tops = scrollers.map(function (selector) {
      var el = root.querySelector(selector);
      return el ? el.scrollTop : 0;
    });
    var target = focusTarget();
    root.innerHTML = html;
    scrollers.forEach(function (selector, i) {
      var el = root.querySelector(selector);
      if (el) {
        el.scrollTop = tops[i];
      }
    });
    restoreFocus(target);
  }

  function send(action) {
    if (typeof window.coQuestAction === "function") {
      window.coQuestAction(JSON.stringify(action));
    }
  }

  function clearPending() {
    if (pending) {
      window.clearTimeout(pending.timer);
      pending = null;
    }
  }

  function expirePending() {
    pending = null;
    paint();
  }

  function setState(raw) {
    var next = raw;
    if (typeof raw === "string") {
      try {
        next = JSON.parse(raw);
      } catch (err) {
        return false;
      }
    }
    if (!next || typeof next !== "object" || Array.isArray(next)) {
      next = { enabled: false };
    }
    var key = null;
    try {
      key = JSON.stringify(next);
    } catch (err) {
      key = null;
    }
    if (key !== null && key === lastStateKey && !pending) {
      return true;
    }
    lastStateKey = key;
    state = next;
    clearPending();
    paint();
    return true;
  }

  function onRootClick(event) {
    var target = event.target;
    var el = target && target.closest ? target.closest("[data-action]") : null;
    if (!el || !el.dataset) {
      return;
    }
    var action = actionFor(el.dataset);
    if (!action) {
      return;
    }
    if (action.action === "select") {
      selected = action.quest;
      paint();
      var detail = root && root.querySelector(".quest-detail");
      if (detail) {
        detail.scrollTop = 0;
      }
      return;
    }
    if (action.action !== "setScope") {
      send(action);
      return;
    }
    if (pending && pending.quest === action.quest) {
      return;
    }
    clearPending();
    pending = {
      quest: action.quest,
      scope: action.scope,
      timer: window.setTimeout(expirePending, PENDING_TIMEOUT_MS)
    };
    send(action);
    paint();
  }

  function focusables() {
    return root
      ? Array.prototype.slice.call(document.querySelectorAll(".quest-panel button:not([disabled])"))
      : [];
  }

  function moveFocus(delta) {
    var items = focusables();
    if (!items.length) {
      return;
    }
    var index = items.indexOf(document.activeElement);
    if (index < 0) {
      index = delta > 0 ? 0 : items.length - 1;
    } else {
      index = (index + delta + items.length) % items.length;
    }
    items[index].focus();
  }

  function activateFocused() {
    var active = document.activeElement;
    if (active && active.tagName === "BUTTON") {
      active.click();
    }
  }

  function close() {
    send({ action: "close" });
  }

  function onKeydown(event) {
    if (event.key === "Escape") {
      event.preventDefault();
      close();
    } else if (event.key === "ArrowDown") {
      event.preventDefault();
      moveFocus(1);
    } else if (event.key === "ArrowUp") {
      event.preventDefault();
      moveFocus(-1);
    } else if (event.key === "Enter") {
      event.preventDefault();
      activateFocused();
    }
  }

  function handleControllerInput(button, phase) {
    if (phase !== "down") {
      return;
    }
    if (button === "b") {
      close();
    } else if (button === "dpad_down") {
      moveFocus(1);
    } else if (button === "dpad_up") {
      moveFocus(-1);
    } else if (button === "a") {
      activateFocused();
    }
  }

  function updateUiScale() {
    var scale = Math.max(0.75, Math.min(1.35, window.innerWidth / 1920));
    document.documentElement.style.setProperty("--co-ui-scale", String(scale));
  }

  function init() {
    root = document.getElementById("coQuests");
    if (!root) {
      return;
    }
    root.addEventListener("click", onRootClick);
    document.addEventListener("keydown", onKeydown);
    updateUiScale();
    paint();
  }

  window.coQuestSetState = setState;
  window.handleControllerInput = handleControllerInput;
  window.coQuestModel = {
    renderState: renderState,
    actionFor: actionFor,
    scopeOf: scopeOf,
    modeLabel: modeLabel,
    stageText: stageText
  };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
