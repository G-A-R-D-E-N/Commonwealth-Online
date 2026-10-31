(function () {
  "use strict";

  var CONTROLLER_ACTION_BUTTONS = {
    "co.select": "a",
    "co.back": "b",
    "co.refresh": "x",
    "co.favorite": "y",
    "co.prevTab": "lb",
    "co.nextTab": "rb",
    "co.navUp": "dpad_up",
    "co.navDown": "dpad_down",
    "co.navLeft": "dpad_left",
    "co.navRight": "dpad_right"
  };

  var KEY_BUTTONS = {
    ArrowUp: "dpad_up",
    ArrowDown: "dpad_down",
    ArrowLeft: "dpad_left",
    ArrowRight: "dpad_right",
    Enter: "a",
    Escape: "b",
    " ": "a",
    r: "x",
    f: "y",
    q: "lb",
    e: "rb"
  };

  var DIRECTIONS = ["dpad_up", "dpad_down", "dpad_left", "dpad_right"];

  function isDirectionalButton(button) {
    return DIRECTIONS.indexOf(button) >= 0;
  }

  function mapKeyToButton(key) {
    var normalized = key && key.length === 1 ? key.toLowerCase() : key;
    return KEY_BUTTONS[normalized] || null;
  }

  function isTextEntryTarget(el) {
    var target = el && el.tagName ? el : document.activeElement;
    if (window.CO_TextFocus && CO_TextFocus.isTextEntryTarget) {
      return CO_TextFocus.isTextEntryTarget(target);
    }
    return false;
  }

  function isKeyboardCaptureTarget(el) {
    var target = el && el.tagName ? el : document.activeElement;
    if (window.CO_TextFocus && CO_TextFocus.isFormEntryTarget) {
      return CO_TextFocus.isFormEntryTarget(target);
    }
    return isTextEntryTarget(target);
  }

    function onPrismaControllerAction(event, visible, state, setPointerMode, handleControllerButton) {
      var detail = event && event.detail;
      if (!visible || !detail || (document.hasFocus && !document.hasFocus())) return;
      var button = CO_ControllerInput.controllerActionButton(detail.action);
      if (!button || (detail.state !== "pressed" &&
          !(detail.state === "repeat" && CO_ControllerInput.shouldHandleRepeat(button)))) return;
      CO_ControllerTrace.trace("prisma-controller-action", detail, button, state, true);
      setPointerMode("gamepad");
      handleControllerButton(button, "down");
    }

  window.CO_ControllerInput = {
    onPrismaControllerAction: onPrismaControllerAction,
    mapKeyToButton: mapKeyToButton,
    isTextEntryTarget: isTextEntryTarget,
    isKeyboardCaptureTarget: isKeyboardCaptureTarget,
    isDirectionalButton: isDirectionalButton,
    controllerActionButton: function (action) {
      return CONTROLLER_ACTION_BUTTONS[action] || null;
    },
    shouldHandleRepeat: isDirectionalButton
  };
})();
