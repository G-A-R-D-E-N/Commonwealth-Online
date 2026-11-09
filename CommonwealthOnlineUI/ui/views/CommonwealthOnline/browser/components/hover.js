(function () {
  "use strict";

  /*
   * PrismaUI / Ultralight updates :hover only for native form controls (input, select).
   * Custom buttons and rows need a .hovered class driven by cursor position.
   */
  var HOVER_SELECTOR = [
    ".co-nav-item",
    ".co-server-row",
    ".co-btn",
    ".co-checkbox-row"
  ].join(", ");

  var currentHovered = null;
  var lastX = null;
  var lastY = null;
  var rafId = null;
  var boundPanel = null;

  function findHoverTarget(node) {
    if (!node || !node.closest) {
      return null;
    }

    var target = node.closest(HOVER_SELECTOR);
    if (!target || !boundPanel || !boundPanel.contains(target)) {
      return null;
    }

    return target;
  }

  function setHovered(target) {
    if (target === currentHovered) {
      return;
    }

    if (currentHovered) {
      currentHovered.classList.remove("hovered");
    }

    currentHovered = target;

    if (currentHovered) {
      currentHovered.classList.add("hovered");
    }
  }

  function clearHovered() {
    setHovered(null);
    lastX = null;
    lastY = null;
  }

  function isInsidePanel(x, y) {
    if (!boundPanel) {
      return false;
    }

    var rect = boundPanel.getBoundingClientRect();
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
  }

  function updateFromPoint(x, y) {
    if (typeof x !== "number" || typeof y !== "number" || isNaN(x) || isNaN(y)) {
      return;
    }

    lastX = x;
    lastY = y;

    if (currentHovered && !document.body.contains(currentHovered)) {
      currentHovered = null;
    }

    if (!isInsidePanel(x, y)) {
      setHovered(null);
      return;
    }

    var node = document.elementFromPoint(x, y);
    setHovered(findHoverTarget(node));
  }

  function onPointerEvent(e) {
    updateFromPoint(e.clientX, e.clientY);
  }

  function tick() {
    if (lastX !== null && lastY !== null) {
      updateFromPoint(lastX, lastY);
    }
    rafId = window.requestAnimationFrame(tick);
  }

  window.CO_Hover = {
    bind: function (root) {
      boundPanel = root && root.querySelector(".co-panel");
      if (!boundPanel) {
        return;
      }

      document.addEventListener("mousemove", onPointerEvent, true);
      document.addEventListener("pointermove", onPointerEvent, true);

      boundPanel.addEventListener("mouseleave", clearHovered);

      if (!rafId) {
        rafId = window.requestAnimationFrame(tick);
      }
    },

    updateFromPoint: updateFromPoint,

    clear: clearHovered
  };
})();
