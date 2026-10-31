(function () {
  "use strict";

  function clampByte(value) {
    return Math.max(0, Math.min(255, Math.round(value)));
  }

  function applyCursorTint(root, color) {
    if (!color) {
      return;
    }

    var r = clampByte(color.r);
    var g = clampByte(color.g);
    var b = clampByte(color.b);
    var svg =
      '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32.3 31.24">' +
      '<polygon fill="rgb(' + r + "," + g + "," + b + ')" stroke="#000" stroke-width="3.2" ' +
      'stroke-miterlimit="10" points="17.4 30.41 31.46 15.9 1.2 1.14 17.4 30.41"/></svg>';
    var url =
      'url("data:image/svg+xml,' +
      encodeURIComponent(svg) +
      '") 1 1, default';
    root.style.setProperty("--co-cursor", url);
  }

  window.applyCoTheme = function (theme) {
    if (!theme) {
      return;
    }

    var root = document.documentElement;
    var hud = theme.hud || theme;
    applyCursorTint(root, hud);

    if (window.CO_ControllerGlyphs && CO_ControllerGlyphs.onThemeApplied) {
      CO_ControllerGlyphs.onThemeApplied();
    }

    console.log(
      "[CO] Applied browser cursor theme rgb(" +
        clampByte(hud.r) + "," +
        clampByte(hud.g) + "," +
        clampByte(hud.b) + ")"
    );
  };
})();
