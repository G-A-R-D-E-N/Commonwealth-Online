(function () {
  "use strict";

  var launcher = document.getElementById("launcher");
  var generation = 0;

  window.setLauncherGeneration = function (value) {
    generation = Number(value) || 0;
  };

  function updateScale() {
    var refWidth = 1920;
    var refHeight = 1080;
    var width = window.innerWidth || refWidth;
    var height = window.innerHeight || refHeight;
    var scale = Math.max(1, Math.min(width / refWidth, height / refHeight));
    document.documentElement.style.setProperty("--co-ui-scale", String(scale));
  }

  function send(name, payload) {
    if (typeof window[name] === "function") {
      window[name](payload);
    }
  }

  function updateRegion() {
    var rect = launcher.getBoundingClientRect();
    send("coLauncherHitRegion", JSON.stringify({
      generation: generation,
      active: rect.width > 0 && rect.height > 0,
      left: Math.round(rect.left),
      top: Math.round(rect.top),
      width: Math.round(rect.width),
      height: Math.round(rect.height)
    }));
  }

  window.updateLauncherRegion = updateRegion;

  updateScale();

  launcher.addEventListener("pointerenter", function () {
    send("coLauncherPointerFocus", JSON.stringify({ generation: generation, focus: true }));
  });

  launcher.addEventListener("pointerleave", function () {
    send("coLauncherPointerFocus", JSON.stringify({ generation: generation, focus: false }));
  });

  launcher.addEventListener("click", function () {
    send("coLauncherAction", JSON.stringify({ generation: generation, action: "restore" }));
  });

  window.addEventListener("resize", function () {
    updateScale();
    updateRegion();
  });
  updateRegion();
}());
