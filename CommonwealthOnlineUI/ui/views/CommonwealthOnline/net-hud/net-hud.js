(function () {
  "use strict";

  var ROW_IDS = {
    ping: "coNetPing",
    quality: "coNetQuality",
    inbound: "coNetIn",
    outbound: "coNetOut"
  };

  function isObject(value) {
    return !!value && typeof value === "object" && !Array.isArray(value);
  }

  function isNumber(value) {
    return typeof value === "number" && isFinite(value);
  }

  function parseState(json) {
    var state;
    try {
      state = JSON.parse(json);
    } catch (error) {
      return null;
    }
    if (!isObject(state) || typeof state.showPing !== "boolean" || typeof state.showStats !== "boolean") {
      return null;
    }
    var stats = state.stats;
    if (!isObject(stats) || !isNumber(stats.pingMs) || !isNumber(stats.qualityLocal) ||
        !isNumber(stats.qualityRemote) || !isNumber(stats.inKBps) || !isNumber(stats.outKBps)) {
      return null;
    }
    return { showPing: state.showPing, showStats: state.showStats, stats: stats };
  }

  function pingText(pingMs) {
    return "Ping: " + (pingMs < 0 ? "-" : Math.round(pingMs) + " ms");
  }

  function qualityText(stats) {
    return "Quality: " + stats.qualityLocal + "% / " + stats.qualityRemote + "%";
  }

  function rateText(label, kbPerSecond) {
    return label + ": " + kbPerSecond.toFixed(1) + " KB/s";
  }

  function setRow(id, visible, text) {
    var row = document.getElementById(id);
    if (!row) {
      return;
    }
    row.hidden = !visible;
    row.textContent = text;
  }

  function render(state) {
    var root = document.getElementById("coNetHud");
    if (!root) {
      return;
    }
    var visible = !!state && (state.showPing || state.showStats);
    root.hidden = !visible;
    if (!visible) {
      return;
    }
    var stats = state.stats;
    setRow(ROW_IDS.ping, state.showPing, state.showPing ? pingText(stats.pingMs) : "");
    setRow(ROW_IDS.quality, state.showStats, state.showStats ? qualityText(stats) : "");
    setRow(ROW_IDS.inbound, state.showStats, state.showStats ? rateText("In", stats.inKBps) : "");
    setRow(ROW_IDS.outbound, state.showStats, state.showStats ? rateText("Out", stats.outKBps) : "");
  }

  window.coNetHudSetState = function (json) {
    render(parseState(json));
  };
  window.coNetHudModel = {
    parseState: parseState,
    pingText: pingText,
    qualityText: qualityText,
    rateText: rateText,
    render: render
  };
})();
