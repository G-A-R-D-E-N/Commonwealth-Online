(function () {
  "use strict";

  var FALLBACK_NAME = "Unknown player";

  function isObject(value) {
    return !!value && typeof value === "object" && !Array.isArray(value);
  }

  function describePrompt(input) {
    var state = isObject(input) ? input : {};
    return {
      visible: state.visible === true,
      name: typeof state.name === "string" && state.name !== "" ? state.name : FALLBACK_NAME
    };
  }

  function paint(view) {
    var root = document.getElementById("coInteractPrompt");
    var label = document.getElementById("coInteractLabel");
    if (!root || !label) {
      return;
    }
    root.hidden = !view.visible;
    label.textContent = "Interact with " + view.name;
  }

  window.coInteractPromptSetState = function (input) {
    var state = null;
    if (typeof input === "string") {
      try {
        state = JSON.parse(input);
      } catch (error) {
        state = null;
      }
    } else {
      state = input;
    }
    paint(describePrompt(state));
  };
  window.coInteractPromptModel = {
    describePrompt: describePrompt
  };
})();
