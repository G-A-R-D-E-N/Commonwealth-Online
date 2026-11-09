(function () {
  "use strict";

  var info = null;
  var mode = "";
  var root = null;

  function esc(text) {
    return String(text == null ? "" : text)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function count(value) {
    return typeof value === "number" && value > 0 ? value : 0;
  }

  function rowsFor(current, modeLabel) {
    if (!current || current.connected !== true) {
      return [["Connection", "Not connected"]];
    }
    var host = typeof current.host === "string" ? current.host : "";
    var port = count(current.port);
    var players = count(current.players);
    var max = count(current.maxPlayers);
    var ping = count(current.ping);
    return [
      ["Connection", "Connected"],
      ["Server", current.name || "Connected"],
      ["Address", host ? (port ? host + ":" + port : host) : "—"],
      ["Mode", modeLabel || "—"],
      ["Players", max ? players + " / " + max : String(Math.max(players, 1))],
      ["Your ping", ping ? ping + " ms" : "—"]
    ];
  }

  function renderInfo(current, modeLabel) {
    var isConnected = current && current.connected === true;
    var rows = rowsFor(current, modeLabel);
    if (!isConnected) {
      return '<section class="info-panel info-panel--server is-offline">' +
        '<header class="info-head">' +
          '<div class="info-head-title-group">' +
            '<span class="info-sys-tag">CO-NET · TELEMETRY</span>' +
            '<h1 class="info-title">SERVER</h1>' +
          '</div>' +
          '<div class="info-head-actions">' +
            '<span class="info-kbd-badge"><kbd class="info-kbd">ESC</kbd> / <kbd class="info-kbd">B</kbd></span>' +
            '<button type="button" class="info-btn" data-action="close" aria-label="Close server info">Close</button>' +
          '</div>' +
        '</header>' +
        '<div class="info-cards info-cards--offline">' +
          '<div class="info-card info-card--offline">' +
            '<div class="info-card-header">' +
              '<span class="info-card-badge info-card-badge--danger">DISCONNECTED</span>' +
              '<span class="info-card-sub">NO ACTIVE LINK</span>' +
            '</div>' +
            '<div class="info-card-body">' +
              '<div class="info-row" data-field="connection"><span class="info-label">' + esc(rows[0][0]) + '</span><span>' + esc(rows[0][1]) + '</span></div>' +
              '<p class="info-hint">No active connection to a Commonwealth wasteland server. Open the Server Browser or accept a Steam squad invite to link.</p>' +
            '</div>' +
          '</div>' +
        '</div>' +
        '<footer class="info-footer">' +
          '<span class="info-footer-status">CARRIER LOST · STANDBY</span>' +
          '<span class="info-footer-version">VAULT-TEC NET-LINK V2.4</span>' +
        '</footer>' +
      '</section>';
    }

    var pingVal = count(current.ping);
    var playersVal = count(current.players);
    var maxVal = count(current.maxPlayers);
    var pct = maxVal > 0 ? Math.min(100, Math.round((playersVal / maxVal) * 100)) : (playersVal > 0 ? 100 : 0);
    var pingBars = pingVal <= 0 ? 0 : (pingVal < 60 ? 4 : (pingVal < 100 ? 3 : (pingVal < 180 ? 2 : 1)));
    var pingQuality = pingVal <= 0 ? "UNKNOWN" : (pingVal < 60 ? "OPTIMAL" : (pingVal < 100 ? "GOOD" : "FAIR"));

    return '<section class="info-panel info-panel--server">' +
      '<header class="info-head">' +
        '<div class="info-head-title-group">' +
          '<span class="info-sys-tag">CO-NET · TELEMETRY</span>' +
          '<h1 class="info-title">SERVER</h1>' +
        '</div>' +
        '<div class="info-head-actions">' +
          '<span class="info-kbd-badge"><kbd class="info-kbd">ESC</kbd> / <kbd class="info-kbd">B</kbd></span>' +
          '<button type="button" class="info-btn" data-action="close" aria-label="Close server info">Close</button>' +
        '</div>' +
      '</header>' +
      '<div class="info-cards">' +
        '<div class="info-card info-card--identity">' +
          '<div class="info-card-header">' +
            '<span class="info-card-tag">01 :: HOST IDENT</span>' +
            '<span class="info-status-pill"><span class="info-status-dot"></span>CONNECTED</span>' +
          '</div>' +
          '<div class="info-card-body">' +
            '<div class="info-row" data-field="connection"><span class="info-label">' + esc(rows[0][0]) + '</span><span>' + esc(rows[0][1]) + '</span></div>' +
            '<div class="info-row info-row--highlight" data-field="server"><span class="info-label">' + esc(rows[1][0]) + '</span><span>' + esc(rows[1][1]) + '</span></div>' +
            '<div class="info-row" data-field="address"><span class="info-label">' + esc(rows[2][0]) + '</span><span>' + esc(rows[2][1]) + '</span></div>' +
            '<div class="info-row" data-field="mode"><span class="info-label">' + esc(rows[3][0]) + '</span><span>' + esc(rows[3][1]) + '</span></div>' +
          '</div>' +
        '</div>' +
        '<div class="info-card info-card--telemetry">' +
          '<div class="info-card-header">' +
            '<span class="info-card-tag">02 :: TELEMETRY & ROSTER</span>' +
            '<span class="info-ping-quality">' + pingQuality + '</span>' +
          '</div>' +
          '<div class="info-card-body">' +
            '<div class="info-row" data-field="players"><span class="info-label">' + esc(rows[4][0]) + '</span><span>' + esc(rows[4][1]) + '</span></div>' +
            '<div class="info-capacity-track"><div class="info-capacity-bar" style="width:' + pct + '%"></div></div>' +
            '<div class="info-row" data-field="your-ping"><span class="info-label">' + esc(rows[5][0]) + '</span><span>' + esc(rows[5][1]) + '</span></div>' +
            '<div class="info-signal-bars" data-bars="' + pingBars + '">' +
              '<span class="info-signal-bar info-signal-bar--1' + (pingBars >= 1 ? ' is-active' : '') + '"></span>' +
              '<span class="info-signal-bar info-signal-bar--2' + (pingBars >= 2 ? ' is-active' : '') + '"></span>' +
              '<span class="info-signal-bar info-signal-bar--3' + (pingBars >= 3 ? ' is-active' : '') + '"></span>' +
              '<span class="info-signal-bar info-signal-bar--4' + (pingBars >= 4 ? ' is-active' : '') + '"></span>' +
            '</div>' +
          '</div>' +
        '</div>' +
      '</div>' +
      '<footer class="info-footer">' +
        '<span class="info-footer-status">HOST LINK ACTIVE · CIPHER SECURED</span>' +
        '<span class="info-footer-version">VAULT-TEC NET-LINK V2.4</span>' +
      '</footer>' +
    '</section>';
  }

  function paint() {
    if (root) {
      root.innerHTML = renderInfo(info, mode);
      var close = root.querySelector('[data-action="close"]');
      if (close && typeof close.focus === "function") {
        close.focus();
      }
    }
  }

  function close() {
    if (typeof window.coInfoAction === "function") {
      window.coInfoAction(JSON.stringify({ action: "close" }));
    }
  }

  function onKeydown(event) {
    if (event.key === "Escape") {
      event.preventDefault();
      close();
    }
  }

  function handleControllerInput(button, phase) {
    if (phase === "down" && (button === "b" || button === "a")) {
      close();
    }
  }

  function init() {
    root = document.getElementById("coInfo");
    if (!root) {
      return;
    }
    root.addEventListener("click", function (event) {
      var target = event.target && event.target.closest ? event.target.closest("[data-action]") : null;
      if (target && target.getAttribute("data-action") === "close") {
        close();
      }
    });
    document.addEventListener("keydown", onKeydown);
    document.documentElement.style.setProperty("--co-ui-scale", String(Math.max(0.75, Math.min(1.35, window.innerWidth / 1920))));
    paint();
  }

  window.coServerSetInfo = function (payload) {
    info = payload && typeof payload === "object" ? payload : null;
    paint();
  };
  window.coServerSetMode = function (payload) {
    mode = payload && typeof payload.label === "string" ? payload.label : "";
    paint();
  };
  window.handleControllerInput = handleControllerInput;
  window.coServerModel = { renderInfo: renderInfo, rowsFor: rowsFor };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
