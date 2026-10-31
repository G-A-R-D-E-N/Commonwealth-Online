(function () {
  "use strict";

  window.CO_ServerDetails = {
    render: function (root, state) {
      var activePane = root.querySelector('.co-tab-pane[data-pane="' + state.activeTab + '"]');
      var el = activePane && activePane.querySelector(".co-preview-area");
      if (!el) return;

      var server = state.servers[state.selectedServerIndex];
      var hasPreview = !!(server && server.previewImage);
      var hasName = !!(server && String(server.name || "").trim());
      var hasDescription = !!(server && String(server.description || "").trim());
      var hasHost = !!(server && String(server.host || server.address || "").trim());
      if (!server || (!hasPreview && !hasName && !hasDescription && !hasHost)) {
        el.innerHTML = "";
        el.classList.add("co-preview-hidden");
        return;
      }

      el.classList.remove("co-preview-hidden");

      var host = server.host || server.address || "";
      var port = server.port > 0 ? server.port : 7777;
      var playersLabel = (server.maxPlayers > 0)
        ? ((server.players || 0) + "/" + server.maxPlayers)
        : "";
      var pingLabel = (typeof server.ping === "number" && server.ping > 0)
        ? (server.ping + " ms")
        : "";

      var stats = [
        server.region ? ("[" + server.region + "]") : "",
        host ? ("FREQ: " + (String(host).indexOf(":") >= 0 ? "[" + host + "]" : host) + ":" + port) : "",
        server.worldspace ? ("CELL: " + server.worldspace) : "",
        server.mode ? ("MODE: " + server.mode) : "",
        playersLabel ? ("CAPACITY: " + playersLabel) : "",
        pingLabel ? ("LATENCY: " + pingLabel) : ""
      ].filter(Boolean).join("  //  ");

      var tags = Array.isArray(server.tags)
        ? server.tags.filter(Boolean).join(", ")
        : "";
      var extraBits = [];
      if (tags) extraBits.push("TAGS: " + tags);
      if (server.discord) extraBits.push("DISCORD: " + server.discord);
      if (server.modlist) extraBits.push("MODLIST: " + server.modlist);
      if (server.passworded) extraBits.push("[ \uD83D\uDD12 PASSWORD REQUIRED ]");
      var extraHtml = extraBits.length
        ? '<div class="co-preview-extra">' + escapeHtml(extraBits.join(" \u00b7 ")) + "</div>"
        : "";

      var descriptionHtml = hasDescription
        ? '<div class="co-preview-description">' + escapeHtml(server.description) + "</div>"
        : "";

      var previewHtml = hasPreview
        ? '<div class="co-preview"><img src="' + escapeAttr(server.previewImage) + '" alt="Preview"></div>'
        : "";

      var displayName = window.CO_ServerList
        ? CO_ServerList.displayName(server)
        : (server.name || server.host || "Server");

      el.innerHTML =
        previewHtml +
        '<div class="co-preview-caption">' +
        '<div class="co-dossier-header">' +
        '<span class="co-dossier-pill">[ FREQUENCY TRANSMISSION ]</span>' +
        '<span class="co-dossier-status"><span class="co-status-pip">●</span> ONLINE</span>' +
        "</div>" +
        '<div class="co-preview-name">' + escapeHtml(displayName) + "</div>" +
        descriptionHtml +
        (stats ? '<div class="co-preview-stats">' + escapeHtml(stats) + "</div>" : "") +
        extraHtml +
        "</div>";
    }
  };

  function escapeHtml(value) {
    return String(value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;");
  }

  function escapeAttr(value) {
    return String(value).replace(/"/g, "&quot;");
  }
})();
