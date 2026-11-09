window.CO_MOCK_SERVERS = [
  {
    id: "nz-1",
    name: "Commonwealth NZ #1",
    region: "Oceania",
    players: 6,
    maxPlayers: 12,
    ping: 32,
    worldspace: "Commonwealth",
    mode: "Co-op",
    status: "Open",
    modded: true,
    passworded: false,
    favorite: true,
    description: "Relaxed co-op server for explorers and builders. No PvP. Help each other rebuild the Commonwealth.",
    rules: ["No griefing or stealing", "Respect build sites", "Mic optional"]
  },
  {
    id: "vault111",
    name: "Vault 111 Co-op",
    region: "North America",
    players: 3,
    maxPlayers: 8,
    ping: 48,
    worldspace: "Commonwealth",
    mode: "Co-op",
    status: "Open",
    modded: false,
    passworded: false,
    favorite: false,
    description: "Story-friendly co-op. Stay in sync with main quest progression.",
    rules: ["No skipping major quest beats without group consent"]
  },
  {
    id: "dc-rp",
    name: "DC Wasteland RP",
    region: "North America",
    players: 10,
    maxPlayers: 10,
    ping: 72,
    worldspace: "Commonwealth",
    mode: "Roleplay",
    status: "Full",
    modded: true,
    passworded: true,
    favorite: false,
    description: "Light roleplay server set around Diamond City trade routes.",
    rules: ["Stay in character in voice", "No combat logging"]
  },
  {
    id: "far-harbor",
    name: "Far Harbor Expedition",
    region: "Europe",
    players: 4,
    maxPlayers: 6,
    ping: 118,
    worldspace: "Far Harbor",
    mode: "Co-op",
    status: "Open",
    modded: true,
    passworded: false,
    favorite: true,
    description: "DLC-focused co-op run through Far Harbor content.",
    rules: ["DLC content required", "Spoiler tags in chat for newcomers"]
  },
  {
    id: "local-relay",
    name: "Local Relay (Dev)",
    region: "Local",
    players: 0,
    maxPlayers: 16,
    ping: 1,
    worldspace: "Commonwealth",
    mode: "Co-op",
    status: "Open",
    modded: false,
    passworded: false,
    favorite: false,
    host: "127.0.0.1",
    port: 7777,
    description: "Native server on this PC. Start CommonwealthOnline.Server.exe before joining.",
    rules: ["Development server only"]
  },
  {
    id: "survival-hard",
    name: "Survival Hardcore",
    region: "Europe",
    players: 2,
    maxPlayers: 4,
    ping: 95,
    worldspace: "Commonwealth",
    mode: "Survival",
    status: "Open",
    modded: true,
    passworded: false,
    favorite: false,
    description: "Hardcore survival with limited respawns and shared resources.",
    rules: ["Permadeath on disconnect during combat", "Shared stash rules apply"]
  }
];

window.CO_MockData = {
  refreshDirectory: function (state, applyDirectoryServers, render) {
    state.isRefreshing = true;
    render();

    var directoryUrls = window.CO_SERVER_DIRECTORY_URL
      ? [window.CO_SERVER_DIRECTORY_URL]
      : [
          "https://oeajjfquvmdxqqtiotfa.supabase.co/functions/v1/server-directory",
          "https://commonwealth-online.com/servers/server.json"
        ];

    if (typeof fetch !== "function") {
      applyDirectoryServers(window.CO_MOCK_SERVERS || []);
      state.isRefreshing = false;
      state.directoryNotice = "";
      render();
      return;
    }

    CO_DirectoryRefresh.query(directoryUrls, function (url) {
      return fetch(url, { cache: "no-store" }).then(function (response) {
        if (!response.ok) {
          throw new Error("HTTP " + response.status);
        }
        return response.json();
      });
    }).then(function (result) {
      state.isRefreshing = false;
      if (result.success) {
        applyDirectoryServers(result.servers);
      }
      state.connectionStatus = result.connectionStatus;
      state.directoryNotice = result.directoryNotice;
      render();
    });
  },

  scanLocal: function (state, render) {
    state.isScanningLocal = true;
    render();
    setTimeout(function () {
      var portNum = window.CO_ServerRecords && window.CO_ServerRecords.portNumFrom
        ? window.CO_ServerRecords.portNumFrom
        : function (p) { var n = parseInt(p, 10); return isNaN(n) ? 7777 : n; };

      state.localServers = (window.CO_MOCK_SERVERS || []).filter(function (s) {
        return s.region === "Local" ||
          (s.host && window.CO_ServerList && CO_ServerList.isLocalHost(s.host));
      }).map(function (s) {
        return {
          id: s.id || ("local:" + (s.host || "127.0.0.1") + ":" + portNum(s.port)),
          name: s.name || "",
          host: s.host || "127.0.0.1",
          port: portNum(s.port),
          region: "Local",
          players: s.players || 0,
          maxPlayers: s.maxPlayers || 16,
          ping: s.ping || 1,
          worldspace: s.worldspace || "",
          mode: s.mode || ""
        };
      });
      state.isScanningLocal = false;
      if (state.activeTab === "local" && state.selectedServerIndex >= state.localServers.length) {
        state.selectedServerIndex = Math.max(0, state.localServers.length - 1);
      }
      render();
    }, 800);
  }
};
