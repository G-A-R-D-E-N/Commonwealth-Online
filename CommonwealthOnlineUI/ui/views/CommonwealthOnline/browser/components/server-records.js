(function () {
  "use strict";

  function portNumFrom(port) {
    var portNum = parseInt(port, 10);
    return isNaN(portNum) ? 7777 : portNum;
  }

  function normalizeDirectoryServer(raw) {
    if (!raw || typeof raw !== "object") return null;
    var host = String(raw.host || "").trim();
    if (!host) return null;
    var port = portNumFrom(raw.port);
    var modlist = String(raw.modlist || "");
    var passworded = typeof raw.passworded === "boolean"
      ? raw.passworded
      : !!raw.password;
    var modded = typeof raw.modded === "boolean" ? raw.modded : !!modlist;
    return {
      id: raw.id || (host + ":" + port),
      name: raw.name || host,
      host: host,
      port: port,
      region: raw.region || "",
      description: raw.description || "",
      worldspace: raw.worldspace || "",
      mode: raw.mode || "",
      status: raw.status || "Open",
      players: raw.players || 0,
      maxPlayers: raw.maxPlayers || 0,
      ping: raw.ping || 0,
      modded: modded,
      passworded: passworded,
      favorite: !!raw.favorite,
      live: raw.live === true,
      lastSeen: raw.lastSeen || "",
      reachability: raw.reachability || "",
      reachabilityReason: raw.reachabilityReason || "",
      reachabilityMs: typeof raw.reachabilityMs === "number" ? raw.reachabilityMs : 0,
      modlist: modlist,
      discord: raw.discord || "",
      tags: Array.isArray(raw.tags) ? raw.tags.slice() : []
    };
  }

  window.CO_ServerRecords = {
    portNumFrom: portNumFrom,
    normalizeDirectoryServer: normalizeDirectoryServer
  };
})();
