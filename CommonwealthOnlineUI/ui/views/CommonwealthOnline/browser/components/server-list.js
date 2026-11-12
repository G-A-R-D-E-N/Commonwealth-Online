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

    reachabilityDescriptor: function (server) {
      if (!server || !(server.host || server.address)) return null;
      if (server.savedOnly) {
        return {
          modifier: "unchecked",
          label: "Offline (saved)",
          title: reachabilityReason(server, "Saved favorite that is not in the latest server directory refresh.")
        };
      }
      var reachability = String(server.reachability || "");
      if (reachability === "checking") {
        return {
          modifier: "checking",
          label: "CHECKING",
          title: "Checking whether this server answers"
        };
      }
      if (reachability === "answering") {
        var milliseconds = typeof server.reachabilityMs === "number" && server.reachabilityMs > 0
          ? " " + server.reachabilityMs + " ms"
          : "";
        return {
          modifier: "answering",
          label: "ANSWERING" + milliseconds,
          title: "This server completed a connection handshake"
        };
      }
      if (reachability === "no_answer") {
        return {
          modifier: "no-answer",
          label: "NO ANSWER",
          title: reachabilityReason(server, "The server did not answer a connection attempt.")
        };
      }
      return {
        modifier: "unchecked",
        label: "NOT CHECKED",
        title: reachabilityReason(server, "No check could run for this server.")
      };
    },

    reachabilityBadge: function (server) {
      var descriptor = CO_ServerList.reachabilityDescriptor(server);
      if (!descriptor) return "";
      return '<span class="co-server-state co-server-state--' + descriptor.modifier +
        '" title="' + escapeAttr(descriptor.title) + '">' + escapeHtml(descriptor.label) + "</span>";
    },

    capacityDescriptor: function (server) {
      var max = server.maxPlayers > 0 ? server.maxPlayers : 0;
      if (server.reachability === "no_answer") {
        return {
          modifier: "",
          label: "—/" + (max || "—"),
          title: "The server did not answer, so its player count is unknown"
        };
      }
      if (!max) return { modifier: "", label: "—", title: "" };
      var count = server.players || 0;
      var full = count >= max;
      return {
        modifier: full ? " co-capacity-full" : "",
        label: count + "/" + max,
        title: full ? "Server is full" : ""
      };
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
        var capacity = CO_ServerList.capacityDescriptor(server);
        var capacityTitle = capacity.title ? ' title="' + escapeAttr(capacity.title) + '"' : "";
        var ping = window.CO_ServerRecords.effectivePing(server);
        var hasPing = ping !== null;
        var pingLabel = hasPing ? (ping + " ms") : "\u2014";
        var pingCls = hasPing ? CO_ServerList.pingClass(ping) : "co-ping-unknown";
        var lockBadge = server.passworded ? '<span class="co-server-locked" title="Password required">&#x1F512;</span>' : "";
        var modeBadge = server.mode ? '<span class="co-server-mode-tag">' + escapeHtml(server.mode) + '</span>' : "";
        var stateBadge = !isRecent && !isLocal ? CO_ServerList.reachabilityBadge(server, state) : "";

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
            '<span class="co-server-capacity"><span class="co-capacity-val">' + escapeHtml((server.players || 0) + "/" + (server.maxPlayers || 100)) + "</span></span>" +
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
          stateBadge +
          "</div>" +
          '<div class="co-server-row-right">' +
          '<span class="co-server-capacity' + capacity.modifier + '"' + capacityTitle + '><span class="co-capacity-val">' + escapeHtml(capacity.label) + "</span></span>" +
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

    applyReachabilityUpdates: function (root, state, event) {
      var updates = event && Array.isArray(event.updates) ? event.updates : [];
      var keys = [];
      updates.forEach(function (update) {
        if (!applyReachabilityUpdate(state, update)) return;
        var key = addressKey(update.host, update.port);
        if (keys.indexOf(key) < 0) keys.push(key);
      });
      if (!keys.length) return false;
      paintReachability(root, state, keys);
      return true;
    },

    addressKey: function (entry) {
      return entryAddressKey(entry);
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

  function reachabilityReason(server, fallback) {
    var reason = server && server.reachabilityReason ? String(server.reachabilityReason) : "";
    return reason || fallback;
  }

  function trimAscii(value) {
    return value.replace(/^[ \t\r\n]+/, "").replace(/[ \t\r\n]+$/, "");
  }

  function normalizeHostKey(host) {
    var value = trimAscii(String(host || "")).toLowerCase();
    if (value.length > 2 && value.charAt(0) === "[" && value.charAt(value.length - 1) === "]") {
      value = trimAscii(value.slice(1, -1));
    }
    return value;
  }

  function addressKey(host, port) {
    var portNum = parseInt(port, 10);
    return normalizeHostKey(host) + ":" + (portNum > 0 ? portNum : 7777);
  }

  function entryAddressKey(entry) {
    return addressKey(entry && (entry.host || entry.address), entry && entry.port);
  }

  function reachabilityLists(state) {
    return [state.allServers, state.servers, state.favorites, state.recent, state.localServers];
  }

  function applyReachabilityUpdate(state, update) {
    if (!update) return false;
    var key = addressKey(update.host, update.port);
    var next = String(update.state || "unchecked");
    if (next !== "checking" && next !== "answering" && next !== "no_answer" && next !== "unchecked") {
      next = "unchecked";
    }
    var reason = update.reason ? String(update.reason) : "";
    var milliseconds = typeof update.milliseconds === "number" && update.milliseconds > 0 ? update.milliseconds : 0;
    var applied = false;
    reachabilityLists(state).forEach(function (list) {
      if (!Array.isArray(list)) return;
      list.forEach(function (entry) {
        if (!entry || entryAddressKey(entry) !== key) return;
        entry.reachability = next;
        entry.reachabilityReason = reason;
        entry.reachabilityMs = milliseconds;
        applied = true;
      });
    });
    return applied;
  }

  function activeListFor(state) {
    if (state.activeTab === "favorites") return state.favorites;
    if (state.activeTab === "recent") return state.recent;
    if (state.activeTab === "local") return state.localServers;
    return state.servers;
  }

  function paintReachability(root, state, keys) {
    var listServers = activeListFor(state) || [];
    var pane = root.querySelector('.co-tab-pane[data-pane="' + state.activeTab + '"]');
    if (!pane) return;
    var list = pane.querySelector(".co-server-list");
    if (!list) return;
    listServers.forEach(function (entry, index) {
      if (!entry || keys.indexOf(entryAddressKey(entry)) < 0) return;
      var row = list.querySelector('.co-server-row[data-index="' + index + '"]');
      var pingEl = row && row.querySelector(".co-ping");
      if (pingEl) {
        var ping = window.CO_ServerRecords.effectivePing(entry);
        pingEl.className = "co-ping " + (ping !== null ? CO_ServerList.pingClass(ping) : "co-ping-unknown");
        pingEl.textContent = ping !== null ? ping + " ms" : "—";
      }
      var badge = row && row.querySelector(".co-server-state");
      var descriptor = CO_ServerList.reachabilityDescriptor(entry);
      if (!badge || !descriptor) return;
      badge.className = "co-server-state co-server-state--" + descriptor.modifier;
      badge.setAttribute("title", descriptor.title);
      badge.textContent = descriptor.label;
    });
    var selected = listServers[state.selectedServerIndex];
    if (selected && keys.indexOf(entryAddressKey(selected)) >= 0 && window.CO_ServerDetails) {
      CO_ServerDetails.render(root, Object.assign({}, state, { servers: listServers }));
    }
  }

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
