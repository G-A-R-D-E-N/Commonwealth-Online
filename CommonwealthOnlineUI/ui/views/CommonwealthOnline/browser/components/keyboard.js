(function () {
  "use strict";

  var LAYOUTS = {
    lower: [
      "1234567890",
      "qwertyuiop",
      "asdfghjkl",
      "zxcvbnm"
    ],
    upper: [
      "1234567890",
      "QWERTYUIOP",
      "ASDFGHJKL",
      "ZXCVBNM"
    ],
    symbol: [
      "1234567890",
      ":-_./\\@",
      "+*=?!&%$",
      "#()[]{}<>"
    ]
  };

  var SHIFT_ORDER = ["lower", "upper", "symbol"];
  var MAX_LENGTH = 255;

  var ACTION_ROW = [
    { id: "back", label: "Del" },
    { id: "space", label: "Space" },
    { id: "done", label: "Done" }
  ];

  var state = {
    open: false,
    target: null,
    original: "",
    layout: "lower",
    row: 0,
    col: 0
  };

  function layoutRows() {
    return LAYOUTS[state.layout].map(function (line) {
      return line.split("");
    });
  }

  function isActionRow() {
    return state.row >= layoutRows().length;
  }

  function currentRowLength() {
    if (isActionRow()) return ACTION_ROW.length;
    return layoutRows()[state.row].length;
  }

  function clampSelection() {
    var rows = layoutRows().length + 1;
    if (state.row < 0) state.row = 0;
    if (state.row >= rows) state.row = rows - 1;
    var len = currentRowLength();
    if (state.col < 0) state.col = 0;
    if (state.col >= len) state.col = len - 1;
  }

  function highlightedKey() {
    if (isActionRow()) return ACTION_ROW[state.col] ? ACTION_ROW[state.col].id : null;
    var line = layoutRows()[state.row];
    return line ? line[state.col] : null;
  }

  function targetValue() {
    if (!state.target) return "";
    return state.target.value === undefined ? "" : String(state.target.value);
  }

  function notifyInput(target) {
    if (!target || typeof target.dispatchEvent !== "function") return;
    var event;
    var view = (target.ownerDocument && target.ownerDocument.defaultView) || window;
    var ctor = view && view.Event;
    if (typeof ctor === "function") {
      event = new ctor("input", { bubbles: true });
    } else {
      event = { type: "input", bubbles: true };
    }
    target.dispatchEvent(event);
  }

  function writeValue(next) {
    if (!state.target) return;
    state.target.value = next;
    notifyInput(state.target);
  }

  function pressKey(key) {
    if (!state.target) return;
    var value = targetValue();
    if (key === "space") {
      if (value.length < MAX_LENGTH) writeValue(value + " ");
      return;
    }
    if (key === "back") {
      writeValue(value.slice(0, -1));
      return;
    }
    if (key === "done") {
      close(true);
      return;
    }
    if (value.length < MAX_LENGTH) writeValue(value + key);
  }

  function cycleLayout(direction) {
    var index = SHIFT_ORDER.indexOf(state.layout);
    if (index < 0) index = 0;
    index = (index + direction + SHIFT_ORDER.length) % SHIFT_ORDER.length;
    state.layout = SHIFT_ORDER[index];
    clampSelection();
  }

  function close(commit) {
    if (!state.open) return;
    var target = state.target;
    var original = state.original;
    if (!commit && target) {
      target.value = original;
      notifyInput(target);
    }
    state.open = false;
    state.target = null;
    state.original = "";
    if (window.CO_Keyboard && typeof window.CO_Keyboard.onClosed === "function") {
      window.CO_Keyboard.onClosed();
    }
  }

  window.CO_Keyboard = {
    isOpen: function () {
      return state.open;
    },

    getTarget: function () {
      return state.target;
    },

    open: function (element) {
      if (!element) return false;
      state.open = true;
      state.target = element;
      state.original = element.value === undefined ? "" : String(element.value);
      state.layout = "lower";
      state.row = 0;
      state.col = 0;
      return true;
    },

    close: function () {
      close(true);
    },

    handleButton: function (button) {
      if (!state.open) return false;

      if (button === "b") {
        close(false);
        return true;
      }
      if (button === "lb") {
        cycleLayout(-1);
        return true;
      }
      if (button === "rb") {
        cycleLayout(1);
        return true;
      }
      if (button === "dpad_left") {
        state.col -= 1;
        clampSelection();
        return true;
      }
      if (button === "dpad_right") {
        state.col += 1;
        clampSelection();
        return true;
      }
      if (button === "dpad_up") {
        state.row -= 1;
        clampSelection();
        return true;
      }
      if (button === "dpad_down") {
        state.row += 1;
        clampSelection();
        return true;
      }
      if (button === "a") {
        var key = highlightedKey();
        if (key) pressKey(key);
        return true;
      }
      return true;
    },

    render: function (root) {
      if (!root) return;
      var host = root.querySelector(".co-keyboard");
      if (!host) return;

      if (!state.open) {
        host.classList.add("hidden");
        host.innerHTML = "";
        return;
      }

      host.classList.remove("hidden");
      var parts = [];
      layoutRows().forEach(function (line, r) {
        parts.push('<div class="co-keyboard-row">');
        line.forEach(function (key, c) {
          var cls = "co-key" + (r === state.row && c === state.col ? " focused" : "");
          parts.push('<span class="' + cls + '">' + escapeHtml(key) + "</span>");
        });
        parts.push("</div>");
      });

      parts.push('<div class="co-keyboard-row co-keyboard-actions">');
      ACTION_ROW.forEach(function (action, c) {
        var cls = "co-key co-key-wide" + (isActionRow() && c === state.col ? " focused" : "");
        parts.push('<span class="' + cls + '">' + escapeHtml(action.label) + "</span>");
      });
      parts.push("</div>");

      host.innerHTML = parts.join("");
    },

    getState: function () {
      return {
        open: state.open,
        layout: state.layout,
        row: state.row,
        col: state.col,
        value: targetValue()
      };
    }
  };

  function escapeHtml(value) {
    return String(value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }
})();
