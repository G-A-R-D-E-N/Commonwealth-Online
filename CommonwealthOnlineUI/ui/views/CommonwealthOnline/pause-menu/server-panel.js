(function () {
  "use strict";

  var panel = null;
  var nameEl = null;
  var playersEl = null;
  var pingEl = null;
  var signalBarsEl = null;
  var isMapMode = function () {
    return false;
  };

  function init(options) {
    options = options || {};
    panel = options.panel || null;
    nameEl = options.name || null;
    playersEl = options.players || null;
    pingEl = options.ping || null;
    signalBarsEl = options.signalBars || null;
    isMapMode = typeof options.isMapMode === "function" ? options.isMapMode : isMapMode;
  }

  function pingTierClass(ping) {
    if (typeof ping !== "number" || ping <= 0) {
      return "signal-bars--unknown";
    }
    if (ping <= 50) {
      return "signal-bars--good";
    }
    if (ping <= 100) {
      return "signal-bars--mid";
    }
    return "signal-bars--bad";
  }

  function syncMapVisibility() {
    if (!panel) {
      return;
    }
    panel.style.display = isMapMode() ? "none" : "";
  }

  function clearSignalBars() {
    if (!signalBarsEl) {
      return;
    }
    signalBarsEl.classList.remove(
      "signal-bars--good",
      "signal-bars--mid",
      "signal-bars--bad",
      "signal-bars--unknown"
    );
  }

  function setInfo(info) {
    if (!panel) {
      return;
    }

    var connected = !!(info && info.connected);
    panel.classList.toggle("is-offline", !connected);

    if (!connected) {
      if (nameEl) {
        nameEl.textContent = "—";
      }
      if (playersEl) {
        playersEl.textContent = "—";
      }
      if (pingEl) {
        pingEl.textContent = "—";
      }
      clearSignalBars();
      if (signalBarsEl) {
        signalBarsEl.classList.add("signal-bars--unknown");
      }
      syncMapVisibility();
      return;
    }

    var name = (info && (info.name || info.host)) || "Connected";
    var players = typeof info.players === "number" ? info.players : 0;
    var maxPlayers = typeof info.maxPlayers === "number" ? info.maxPlayers : 0;
    var ping = typeof info.ping === "number" ? info.ping : 0;

    if (nameEl) {
      nameEl.textContent = name;
    }
    if (playersEl) {
      playersEl.textContent = maxPlayers > 0
        ? (players + " / " + maxPlayers)
        : String(Math.max(players, 1));
    }
    if (pingEl) {
      pingEl.textContent = ping > 0 ? (ping + " ms") : "—";
    }
    clearSignalBars();
    if (signalBarsEl) {
      signalBarsEl.classList.add(pingTierClass(ping));
    }

    syncMapVisibility();
  }

  window.CO_ServerPanel = {
    init: init,
    setInfo: setInfo,
    syncMapVisibility: syncMapVisibility
  };
})();
