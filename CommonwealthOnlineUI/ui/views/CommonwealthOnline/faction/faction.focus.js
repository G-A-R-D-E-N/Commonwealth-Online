(function () {
  "use strict";

  function cssEscape(value) {
    var text = String(value);
    if (window.CSS && typeof window.CSS.escape === "function") {
      return window.CSS.escape(text);
    }
    return Array.from(text).map(function (ch) {
      return /[A-Za-z0-9_-]/.test(ch) ? ch : "\\" + ch.codePointAt(0).toString(16) + " ";
    }).join("");
  }

  function focusKey(element) {
    if (!element) {
      return "";
    }
    if (element.id) {
      return "#" + cssEscape(element.id);
    }
    if (!element.dataset || !element.dataset.op) {
      return "";
    }
    return Object.keys(element.dataset).map(function (key) {
      var attribute = key.replace(/[A-Z]/g, function (c) {
        return "-" + c.toLowerCase();
      });
      return '[data-' + attribute + '="' + cssEscape(element.dataset[key]) + '"]';
    }).join("");
  }

  function isKeyboardOpen() {
    return !!(window.CO_Keyboard && window.CO_Keyboard.isOpen());
  }

  function restoreFocus(root, key, caret) {
    try {
      var target = key ? root.querySelector(key) : null;
      if (!target) {
        return;
      }
      if (typeof target.focus === "function") {
        target.focus();
      }
      if (caret && typeof target.setSelectionRange === "function") {
        target.setSelectionRange(caret[0], caret[1]);
      }
    } catch (err) {
      return;
    }
  }

  window.coFactionFocus = { focusKey: focusKey, isKeyboardOpen: isKeyboardOpen, restoreFocus: restoreFocus };
})();
