(function () {
  "use strict";

  var RECENT_STORAGE_KEY = "co_recent_servers";
  var MAX_RECENT = 8;

  function recentEntryKey(server) {
    if (!server) {
      return "";
    }
    var host = server.host || server.address || "";
    if (host) {
      return host + ":" + CO_ServerRecords.portNumFrom(server.port);
    }
    return server.id || "";
  }

  function snapshotRecentEntry(server) {
    return {
      id: server.id || recentEntryKey(server),
      name: server.name || "",
      host: server.host || server.address || "",
      port: CO_ServerRecords.portNumFrom(server.port),
      region: server.region || "",
      description: server.description || "",
      players: server.players || 0,
      maxPlayers: server.maxPlayers || 0,
      ping: server.ping || 0,
      worldspace: server.worldspace || "",
      mode: server.mode || "",
      favorite: !!server.favorite
    };
  }

  function load(state) {
    try {
      var raw = localStorage.getItem(RECENT_STORAGE_KEY);
      if (!raw) {
        return;
      }
      var parsed = JSON.parse(raw);
      if (Array.isArray(parsed) && parsed.length && !state.recent.length) {
        state.recent = parsed.slice(0, MAX_RECENT);
      }
    } catch (err) {
      console.warn("[CO] Failed to load recent servers cache:", err);
    }
  }

  function save(state) {
    try {
      localStorage.setItem(RECENT_STORAGE_KEY, JSON.stringify(state.recent));
    } catch (err) {
    }
  }

  function apply(state, list) {
    if (!Array.isArray(list)) return;
    state.recent = list.slice(0, MAX_RECENT).map(function (server) {
      return snapshotRecentEntry(server);
    }).filter(function (entry) {
      return !!(entry.host || entry.id);
    });
    save(state);
    if (state.activeTab === "recent" && state.selectedServerIndex >= state.recent.length) {
      state.selectedServerIndex = Math.max(0, state.recent.length - 1);
    }
  }

  function add(state, serverOrId, findServerById) {
    var server = null;
    if (serverOrId && typeof serverOrId === "object") {
      server = serverOrId;
    } else if (serverOrId) {
      server = findServerById(serverOrId);
    }
    if (!server) {
      return;
    }

    var entry = snapshotRecentEntry(server);
    var key = recentEntryKey(entry);
    state.recent = state.recent.filter(function (s) {
      return recentEntryKey(s) !== key && s.id !== entry.id;
    });
    state.recent.unshift(entry);
    if (state.recent.length > MAX_RECENT) {
      state.recent.length = MAX_RECENT;
    }
    save(state);
  }

  window.CO_RecentServers = {
    load: load,
    apply: apply,
    add: add
  };
})();
