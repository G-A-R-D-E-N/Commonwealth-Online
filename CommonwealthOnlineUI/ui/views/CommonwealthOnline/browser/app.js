(function () {
  "use strict";

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
  if (window.CO_ENABLE_MOCKS && window.CO_MOCK_SERVERS && typeof window.dispatchCoEvent === "function") {
    window.dispatchCoEvent({ type: "serverListUpdated", servers: window.CO_MOCK_SERVERS });
  }
  console.log("[CO] Standalone server browser loaded");
})();
