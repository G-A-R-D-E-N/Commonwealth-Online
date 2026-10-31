(function () {
  "use strict";

  var MAX_POINTS = 8;
  var MAX_FRIENDS = 32;
  var MAX_LABEL = 48;
  var MAX_NAME = 32;
  var MAX_ID = 64;
  var PENDING_TIMEOUT_MS = 5000;
  var FALLBACK_NAME = "a friend";

  var CONTROLLER_ACTIONS = {
    "co.select": "a",
    "co.navUp": "dpad_up",
    "co.navDown": "dpad_down"
  };

  var root = null;
  var state = { phase: "hidden", points: [], friends: [] };
  var pending = false;
  var pendingTimer = null;
  var lastStateKey = null;

  function esc(text) {
    return String(text == null ? "" : text)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function text(value, maxLength) {
    return String(value == null ? "" : value).slice(0, maxLength);
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

    var points = [];
    if (Array.isArray(next.points)) {
      next.points.forEach(function (candidate) {
        if (points.length >= MAX_POINTS || !candidate || typeof candidate !== "object") {
          return;
        }
        var id = candidate.id === "sanctuary" || candidate.id === "lastbed" || candidate.id === "checkpoint" ?
          candidate.id :
          null;
        if (!id) {
          return;
        }
        points.push({
          id: id,
          label: text(candidate.label, MAX_LABEL),
          available: candidate.available === true
        });
      });
    }

    var friends = [];
    if (Array.isArray(next.friends)) {
      next.friends.forEach(function (candidate) {
        if (friends.length >= MAX_FRIENDS || !candidate || typeof candidate !== "object") {
          return;
        }
        var id = text(candidate.id, MAX_ID);
        if (id === "") {
          return;
        }
        friends.push({ id: id, name: text(candidate.name, MAX_NAME) });
      });
    }

    return {
      phase: next.phase === "dead" ? "dead" : "hidden",
      points: points,
      friends: friends
    };
  }

  function buttonHtml(attributes, label, primary, disabled) {
    return (
      '<button type="button" class="co-respawn-btn' + (primary ? " is-primary" : "") + '"' +
      attributes +
      (disabled ? " disabled" : "") +
      ">" +
      esc(label) +
      "</button>"
    );
  }

  function pointsHtml(points) {
    if (!points.length) {
      return '<p class="co-respawn-muted">No respawn points are available.</p>';
    }
    return (
      '<div class="co-respawn-list">' +
      points
        .map(function (point) {
          return buttonHtml(
            ' data-point="' + esc(point.id) + '"',
            point.label || point.id,
            point.available,
            !point.available || pending
          );
        })
        .join("") +
      "</div>"
    );
  }

  function friendsHtml(friends) {
    if (!friends.length) {
      return '<p class="co-respawn-muted">No friends are online.</p>';
    }
    return (
      '<div class="co-respawn-list">' +
      friends
        .map(function (friend) {
          return buttonHtml(
            ' data-point="friend" data-friend-id="' + esc(friend.id) + '"',
            "Respawn next to " + (friend.name || FALLBACK_NAME),
            false,
            pending
          );
        })
        .join("") +
      "</div>"
    );
  }

  function render(s) {
    if (s.phase !== "dead") {
      return "";
    }
    return (
      '<section class="co-respawn-panel" role="dialog" aria-label="You died">' +
      '<h1 class="co-respawn-title">YOU DIED</h1>' +
      '<p class="co-respawn-note">' +
      (pending ? "Respawning…" : "Choose where to respawn.") +
      "</p>" +
      '<div class="co-respawn-section"><h2>RESPAWN POINT</h2>' +
      pointsHtml(s.points) +
      "</div>" +
      '<div class="co-respawn-section"><h2>RESPAWN NEXT TO A FRIEND</h2>' +
      friendsHtml(s.friends) +
      "</div>" +
      "</section>"
    );
  }

  function focusables() {
    if (!root || !root.querySelectorAll) {
      return [];
    }
    return Array.prototype.slice.call(root.querySelectorAll(".co-respawn-btn:not([disabled])"));
  }

  function focusFirst() {
    var items = focusables();
    if (items.length && document.activeElement !== items[0] && typeof items[0].focus === "function") {
      items[0].focus();
    }
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
    if (typeof items[index].focus === "function") {
      items[index].focus();
    }
  }

  function activateFocused() {
    var active = document.activeElement;
    if (!active || active.tagName !== "BUTTON" || active.disabled) {
      return;
    }
    choose(active);
  }

  function release(repaint) {
    if (pendingTimer !== null) {
      clearTimeout(pendingTimer);
      pendingTimer = null;
    }
    if (pending) {
      pending = false;
      if (repaint) {
        paint();
      }
    }
  }

  function send(point, friendId) {
    if (pending) {
      return;
    }
    pending = true;
    paint();
    pendingTimer = setTimeout(function () {
      pendingTimer = null;
      release(true);
    }, PENDING_TIMEOUT_MS);
    if (typeof window.coDownedAction === "function") {
      window.coDownedAction(JSON.stringify({ action: "respawn", point: point, friendId: friendId || "" }));
    }
  }

  function choose(element) {
    var point = element.getAttribute("data-point");
    if (!point) {
      return;
    }
    send(point, element.getAttribute("data-friend-id") || "");
  }

  function onRootClick(event) {
    var target = event.target;
    var element = target && target.closest ? target.closest(".co-respawn-btn") : null;
    if (!element || element.disabled) {
      return;
    }
    choose(element);
  }

  function paint() {
    if (!root) {
      return;
    }
    root.innerHTML = render(state);
    focusFirst();
  }

  function setState(raw) {
    var next = normalize(raw);
    if (!next) {
      return false;
    }
    var key = JSON.stringify(next);
    state = next;
    release(key === lastStateKey && next.phase === "dead");
    if (key !== lastStateKey) {
      lastStateKey = key;
      paint();
    }
    return true;
  }

  function handleButton(button) {
    if (button === "a") {
      activateFocused();
    } else if (button === "dpad_up") {
      moveFocus(-1);
    } else if (button === "dpad_down") {
      moveFocus(1);
    }
  }

  function onKeydown(event) {
    if (event.key === "ArrowUp") {
      event.preventDefault();
      handleButton("dpad_up");
    } else if (event.key === "ArrowDown") {
      event.preventDefault();
      handleButton("dpad_down");
    } else if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      handleButton("a");
    }
  }

  function onPrismaControllerAction(event) {
    var detail = event && event.detail;
    if (!detail || detail.state !== "pressed") {
      return;
    }
    var button = CONTROLLER_ACTIONS[detail.action];
    if (button) {
      handleButton(button);
    }
  }

  function updateUiScale() {
    var scale = Math.max(0.75, Math.min(1.35, window.innerWidth / 1920));
    document.documentElement.style.setProperty("--co-ui-scale", String(scale));
  }

  function init() {
    root = document.getElementById("coRespawn");
    if (!root) {
      return;
    }
    root.addEventListener("click", onRootClick);
    document.addEventListener("keydown", onKeydown);
    window.addEventListener("prisma-controller-action", onPrismaControllerAction);
    updateUiScale();
    paint();
  }

  window.coRespawnSetState = setState;
  window.coRespawnModel = {
    normalize: normalize,
    render: render,
    moveFocus: moveFocus,
    handleButton: handleButton
  };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
