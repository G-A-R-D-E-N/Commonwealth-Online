(function () {
  "use strict";

  var gamepad = false;
  var KEYBOARD_KEYS = { A: "Enter", B: "Esc", X: "R", Y: "F", LB: "Q", RB: "E" };
  window.CO_ControllerIcons = {
    setInputMode: function (mode) { gamepad = mode === "gamepad"; },
    render: function (key) {
      if (!gamepad) return '<span class="co-prompt-key">' + (KEYBOARD_KEYS[key] || key) + "</span>";
      if (window.CO_ControllerGlyphs && CO_ControllerGlyphs.render) {
        return CO_ControllerGlyphs.render(key);
      }
      return '<span class="co-prompt-key co-prompt-key--fallback">' + key + "</span>";
    }
  };
})();
