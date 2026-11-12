(function () {
  "use strict";

  var DEAD_ZONE = 0.2;
  var MAX_STEP_PX = 16;
  var selector = null;
  var stick = 0;
  var frame = 0;

  function stickValue(raw) {
    var magnitude = Math.abs(Number(raw) || 0);
    if (magnitude <= DEAD_ZONE) return 0;
    var scaled = (magnitude - DEAD_ZONE) / (1 - DEAD_ZONE);
    return raw < 0 ? -scaled : scaled;
  }

  function findTarget() {
    if (!selector) return document.scrollingElement;
    var nodes = document.querySelectorAll(selector);
    for (var i = 0; i < nodes.length; i++) {
      if (nodes[i].getClientRects().length > 0) return nodes[i];
    }
    return null;
  }

  function tick() {
    frame = 0;
    var target = findTarget();
    if (!target) {
      stick = 0;
      return;
    }
    if (stick) target.scrollTop -= stick * MAX_STEP_PX;
    if (stick) frame = window.requestAnimationFrame(tick);
  }

  window.handleControllerAxes = function (leftX, leftY, rightX, rightY) {
    stick = stickValue(rightY);
    if (stick && !frame) frame = window.requestAnimationFrame(tick);
  };

  window.CO_GamepadSticks = {
    start: function (scrollSelector) {
      selector = scrollSelector || null;
    }
  };
})();
