(function () {
  "use strict";

  var REF_W = 1920;
  var REF_H = 1080;

  // Shared --co-ui-scale for main menu + embedded browser/settings.
  // Floor at 1 so hosts that previously used a sub-1 scale do not jump
  // when this view opens and refreshes the variable.
  function updateUiScale() {
    var w = window.innerWidth || REF_W;
    var h = window.innerHeight || REF_H;
    var scale = Math.max(1, Math.min(w / REF_W, h / REF_H));
    document.documentElement.style.setProperty("--co-ui-scale", String(scale));
  }

  window.CO_Layout = {
    update: updateUiScale
  };

  window.addEventListener("resize", updateUiScale);
  updateUiScale();
})();
