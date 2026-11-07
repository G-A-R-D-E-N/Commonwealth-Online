(function () {
  "use strict";

  var ROOT_ID = "coNameplates";
  var elements = {};

  function isUnit(value) {
    return typeof value === "number" && value >= 0 && value <= 1;
  }

  function isLabel(entry) {
    return (
      !!entry &&
      typeof entry === "object" &&
      typeof entry.id === "number" &&
      typeof entry.text === "string" &&
      isUnit(entry.x) &&
      isUnit(entry.y) &&
      isUnit(entry.opacity)
    );
  }

  function parseLabels(json) {
    var parsed;
    try {
      parsed = JSON.parse(json);
    } catch (error) {
      return [];
    }
    return Array.isArray(parsed) ? parsed.filter(isLabel) : [];
  }

  function applyLabels(root, list) {
    var keep = {};
    list.forEach(function (entry) {
      var key = String(entry.id);
      var element = elements[key];
      keep[key] = true;
      if (!element) {
        element = document.createElement("div");
        element.className = "co-nameplate";
        elements[key] = element;
        root.appendChild(element);
      }
      element.textContent = entry.text;
      element.style.left = entry.x * 100 + "%";
      element.style.top = entry.y * 100 + "%";
      element.style.opacity = String(entry.opacity);
    });
    Object.keys(elements).forEach(function (key) {
      if (!keep[key]) {
        root.removeChild(elements[key]);
        delete elements[key];
      }
    });
  }

  function setLabels(json) {
    var root = document.getElementById(ROOT_ID);
    if (!root) {
      return;
    }
    applyLabels(root, parseLabels(json));
  }

  window.coNameplatesSet = setLabels;
  window.coNameplatesModel = {
    parseLabels: parseLabels,
    isLabel: isLabel
  };
})();
