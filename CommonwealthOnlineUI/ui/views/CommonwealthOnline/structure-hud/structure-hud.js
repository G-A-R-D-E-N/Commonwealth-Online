(function () {
  "use strict";

  var HIDE_AFTER_MS = 4000;
  var HIDDEN = { visible: false, tier: "red", percent: 0 };
  var hideTimer = null;

  function isObject(value) {
    return !!value && typeof value === "object" && !Array.isArray(value);
  }

  function isPositiveNumber(value) {
    return typeof value === "number" && Number.isFinite(value) && value > 0;
  }

  function tierFor(ratio) {
    if (ratio > 0.5) {
      return "green";
    }
    return ratio > 0.25 ? "amber" : "red";
  }

  function describeHealth(payload) {
    if (!isObject(payload) || !isPositiveNumber(payload.health) || !isPositiveNumber(payload.maxHealth)) {
      return HIDDEN;
    }
    var ratio = Math.min(1, payload.health / payload.maxHealth);
    return { visible: true, tier: tierFor(ratio), percent: Math.round(ratio * 100) };
  }

  function paint(view) {
    var root = document.getElementById("coStructureHud");
    var bar = document.getElementById("coStructureBar");
    var fill = document.getElementById("coStructureFill");
    var value = document.getElementById("coStructureValue");
    if (!root || !bar || !fill || !value) {
      return;
    }
    root.hidden = !view.visible;
    root.setAttribute("data-tier", view.tier);
    fill.style.width = view.percent + "%";
    bar.setAttribute("aria-valuenow", String(view.percent));
    value.textContent = view.visible ? view.percent + "%" : "";
  }

  function cancelHide() {
    if (hideTimer !== null) {
      clearTimeout(hideTimer);
      hideTimer = null;
    }
  }

  window.coStructureSetState = function (json) {
    var payload = null;
    try {
      payload = JSON.parse(json);
    } catch (error) {
      payload = null;
    }
    var view = describeHealth(payload);
    cancelHide();
    paint(view);
    if (view.visible) {
      hideTimer = setTimeout(function () {
        hideTimer = null;
        paint(HIDDEN);
      }, HIDE_AFTER_MS);
    }
  };
  window.coStructureHudModel = {
    describeHealth: describeHealth
  };
})();
