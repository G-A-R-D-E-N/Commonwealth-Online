(function () {
  "use strict";

  if (typeof window.coAction !== "function") {
    window.CO_ENABLE_MOCKS = true;
  }

  var mount = document.getElementById("coBrowserMount");
  if (!mount) {
    console.error("[CO] coBrowserMount element is required");
    return;
  }
  if (!window.CO_Browser) {
    console.error("[CO] CO_Browser module is required before app.js");
    return;
  }

  if (typeof window.coAction === "function") {
    try {
      window.coAction(JSON.stringify({ action: "openView", view: "multiplayer" }));
    } catch (_) {}
  }

  var browser = window.CO_Browser.create({
    mount: mount,
    layout: "standalone",
    exposeGlobals: true,
    captureKeys: true,
    onBack: function () {
      if (typeof window.closeBrowser === "function") {
        window.closeBrowser();
      }
      if (typeof window.coAction === "function") {
        window.coAction(JSON.stringify({ action: "closeBrowser" }));
      }
      if (mount) {
        mount.style.display = "none";
      }
    }
  });

  browser.show();
  console.log("[CO] Standalone server browser loaded");
})();
