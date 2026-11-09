(function () {
  "use strict";

  var MAX_FRIENDS = 200;
  var MAX_NAME = 128;
  var MAX_ID = 64;

  function esc(value) {
    return String(value).replace(/[&<>"']/g, function (ch) {
      return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[ch];
    });
  }

  function text(value, limit) {
    return typeof value === "string" && value.length > 0 && value.length <= limit ? value : "";
  }

  function normalize(state) {
    var source = state && typeof state === "object" ? state : {};
    var result = { supported: source.supported === true, friends: [], incoming: [], outgoing: [] };
    if (Array.isArray(source.friends)) {
      for (var i = 0; i < source.friends.length && result.friends.length < MAX_FRIENDS; i += 1) {
        var friend = source.friends[i];
        if (!friend || typeof friend !== "object") continue;
        var friendId = text(friend.friendId, MAX_ID);
        var name = text(friend.name, MAX_NAME);
        if (!friendId || !name) continue;
        result.friends.push({ friendId: friendId, name: name, online: friend.online === true });
      }
    }
    if (Array.isArray(source.pendingIncoming)) {
      for (var j = 0; j < source.pendingIncoming.length && result.incoming.length < 50; j += 1) {
        var request = source.pendingIncoming[j];
        if (!request || typeof request !== "object") continue;
        var requestId = text(request.requestId, MAX_ID);
        if (!requestId) continue;
        result.incoming.push({ requestId: requestId, displayName: text(request.displayName, MAX_NAME) || "Stranger" });
      }
    }
    if (Array.isArray(source.pendingOutgoingPlayerIds)) {
      for (var k = 0; k < source.pendingOutgoingPlayerIds.length && result.outgoing.length < 50; k += 1) {
        var playerId = source.pendingOutgoingPlayerIds[k];
        if (typeof playerId === "number" && isFinite(playerId) && playerId > 0) result.outgoing.push(playerId);
      }
    }
    return result;
  }

  function findPlayerId(roster, name) {
    var wanted = typeof name === "string" ? name.trim().toLowerCase() : "";
    if (!wanted || wanted === "stranger" || !Array.isArray(roster)) return 0;
    for (var i = 0; i < roster.length; i += 1) {
      var entry = roster[i];
      if (entry && typeof entry.name === "string" && entry.name.toLowerCase() === wanted && typeof entry.playerId === "number" && entry.playerId > 0) {
        return entry.playerId;
      }
    }
    return 0;
  }

  function addFormHtml() {
    return '<div class="faction-panel friends-add"><h3>ADD A FRIEND</h3>' +
      '<input id="coFriendName" type="text" maxlength="' + MAX_NAME + '" autocomplete="off" spellcheck="false" placeholder="Player name" />' +
      '<button type="button" class="faction-button" data-op="addFriendByName">Send request</button>' +
      '<p class="faction-muted" id="coFriendAddStatus" aria-live="polite"></p></div>';
  }

  function renderHtml(state) {
    var model = normalize(state);
    if (!model.supported) {
      return '<div class="faction-panel"><h2>FRIENDS</h2><p class="faction-muted">This server does not support friends.</p></div>';
    }
    var html = ['<div class="faction-panel friends-panel"><h2>FRIENDS</h2>'];
    if (model.incoming.length > 0) {
      html.push('<div class="faction-section"><h3>FRIEND REQUESTS</h3><ul class="friends-list">');
      model.incoming.forEach(function (request) {
        html.push(
          '<li class="friends-row" data-request="' + esc(request.requestId) + '">' +
            '<span class="friends-name">' + esc(request.displayName) + " wants to add you as a friend" + "</span>" +
            '<span class="friends-actions">' +
              '<button type="button" class="faction-button" data-op="friendAccept" data-request="' + esc(request.requestId) + '">Accept</button>' +
              '<button type="button" class="faction-button" data-op="friendDecline" data-request="' + esc(request.requestId) + '">Decline</button>' +
            "</span>" +
          "</li>"
        );
      });
      html.push("</ul></div>");
    }
    html.push('<div class="faction-section"><h3>YOUR FRIENDS</h3>');
    if (model.friends.length === 0) {
      html.push('<p class="faction-muted">You have not added any friends yet.</p>');
    } else {
      html.push('<ul class="friends-list">');
      model.friends.forEach(function (friend) {
        html.push(
          '<li class="friends-row">' +
            '<span class="friends-name">' + esc(friend.name) + "</span>" +
            '<span class="friends-status' + (friend.online ? " is-online" : "") + '">' + (friend.online ? "Online" : "Offline") + "</span>" +
            '<span class="friends-actions">' +
              '<button type="button" class="faction-button" data-op="friendRemove" data-friend="' + esc(friend.friendId) + '">Remove</button>' +
            "</span>" +
          "</li>"
        );
      });
      html.push("</ul>");
    }
    if (model.outgoing.length > 0) {
      html.push('<p class="faction-muted">' + model.outgoing.length + " friend request" + (model.outgoing.length === 1 ? "" : "s") + " waiting for an answer.</p>");
    }
    html.push("</div></div>");
    return html.join("");
  }

  function send(action) {
    if (typeof window.coFactionAction === "function") {
      window.coFactionAction(JSON.stringify(action));
    }
  }

  var panel = null;
  var tabButton = null;
  var latest = null;
  var roster = [];

  function normalizePayload(value) {
    if (typeof value === "string") {
      try {
        value = JSON.parse(value);
      } catch (err) {
        return null;
      }
    }
    return value && typeof value === "object" ? value : null;
  }

  function paint() {
    if (!panel) return;
    var input = document.getElementById("coFriendName");
    var status = document.getElementById("coFriendAddStatus");
    var typed = input ? input.value : "";
    var message = status ? status.textContent : "";
    var focused = input && document.activeElement === input;
    panel.innerHTML = (normalize(latest).supported ? addFormHtml() : "") + renderHtml(latest);
    input = document.getElementById("coFriendName");
    status = document.getElementById("coFriendAddStatus");
    if (input) {
      input.value = typed;
      if (focused && typeof input.focus === "function") input.focus();
    }
    if (status) status.textContent = message;
  }

  function init() {
    var nav = document.querySelector(".faction-tabs");
    if (!nav) return;
    tabButton = document.createElement("button");
    tabButton.type = "button";
    tabButton.className = "faction-tab";
    tabButton.id = "coTabFriends";
    tabButton.setAttribute("role", "tab");
    tabButton.setAttribute("aria-selected", "false");
    tabButton.textContent = "Friends";
    nav.appendChild(tabButton);

    panel = document.createElement("section");
    panel.className = "faction";
    panel.id = "coFriends";
    panel.hidden = true;
    panel.setAttribute("aria-live", "polite");
    var host = document.getElementById("coFaction");
    if (host && host.parentNode) host.parentNode.insertBefore(panel, host.nextSibling);

    nav.addEventListener("click", function (event) {
      var target = event.target && typeof event.target.closest === "function"
        ? event.target.closest("[role=tab]")
        : null;
      if (!target || !target.id || !window.coFactionTabs) return;
      window.coFactionTabs.show(target.id);
    });

    if (window.coFactionTabs && typeof window.coFactionTabs.registerPanel === "function") {
      window.coFactionTabs.registerPanel("coTabFriends", paint);
    }

    panel.addEventListener("click", function (event) {
      var target = event.target.closest("[data-op]");
      if (!target) return;
      var op = target.getAttribute("data-op");
      if (op === "addFriendByName") {
        var input = document.getElementById("coFriendName");
        var status = document.getElementById("coFriendAddStatus");
        var typed = input ? input.value.trim() : "";
        var playerId = findPlayerId(roster, typed);
        if (playerId) {
          send({ op: "friendAdd", playerId: playerId });
          if (input) input.value = "";
          if (status) status.textContent = "Friend request queued for " + typed + ".";
        } else if (status) {
          status.textContent = "No online player named that. You can only look up players whose name you know.";
        }
      } else if (op === "friendAccept" || op === "friendDecline") {
        send({ op: op, requestId: target.getAttribute("data-request") || "" });
      } else if (op === "friendRemove") {
        send({ op: op, friendId: target.getAttribute("data-friend") || "" });
      }
    });

    var previous = window.coFactionSetState;
    window.coFactionSetState = function (state) {
      latest = normalizePayload(state && typeof state === "object" ? state.friends : null);
      roster = state && Array.isArray(state.roster) ? state.roster : [];
      if (typeof previous === "function") previous(state);
      if (panel && !panel.hidden) paint();
    };
    paint();
  }

  window.coFriendsModel = { normalize: normalize, renderHtml: renderHtml, findPlayerId: findPlayerId };

  if (typeof document !== "undefined") {
    if (document.readyState === "loading") {
      document.addEventListener("DOMContentLoaded", init);
    } else {
      init();
    }
  }
})();
