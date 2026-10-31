(function () {
  "use strict";

  var generation = null;

  function sendCreatorAction(payload) {
    if (generation !== null) {
      payload.generation = generation;
    } else if (payload.action !== "getState") {
      return false;
    }
    var msg = JSON.stringify(payload);
    if (typeof window.coCreatorAction === "function") {
      window.coCreatorAction(msg);
      return true;
    }
    console.warn("[CO Character Creator] coCreatorAction bridge unavailable:", payload);
    return false;
  }

  window.CO_CharacterCreator = {
    send: sendCreatorAction
  };

  function selectTab(name) {
    var tabs = document.querySelectorAll(".creator__tab");
    var panels = document.querySelectorAll(".creator__tabpanel");
    Array.prototype.forEach.call(tabs, function (tab) {
      tab.classList.toggle("is-active", tab.dataset.tab === name);
    });
    Array.prototype.forEach.call(panels, function (panel) {
      panel.classList.toggle("is-active", panel.dataset.panel === name);
    });
  }

  document.getElementById("tabs").addEventListener("click", function (event) {
    var tab = event.target.closest(".creator__tab");
    if (tab) {
      selectTab(tab.dataset.tab);
    }
  });

  document.getElementById("refresh").addEventListener("click", function () {
    sendCreatorAction({ action: "getState" });
  });

  document.getElementById("confirm").addEventListener("click", function () {
    sendCreatorAction({ action: "confirm" });
  });

  document.getElementById("undo").addEventListener("click", function () {
    sendCreatorAction({ action: "undo" });
    setTimeout(function () {
      sendCreatorAction({ action: "getState" });
    }, 120);
  });

  window.onCharacterCreatorShown = function () {
    generation = null;
    window.CO_CreatorControls.build();
    sendCreatorAction({ action: "getState" });
  };

  window.dispatchCoCreatorEvent = function (payload) {
    if (!payload) {
      return;
    }
    if (payload.type === "creatorInput") {
      window.CO_CreatorRender.noteInput(payload.control, payload.handled);
      return;
    }
    if (payload.type === "creatorController") {
      window.CO_CreatorControls.handleController(payload);
      return;
    }
    if (payload.type === "creatorPresets") {
      window.CO_CreatorRender.renderPresets(payload);
      return;
    }
    if (payload.type === "creatorAxes") {
      window.CO_CreatorRender.renderAxes(payload);
      return;
    }
    if (payload.type === "creatorOptions") {
      window.CO_CreatorControls.renderOptions(payload);
      return;
    }
    if (payload.type === "creatorState") {
      generation = Number.isSafeInteger(payload.generation) && payload.generation >= 0
        ? payload.generation : null;
      window.CO_CreatorRender.render(payload);
    }
  };

  window.addEventListener("error", function (event) {
    console.error("[CO Character Creator] " + (event.message || "unknown error"));
  });

  window.CO_CreatorControls.build();
  sendCreatorAction({ action: "getState" });
})();
