(function () {
  "use strict";

  var MAX_OFFER_LINES = 16;
  var SHIFT_STEP = 5;
  var trade = null;
  var holdings = [];

  function byId(id) {
    return document.getElementById(id);
  }

  function send(payload) {
    if (typeof window.coTradeAction === "function") {
      window.coTradeAction(JSON.stringify(payload));
    }
  }

  function element(tag, className, value) {
    var node = document.createElement(tag);
    if (className) {
      node.className = className;
    }
    node.textContent = value;
    return node;
  }

  function partnerName() {
    return trade.partnerName || "Player " + trade.partnerPlayerId;
  }

  function offerLine(lines, itemKey) {
    for (var i = 0; i < lines.length; i++) {
      if (lines[i].itemKey === itemKey) {
        return lines[i];
      }
    }
    return null;
  }

  function changeOffer(itemKey, direction, limit, amount) {
    var lines = trade.ownOffer.map(function (line) {
      return { itemKey: line.itemKey, count: line.count };
    });
    var entry = offerLine(lines, itemKey);
    var current = entry ? entry.count : 0;
    var next = direction > 0 ? Math.min(current + amount, limit) : Math.max(current - amount, 0);
    if (next === current || (!entry && lines.length >= MAX_OFFER_LINES)) {
      return;
    }
    if (entry) {
      entry.count = next;
    } else {
      lines.push({ itemKey: itemKey, count: next });
    }
    send({
      op: "offer",
      tradeId: trade.tradeId,
      lines: lines.filter(function (line) {
        return line.count > 0;
      })
    });
  }

  function canAdd(holding) {
    var entry = offerLine(trade.ownOffer, holding.itemKey);
    var owned = entry ? entry.count : 0;
    return owned < holding.count && (owned > 0 || trade.ownOffer.length < MAX_OFFER_LINES);
  }

  function row(line, hint, activate, enabled, key) {
    var item = document.createElement("li");
    item.className = "trade-row";
    item.appendChild(element("span", "trade-line-name", line.name || line.itemKey));
    item.appendChild(element("span", "trade-line-count", "x" + line.count));
    if (!activate) {
      return item;
    }
    item.appendChild(element("span", "trade-row-hint", hint));
    if (!enabled) {
      item.className += " is-disabled";
      return item;
    }
    item.className += " is-enabled";
    item.tabIndex = 0;
    item.setAttribute("data-key", key);
    item.addEventListener("click", function (event) {
      activate(event.shiftKey);
    });
    item.addEventListener("keydown", function (event) {
      if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        activate(event.shiftKey);
      }
    });
    return item;
  }

  function fill(list, rows, emptyText) {
    list.textContent = "";
    if (rows.length === 0) {
      list.appendChild(element("li", "trade-empty", emptyText));
      return;
    }
    rows.forEach(function (item) {
      list.appendChild(item);
    });
  }

  function badge(id, label, accepted) {
    var node = byId(id);
    node.textContent = label + ": " + (accepted ? "Accepted" : "Not accepted yet");
    node.className = "trade-badge" + (accepted ? " is-accepted" : "");
  }

  function statusText() {
    if (trade.phase !== "requested") {
      return "Negotiating: add items, then press Accept Trade";
    }
    return trade.role === "target"
      ? partnerName() + " wants to trade with you"
      : "Waiting for " + partnerName() + " to accept your request";
  }

  function focusables() {
    return Array.prototype.slice.call(document.querySelectorAll("#coTrade .trade-row.is-enabled, #coTrade .trade-actions button")).filter(function (el) {
      return !el.disabled && !el.hidden;
    });
  }

  function focusKey(el) {
    return el ? el.getAttribute("data-key") || el.id : "";
  }

  function moveFocus(step) {
    var targets = focusables();
    if (targets.length === 0) {
      return;
    }
    var index = targets.indexOf(document.activeElement);
    var next = index < 0 ? (step > 0 ? 0 : targets.length - 1) : (index + step + targets.length) % targets.length;
    targets[next].focus();
  }

  function render() {
    byId("coTrade").hidden = !trade;
    if (!trade) {
      return;
    }
    var active = focusKey(document.activeElement);
    var negotiating = trade.phase === "negotiating";
    var incoming = trade.phase === "requested" && trade.role === "target";
    var partner = partnerName();
    byId("coTradeTitle").textContent = "Trade with " + partner;
    byId("coTradeStatus").textContent = statusText();
    badge("coTradeOwnBadge", "You", trade.ownConfirmed);
    badge("coTradePartnerBadge", partner, trade.partnerConfirmed);
    byId("coTradePartnerTitle").textContent = partner + "'s offer";
    fill(byId("coTradeHoldings"), holdings.map(function (holding) {
      return row(holding, "+", function (shift) {
        changeOffer(holding.itemKey, 1, holding.count, shift ? SHIFT_STEP : 1);
      }, negotiating && canAdd(holding), "h:" + holding.itemKey);
    }), "You have no items");
    fill(byId("coTradeOwnOffer"), trade.ownOffer.map(function (line) {
      return row(line, "-", function (shift) {
        changeOffer(line.itemKey, -1, line.count, shift ? SHIFT_STEP : 1);
      }, negotiating, "o:" + line.itemKey);
    }), "Nothing offered yet");
    fill(byId("coTradePartnerOffer"), trade.partnerOffer.map(function (line) {
      return row(line, "", null, false, "");
    }), "Nothing offered yet");
    byId("coTradeAccept").hidden = !incoming;
    byId("coTradeDecline").hidden = !incoming;
    var confirmButton = byId("coTradeConfirm");
    confirmButton.hidden = !negotiating;
    confirmButton.disabled = Boolean(trade.ownConfirmed);
    confirmButton.textContent = trade.ownConfirmed ? "Waiting for " + partner + "..." : "Accept Trade";
    if (active) {
      var restored = focusables().filter(function (el) {
        return focusKey(el) === active;
      })[0];
      if (restored) {
        restored.focus();
      }
    }
  }

  function respond(accept) {
    if (trade) {
      send({ op: "respond", tradeId: trade.tradeId, accept: accept });
    }
  }

  byId("coTradeHide").addEventListener("click", function () {
    send({ op: "hide" });
  });
  byId("coTradeAccept").addEventListener("click", function () {
    respond(true);
  });
  byId("coTradeDecline").addEventListener("click", function () {
    respond(false);
  });
  byId("coTradeConfirm").addEventListener("click", function () {
    if (trade) {
      send({ op: "confirm", tradeId: trade.tradeId, offerVersion: trade.offerVersion });
    }
  });
  byId("coTradeCancel").addEventListener("click", function () {
    if (trade) {
      send({ op: "cancel", tradeId: trade.tradeId });
    }
  });

  function handleControllerInput(button, phase) {
    if (phase !== "down" || !trade) {
      return;
    }
    if (button === "b") {
      send({ op: "cancel", tradeId: trade.tradeId });
    } else if (button === "dpad_up" || button === "dpad_left") {
      moveFocus(-1);
    } else if (button === "dpad_down" || button === "dpad_right") {
      moveFocus(1);
    } else if (button === "a") {
      var focused = document.activeElement;
      if (focused && focusables().indexOf(focused) >= 0) {
        focused.click();
      } else {
        moveFocus(1);
      }
    }
  }

  document.addEventListener("keydown", function (event) {
    if (event.key === "Escape" && trade) {
      event.preventDefault();
      send({ op: "cancel", tradeId: trade.tradeId });
    }
  });

  window.handleControllerInput = handleControllerInput;

  window.coTradeSetState = function (next) {
    if (next && next.visible) {
      trade = next;
      holdings = Array.isArray(next.holdings) ? next.holdings : [];
    } else {
      trade = null;
      holdings = [];
    }
    render();
  };

  render();
})();
