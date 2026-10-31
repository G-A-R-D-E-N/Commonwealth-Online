(function () {
  "use strict";

  window.setWorldModeInfo = function (payload) {
    var element = document.getElementById("serverModeBadge");
    if (!element) {
      return;
    }
    var text = payload && typeof payload.label === "string" ? payload.label : "";
    element.textContent = text;
    element.hidden = text === "";
  };
})();
