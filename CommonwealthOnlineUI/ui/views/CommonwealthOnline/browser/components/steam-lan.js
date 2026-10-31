(function () {
  "use strict";

  function escapeHtml(value) {
    return String(value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function escapeAttr(value) {
    return String(value).replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;");
  }

  function rows(state, focusedId) {
    var servers = state.localServers || [];
    if (state.isScanningLocal && !servers.length) {
      return '<p class="co-steam-desc">Scanning this network...</p>';
    }
    if (!servers.length) {
      return '<p class="co-steam-desc">No dedicated servers on this network yet.</p>';
    }
    return servers.map(function (server, index) {
      var id = "co-lan-" + index;
      var name = server.name || ((server.host || "server") + ":" + (server.port || 7777));
      var players = (server.players || 0) + "/" + (server.maxPlayers || 16);
      return (
        '<button type="button" class="co-lan-row' + (id === focusedId ? " focused" : "") + '" id="' + id +
        '" data-lan-id="' + escapeAttr(server.id || "") + '" data-steam-field="' + id + '">' +
        '<span class="co-lan-name">' + escapeHtml(name) + "</span>" +
        '<span class="co-lan-meta">' + escapeHtml(players) + "</span>" +
        "</button>"
      );
    }).join("");
  }

  function section(state, focusedId, scanButton) {
    var count = (state.localServers || []).length;
    return (
      '<section class="co-steam-card co-steam-lan">' +
      '<div class="co-steam-lan-head">' +
      '<h3 class="co-steam-title">On this network</h3>' +
      '<span class="co-count" id="coLanCount">' + (state.isScanningLocal ? "Scanning" : count + " found") + "</span>" +
      "</div>" +
      '<p class="co-steam-desc">Dedicated servers on this LAN. They are not Steam friends.</p>' +
      '<div class="co-lan-list">' + rows(state, focusedId) + "</div>" +
      '<div class="co-steam-actions">' + scanButton + "</div>" +
      "</section>"
    );
  }

  function extraFieldIds(state) {
    var ids = ["co-steam-scan"];
    var lan = state.localServers || [];
    for (var i = 0; i < lan.length; i++) ids.push("co-lan-" + i);
    return ids;
  }

  function updateList(container, state, focusedId) {
    var lan = container.querySelector(".co-lan-list");
    if (!lan) return;
    var active = document.activeElement;
    var target = window.CO_Keyboard && CO_Keyboard.getTarget ? CO_Keyboard.getTarget() : null;
    if (lan.contains(active) || (target && lan.contains(target))) return;
    lan.innerHTML = rows(state, focusedId);
  }

  window.CO_SteamLan = {
    rows: rows,
    section: section,
    extraFieldIds: extraFieldIds,
    updateList: updateList
  };
})();
