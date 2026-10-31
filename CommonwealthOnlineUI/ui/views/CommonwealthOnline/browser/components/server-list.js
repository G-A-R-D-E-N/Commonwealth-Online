(function () {
  "use strict";

  window.CO_ServerList = {
    pingClass: function (ping) {
      if (ping <= 50) return "co-ping-good";
      if (ping <= 100) return "co-ping-mid";
      return "co-ping-bad";
    },

    isLocalHost: function (host) {
      if (!host) return false;
      return /^(127\.0\.0\.1|localhost|::1)$/i.test(String(host).trim());
    },

    displayName: function (server) {
      if (!server) return "";
      var host = server.host || server.address || "";
      var port = server.port > 0 ? server.port : 7777;
      var name = String(server.name || "").trim();
      var isDirect = server.id === "direct" || name === "Direct Connect";
      if (!name || isDirect) {
        var shownHost = host || "127.0.0.1";
        return (String(shownHost).indexOf(":") >= 0 ? "[" + shownHost + "]" : shownHost) + ":" + port;
      }
      return name;
    },

    render: function (root, state) {
      var wrap = root.querySelector(".co-server-list-wrap");
      var list = root.querySelector(".co-server-list");
      if (!wrap || !list) return;

      wrap.className = "co-server-list-wrap" + (state.focusZone === "server-list" ? " focused" : "");
      var isRecent = state.activeTab === "recent";
      var isLocal = state.activeTab === "local";

      if (!state.servers.length) {
        var emptyText = "No servers found";
        if (isRecent) emptyText = "No recent transmissions logged. Join a world to establish connection history.";
        else if (isLocal) {
          emptyText = state.isScanningLocal ?
            "Scanning local sub-space frequencies..." :
            "No local broadcasts detected. Verify native server is active and UDP discovery traffic is permitted through firewall.";
        } else if (state.isRefreshing) {
          emptyText = "Tuning Appalachia broadcast directory...";
        } else if (state.directoryNotice && state.activeTab === "server-browser") {
          emptyText = state.directoryNotice;
        }
        var emptyEl = document.createElement("div");
        emptyEl.className = "co-empty";
        emptyEl.textContent = emptyText;
        list.textContent = "";
        list.appendChild(emptyEl);
        CO_ServerList.updateScrollIndicator(wrap, list);
        return;
      }

      list.innerHTML = state.servers.map(function (server, index) {
        var selected = index === state.selectedServerIndex ? " selected" : "";
        var label = CO_ServerList.displayName(server);
        var playersLabel = (server.maxPlayers > 0)
          ? ((server.players || 0) + "/" + server.maxPlayers)
          : "\u2014";
        var hasPing = typeof server.ping === "number" && server.ping > 0;
        var pingLabel = hasPing ? (server.ping + " ms") : "\u2014";
        var pingCls = hasPing ? CO_ServerList.pingClass(server.ping) : "co-ping-unknown";
        var lockBadge = server.passworded ? '<span class="co-server-locked" title="Password required">&#x1F512;</span>' : "";
        var modeBadge = server.mode ? '<span class="co-server-mode-tag">' + escapeHtml(server.mode) + '</span>' : "";

        if (isRecent) {
          var recentMeta = [];
          if (server.host) {
            recentMeta.push((String(server.host).indexOf(":") >= 0 ? "[" + server.host + "]" : server.host) + ":" + (server.port > 0 ? server.port : 7777));
          }
          if (server.region) {
            recentMeta.push(server.region);
          }
          return (
            '<div class="co-server-row' + selected + '" data-index="' + index + '" data-id="' + escapeAttr(server.id || "") + '">' +
            '<div class="co-server-row-left co-server-row-stack">' +
            '<div class="co-server-row-top">' +
            '<span class="co-server-cursor" aria-hidden="true">&#x25B6;</span>' +
            '<span class="co-server-row-name">' + escapeHtml(label) + "</span>" +
            lockBadge +
            modeBadge +
            "</div>" +
            (recentMeta.length
              ? '<span class="co-server-row-meta">' + escapeHtml(recentMeta.join(" \u00b7 ")) + "</span>"
              : "") +
            "</div>" +
            "</div>"
          );
        }

        if (isLocal) {
          return (
            '<div class="co-server-row' + selected + '" data-index="' + index + '" data-id="' + escapeAttr(server.id || "") + '">' +
            '<div class="co-server-row-left">' +
            '<span class="co-server-cursor" aria-hidden="true">&#x25B6;</span>' +
            '<span class="co-server-row-name">' + escapeHtml(label) + "</span>" +
            lockBadge +
            modeBadge +
            "</div>" +
            '<div class="co-server-row-right">' +
            '<span class="co-server-capacity"><span class="co-capacity-val">' + escapeHtml((server.players || 0) + "/" + (server.maxPlayers || 16)) + "</span></span>" +
            '<span class="co-ping ' + pingCls + '">' + escapeHtml(pingLabel) + "</span>" +
            "</div>" +
            "</div>"
          );
        }

        var fav = server.favorite ? " on" : "";
        return (
          '<div class="co-server-row' + selected + '" data-index="' + index + '" data-id="' + escapeAttr(server.id) + '">' +
          '<div class="co-server-row-left">' +
          '<span class="co-server-cursor" aria-hidden="true">&#x25B6;</span>' +
          '<span class="co-fav' + fav + '">' + (server.favorite ? "\u2605" : "\u2606") + "</span>" +
          lockBadge +
          '<span class="co-server-row-name">' + escapeHtml(label) + "</span>" +
          modeBadge +
          "</div>" +
          '<div class="co-server-row-right">' +
          '<span class="co-server-capacity"><span class="co-capacity-val">' + escapeHtml(playersLabel) + "</span></span>" +
          '<span class="co-ping ' + pingCls + '">' + escapeHtml(pingLabel) + "</span>" +
          "</div>" +
          "</div>"
        );
      }).join("");

      CO_ServerList.updateScrollIndicator(wrap, list);
    },

    updateScrollIndicator: function (wrap, list) {
      var indicator = wrap && wrap.querySelector(".co-scroll-indicator");
      if (!indicator || !list) return;
      var hasOverflow = list.scrollHeight > list.clientHeight + 2;
      indicator.classList.toggle("hidden", !hasOverflow);
    },

    bind: function (root, onSelect) {
      var list = root.querySelector(".co-server-list");
      if (!list) return;

      list.addEventListener("click", function (e) {
        var row = e.target.closest(".co-server-row");
        if (!row) return;
        onSelect(parseInt(row.getAttribute("data-index"), 10));
      });

      list.addEventListener("dblclick", function (e) {
        var row = e.target.closest(".co-server-row");
        if (!row) return;
        onSelect(parseInt(row.getAttribute("data-index"), 10));
        if (window.CO_App && window.CO_App.joinSelected) {
          window.CO_App.joinSelected();
        }
      });
    },

    scrollToSelected: function (root, index) {
      var list = root.querySelector(".co-server-list");
      var row = list && list.querySelector('.co-server-row[data-index="' + index + '"]');
      if (row) {
        row.scrollIntoView({ block: "nearest" });
      }
      var wrap = root.querySelector(".co-server-list-wrap");
      if (wrap && list) {
        CO_ServerList.updateScrollIndicator(wrap, list);
      }
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
