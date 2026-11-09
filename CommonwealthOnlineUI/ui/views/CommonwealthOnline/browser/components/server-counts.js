(function () {
  "use strict";

  function updateServer(root, state) {
    var countEl = root.querySelector("#coServerCount");
    var titleEl = root.querySelector("#coSubpanelTitle");
    var count = state.servers.length;
    if (countEl) {
      countEl.textContent = count + " Found";
    }
    if (titleEl && state.activeTab === "server-browser") {
      titleEl.textContent = count === 1 ? "1 Server" : count + " Servers";
    }
  }

  function updateLocal(root, state) {
    var countEl = root.querySelector("#coLocalCount");
    var titleEl = root.querySelector("#coLocalTitle");
    if (!countEl && !titleEl) {
      return;
    }

    var count = state.localServers.length;
    if (countEl) {
      if (state.isScanningLocal) {
        countEl.textContent = "Scanning";
      } else {
        countEl.textContent = count + " Found";
      }
    }
    if (titleEl && state.activeTab === "local") {
      if (state.isScanningLocal) {
        titleEl.textContent = "Scanning...";
      } else {
        titleEl.textContent = count === 1 ? "1 Local Server" : count + " Local Servers";
      }
    }
  }

  window.CO_ServerCounts = {
    updateServer: updateServer,
    updateLocal: updateLocal
  };
})();
