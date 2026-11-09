(function () {
  "use strict";

  var MAX_CART_LINES = 64;
  var vendor = null;
  var cart = {};
  var baseSetState = window.coTradeSetState;
  var baseController = window.handleControllerInput;

  function byId(id) {
    return document.getElementById(id);
  }

  function send(payload) {
    if (typeof window.coTradeAction === "function") {
      window.coTradeAction(JSON.stringify(payload));
    }
  }

  function node(tag, className, text) {
    var item = document.createElement(tag);
    if (className) {
      item.className = className;
    }
    item.textContent = text;
    return item;
  }

  function find(list, formId) {
    for (var i = 0; i < list.length; i++) {
      if (list[i].formId === formId) {
        return list[i];
      }
    }
    return null;
  }

  function cartKey(buy, formId) {
    return (buy ? "b:" : "s:") + formId;
  }

  function lineOf(key) {
    var buy = key.charAt(0) === "b";
    var formId = Number(key.slice(2));
    return { buy: buy, formId: formId, entry: find(buy ? vendor.stock : vendor.inventory, formId) };
  }

  function prune() {
    Object.keys(cart).forEach(function (key) {
      var line = lineOf(key);
      if (!line.entry || Math.abs(cart[key]) > line.entry.count) {
        delete cart[key];
      }
    });
  }

  function total() {
    var sum = 0;
    Object.keys(cart).forEach(function (key) {
      var line = lineOf(key);
      sum -= cart[key] * (line.entry ? line.entry.price : 0);
    });
    return sum;
  }

  function add(formId, buy, step, limit) {
    var key = cartKey(buy, formId);
    var current = cart[key] || 0;
    var next = current + step;
    if (Math.abs(next) > limit) {
      return;
    }
    if (current === 0 && Object.keys(cart).length >= MAX_CART_LINES) {
      return;
    }
    delete cart[cartKey(!buy, formId)];
    if (next === 0) {
      delete cart[key];
    } else {
      cart[key] = next;
    }
    render();
  }

  function listRow(entry, onClick) {
    var item = node("li", "", "");
    item.tabIndex = 0;
    item.appendChild(node("span", "trade-line-name", entry.name));
    item.appendChild(node("span", "trade-line-count", "x" + entry.count + " - " + entry.price + " caps"));
    item.addEventListener("click", onClick);
    item.addEventListener("keydown", function (event) {
      if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        onClick();
      }
    });
    return item;
  }

  function fillList(id, entries, buy) {
    var list = byId(id);
    list.textContent = "";
    var shownAny = false;
    entries.forEach(function (entry) {
      var inCart = Math.abs(cart[cartKey(buy, entry.formId)] || 0);
      var left = entry.count - inCart;
      if (left <= 0) {
        return;
      }
      shownAny = true;
      list.appendChild(listRow({ name: entry.name, price: entry.price, count: left }, function () {
        add(entry.formId, buy, buy ? 1 : -1, entry.count);
      }));
    });
    if (!shownAny) {
      list.appendChild(node("li", "vendor-empty", "Nothing here"));
    }
  }

  function render() {
    byId("coVendor").hidden = !vendor;
    if (!vendor) {
      return;
    }
    byId("coVendorTitle").textContent = vendor.vendorName || "Trader";
    byId("coVendorStatus").textContent = vendor.status || "Click an item to put it in the cart.";
    byId("coVendorCaps").textContent = "Trader caps: " + vendor.vendorCaps;
    byId("coVendorPlayerCaps").textContent = "Your caps: " + vendor.playerCaps;
    fillList("coVendorStock", vendor.stock, true);
    fillList("coVendorInventory", vendor.inventory, false);
    var cartList = byId("coVendorCart");
    cartList.textContent = "";
    var keys = Object.keys(cart);
    if (keys.length === 0) {
      cartList.appendChild(node("li", "vendor-empty", "Cart is empty"));
    }
    keys.forEach(function (key) {
      var line = lineOf(key);
      var delta = cart[key];
      var item = node("li", "", "");
      item.appendChild(node("span", "trade-line-name", (line.buy ? "Buy " : "Sell ") + (line.entry ? line.entry.name : line.formId)));
      item.appendChild(node("span", "trade-line-count", "x" + Math.abs(delta)));
      var remove = node("button", "", "-");
      remove.type = "button";
      remove.addEventListener("click", function () {
        add(line.formId, line.buy, delta > 0 ? -1 : 1, Math.abs(delta));
      });
      item.appendChild(remove);
      cartList.appendChild(item);
    });
    var net = total();
    byId("coVendorTotal").textContent = net >= 0 ? "You receive " + net + " caps" : "You pay " + -net + " caps";
    byId("coVendorConfirm").disabled = keys.length === 0 || Boolean(vendor.busy);
    byId("coVendorClear").disabled = keys.length === 0;
    byId("coVendorClose").disabled = !vendor.canClose;
  }

  function confirmTrade() {
    if (!vendor || Object.keys(cart).length === 0 || vendor.busy) {
      return;
    }
    var lines = Object.keys(cart).map(function (key) {
      return { formId: lineOf(key).formId, delta: cart[key] };
    });
    send({ op: "vendorConfirm", tradeId: vendor.tradeId, lines: lines });
  }

  function closeVendor() {
    if (vendor && !vendor.canClose) {
      return;
    }
    send({ op: "vendorClose", tradeId: vendor ? vendor.tradeId : "" });
  }

  byId("coVendorClear").addEventListener("click", function () {
    cart = {};
    render();
  });
  byId("coVendorConfirm").addEventListener("click", confirmTrade);
  byId("coVendorClose").addEventListener("click", closeVendor);

  document.addEventListener("keydown", function (event) {
    if (event.key === "Escape" && vendor) {
      event.preventDefault();
      closeVendor();
    }
  });

  window.coTradeSetState = function (next) {
    if (next && next.visible && next.kind === "vendor") {
      var sameVendor = vendor && vendor.tradeId === next.tradeId;
      vendor = next;
      vendor.stock = Array.isArray(next.stock) ? next.stock : [];
      vendor.inventory = Array.isArray(next.inventory) ? next.inventory : [];
      if (!sameVendor || next.clearCart) {
        cart = {};
      }
      prune();
      byId("coTrade").hidden = true;
      render();
      return;
    }
    vendor = null;
    cart = {};
    render();
    baseSetState(next);
  };

  window.handleControllerInput = function (button, phase) {
    if (!vendor) {
      baseController(button, phase);
      return;
    }
    if (phase !== "down") {
      return;
    }
    if (button === "b") {
      closeVendor();
    } else if (button === "x") {
      confirmTrade();
    } else if (button === "dpad_up" || button === "dpad_down" || button === "dpad_left" || button === "dpad_right") {
      var targets = Array.prototype.slice.call(document.querySelectorAll("#coVendor li[tabindex], #coVendor button")).filter(function (el) {
        return !el.disabled;
      });
      if (targets.length === 0) {
        return;
      }
      var step = button === "dpad_up" || button === "dpad_left" ? -1 : 1;
      var index = targets.indexOf(document.activeElement);
      targets[index < 0 ? 0 : (index + step + targets.length) % targets.length].focus();
    } else if (button === "a" && document.activeElement && document.activeElement.click) {
      document.activeElement.click();
    }
  };
})();
