(function () {
  "use strict";

  var SCOPE_LABEL = { shared: "SHARED", solo: "SOLO", party: "PARTY", fireteam: "FIRETEAM", held: "HELD" };
  var SCOPE_RULES = "Party: your faction group shares it. Solo: you play alone.";
  var SHARE_RULE = "Share: needs a fireteam. ";

  var MODE_LABEL = {
    "coop-story": "CO-OP STORY",
    "faction-war": "FACTION WAR",
    "instanced-story": "INSTANCED STORY",
    "free-roam": "FREE ROAM"
  };

  var FIRETEAM_MAX_MEMBERS = 4;

  var FIRETEAM_REASON = {
    full: "The fireteam is full.",
    already_member: "That player is already in a fireteam.",
    no_invite: "That invite has expired.",
    not_leader: "Only the fireteam leader can do that.",
    invalid_target: "That player cannot be invited.",
    self_target: "You cannot pick yourself.",
    unknown_member: "That player is not in your fireteam.",
    unsupported: "This server does not support fireteams.",
    not_connected: "Not connected to the server."
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
    if (name === "fireteamLeave") {
      return { action: name };
    }
    if (name === "fireteamInvite" || name === "fireteamKick") {
      var target = trimmed(data.target);
      var targetId = Number(target);
      if (!/^[0-9]{1,10}$/.test(target) || targetId < 1 || targetId > 4294967295) {
        return null;
      }
      return { action: name, target: targetId };
    }
    if (name === "fireteamRespond") {
      var inviteId = trimmed(data.inviteId);
      var accept = trimmed(data.accept);
      if (!/^[A-Za-z0-9_-]{1,64}$/.test(inviteId) || (accept !== "true" && accept !== "false")) {
        return null;
      }
      return { action: name, inviteId: inviteId, accept: accept === "true" };
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
      if (scope !== "shared" && scope !== "solo" && scope !== "party" && scope !== "fireteam") {
        return null;
      }
      return { action: "setScope", quest: quest, scope: scope };
    }
    return null;
  }

  function fireteamEventText(event) {
    var kind = event ? trimmed(event.kind) : "";
    var name = trimmed(event && event.name) || "Player";
    if (kind === "invited") {
      return "Invite sent to " + name + ".";
    }
    if (kind === "declined") {
      return name + " declined your invite.";
    }
    if (kind === "removed") {
      return "You are no longer in a fireteam.";
    }
    if (kind !== "refusal") {
      return "";
    }
    var reason = trimmed(event.reason);
    return hasOwn(FIRETEAM_REASON, reason) ? FIRETEAM_REASON[reason] : "The fireteam request was refused.";
  }

  function fireteamSection(fireteam) {
    var data = fireteam && typeof fireteam === "object" ? fireteam : {};
    var self = Number(data.self) || 0;
    var team = data.team && typeof data.team === "object" ? data.team : null;
    var members = team ? list(team.members) : [];
    var leader = team ? Number(team.leaderPlayerId) : 0;
    var amLeader = team !== null && self !== 0 && leader === self;
    var full = members.length >= FIRETEAM_MAX_MEMBERS;
    var teamBlock = '<p class="quest-muted">Not in a fireteam.</p>';
    if (team) {
      var rows = members.map(function (member) {
        var id = Number(member.playerId);
        var name = trimmed(member.name) || ("Player " + id);
        var badge = id === leader ? '<span class="quest-badge is-next">LEADER</span>' : "";
        var kick = amLeader && id !== self
          ? '<button type="button" class="quest-btn" data-action="fireteamKick" data-target="' + esc(id) + '">Kick</button>'
          : "";
        return "<li>" + esc(name) + badge + kick + "</li>";
      });
      teamBlock = '<h3>TEAM</h3><ul class="quest-team">' + rows.join("") + "</ul>" +
        '<div class="quest-actions"><button type="button" class="quest-btn" data-action="fireteamLeave">Leave</button></div>';
    }
    var invites = list(data.invites).map(function (invite) {
      var inviteId = esc(trimmed(invite.inviteId));
      var from = trimmed(invite.fromName) || ("Player " + trimmed(invite.fromPlayerId));
      return (
        '<li class="quest-invite"><span class="quest-invite__text">' + esc(from) + " invites you to a fireteam.</span>" +
        '<button type="button" class="quest-btn is-primary" data-action="fireteamRespond" data-invite-id="' + inviteId +
        '" data-accept="true">Accept</button>' +
        '<button type="button" class="quest-btn" data-action="fireteamRespond" data-invite-id="' + inviteId +
        '" data-accept="false">Decline</button></li>'
      );
    });
    var eventText = fireteamEventText(data.event);
    var roster = list(data.invitable).map(function (candidate) {
      var id = Number(candidate.playerId);
      var name = trimmed(candidate.name) || ("Player " + id);
      return "<li>" + esc(name) + '<button type="button" class="quest-btn" data-action="fireteamInvite" data-target="' +
        esc(id) + '"' + (full ? " disabled" : "") + ">Invite</button></li>";
    });
    return (
      '<div class="quest-section quest-fireteam"><h2>FIRETEAM</h2>' +
      teamBlock +
      (invites.length ? '<h3>INVITES</h3><ul class="quest-invites">' + invites.join("") + "</ul>" : "") +
      (eventText ? '<p class="quest-note" aria-live="polite">' + esc(eventText) + "</p>" : "") +
      "<h3>INVITE A PLAYER</h3>" +
      (roster.length ? '<ul class="quest-roster">' + roster.join("") + "</ul>" : '<p class="quest-muted">No players to invite.</p>') +
      "</div>"
    );
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
    var lock = current === "held" || pressed || unavailable ? " disabled" : pendingRow ? ' aria-disabled="true"' : "";
    return (
      '<button type="button" class="quest-btn' + (scope === "shared" ? " is-primary" : "") +
      (pressed ? " is-pressed" : "") + '" data-action="setScope" data-quest="' + esc(key) +
      '" data-scope="' + scope + '" aria-pressed="' + (pressed ? "true" : "false") + '"' + lock + ">" +
      text + "</button>"
    );
  }

  var activeSubTab = "all";

  var SUBTABS = [
    { id: "all", label: "ALL" },
    { id: "main", label: "MAIN STORY" },
    { id: "side", label: "SIDE QUESTS" },
    { id: "faction", label: "FACTIONS" },
    { id: "misc", label: "EXPEDITIONS / MISC" }
  ];

  var CATEGORIES = [
    { id: "main", label: "MAIN STORY" },
    { id: "faction", label: "FACTION" },
    { id: "side", label: "SIDE QUESTS" },
    { id: "misc", label: "OTHER" }
  ];

  function categoryOf(quest) {
    if (!quest) return "main";
    if (quest.nextStep === true) return "main";
    var cat = trimmed(quest.category).toLowerCase();
    if (!cat) return "main";
    if (cat === "side" || cat.indexOf("side") >= 0) return "side";
    if (cat === "faction" || cat.indexOf("faction") >= 0) return "faction";
    if (cat === "misc" || cat === "other" || cat.indexOf("misc") >= 0) return "misc";
    if (cat === "main" || cat.indexOf("main") >= 0 || cat.indexOf("story") >= 0) return "main";
    return "main";
  }

  function subtabCounts(allQuests) {
    var counts = { all: allQuests.length, main: 0, side: 0, faction: 0, misc: 0 };
    allQuests.forEach(function (q) {
      var cat = categoryOf(q);
      if (counts[cat] !== undefined) counts[cat]++;
    });
    return counts;
  }

  function questSubNav(counts, currentTab) {
    return (
      '<nav class="quest-subnav" aria-label="Quest categories">' +
      SUBTABS.map(function (tab) {
        var active = tab.id === currentTab ? " is-active" : "";
        var count = counts[tab.id] !== undefined ? counts[tab.id] : 0;
        return (
          '<button type="button" class="quest-subtab' + active + '" data-quest-cat="' + tab.id + '">' +
          '<span class="quest-subtab__label">' + tab.label + '</span>' +
          '<span class="quest-subtab__count">[' + count + ']</span>' +
          '</button>'
        );
      }).join("") +
      '</nav>'
    );
  }

  function orderedQuests(quests, filterCat) {
    var out = [];
    CATEGORIES.forEach(function (category) {
      if (filterCat && filterCat !== "all" && category.id !== filterCat) {
        return;
      }
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

  function questIndex(ordered, chosen, filterCat) {
    var lastCategory = "";
    var showCatHeaders = !filterCat || filterCat === "all";
    return ordered.map(function (quest) {
      var category = categoryOf(quest);
      var head = "";
      if (showCatHeaders && category !== lastCategory) {
        lastCategory = category;
        head = '<li class="quest-cat">' + CATEGORIES.filter(function (c) { return c.id === category; })[0].label + "</li>";
      }
      var key = trimmed(quest.quest);
      var on = quest === chosen;
      var scope = scopeOf(quest);
      var badge = '<span class="quest-item__badge is-' + scope + '">' + SCOPE_LABEL[scope] + '</span>';
      return (
        head + '<li><button type="button" class="quest-item' + (on ? " is-selected" : "") +
        (quest.nextStep === true ? " is-next" : "") + '" data-action="select" data-quest="' + esc(key) +
        '" aria-pressed="' + (on ? "true" : "false") + '">' +
        '<span class="quest-item__name">' + esc(quest.name) + '</span>' +
        '<span class="quest-item__meta">' + badge + '</span>' +
        "</button></li>"
      );
    }).join("");
  }

  function questDetail(quest, inFaction, fireteamSupported) {
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
      scopeButton(key, "shared", scope === "solo" ? "Share quest" : "Share", scope, pendingRow, false) +
      scopeButton(key, "party", "Party", scope, pendingRow, !inFaction) +
      (fireteamSupported ? scopeButton(key, "fireteam", "Fireteam", scope, pendingRow, false) : "") +
      (scope === "solo" ? "" : scopeButton(key, "solo", "Go solo", scope, pendingRow, false)) +
      "</div>" +
      '<p class="quest-muted">' + (fireteamSupported ? SHARE_RULE : "") + SCOPE_RULES + "</p>" +
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

    var allQuests = list(s.quests);
    var counts = subtabCounts(allQuests);
    var ordered = orderedQuests(allQuests, activeSubTab);
    var chosen = selectedQuest(ordered);
    return panel(
      modeLabel(s.mode),
      questSubNav(counts, activeSubTab) +
      '<div class="quest-body"><div class="quest-index">' +
      '<h2>QUESTS</h2>' +
      (ordered.length
        ? '<ul class="quest-list">' + questIndex(ordered, chosen, activeSubTab) + "</ul>"
        : '<p class="quest-muted">No quests in this category.</p>') +
      '</div><div class="quest-detail">' +
      (chosen ? questDetail(chosen, s.inFaction === true, s.fireteamSupported === true) : "") +
      noticesSection(s.notices) +
      (s.fireteamSupported === true ? fireteamSection(s.fireteam) : "") +
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
      scope: active.dataset.scope || "",
      target: active.dataset.target || "",
      inviteId: active.dataset.inviteId || "",
      accept: active.dataset.accept || ""
    };
  }

  function restoreFocus(target) {
    if (!target) {
      return;
    }
    var match = Array.from(root.querySelectorAll("button[data-action]")).find(function (el) {
      return !el.disabled && el.dataset.action === target.action &&
        (el.dataset.quest || "") === target.quest && (el.dataset.scope || "") === target.scope &&
        (el.dataset.target || "") === target.target && (el.dataset.inviteId || "") === target.inviteId &&
        (el.dataset.accept || "") === target.accept;
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
    var subtabEl = target && target.closest ? target.closest("[data-quest-cat]") : null;
    if (subtabEl && subtabEl.dataset && subtabEl.dataset.questCat) {
      var cat = subtabEl.dataset.questCat;
      if (activeSubTab !== cat) {
        activeSubTab = cat;
        lastStateKey = null;
        paint();
      }
      return;
    }
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
