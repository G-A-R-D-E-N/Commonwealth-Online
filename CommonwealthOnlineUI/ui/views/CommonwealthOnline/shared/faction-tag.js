(function () {
  "use strict";

  var MAX_CODE_POINTS = 8;

  function factionTagLabel(message) {
    var raw = message && typeof message.factionTag === "string" ? message.factionTag : "";
    var tag = raw.replace(/[\u0000-\u001f\u007f]/g, "").trim();
    return tag && Array.from(tag).length <= MAX_CODE_POINTS ? "[" + tag + "]" : "";
  }

  window.CO_FactionTag = {
    factionTagLabel: factionTagLabel
  };
})();
