(function () {
  "use strict";

  function validateDirectConnect(address, port) {
    if (!address || !String(address).trim()) {
      return "Server address is required.";
    }
    var portNum = parseInt(port, 10);
    if (!port || isNaN(portNum) || portNum < 1 || portNum > 65535) {
      return "Port must be a number between 1 and 65535.";
    }
    return null;
  }

  function create(deps) {
    function findServerInActiveList(serverId) {
      var list = deps.getActiveList();
      for (var i = 0; i < list.length; i++) {
        if (list[i].id === serverId) {
          return list[i];
        }
      }
      return null;
    }

    function joinServerEntry(server) {
      if (!server) return;
      var label = window.CO_ServerList
        ? CO_ServerList.displayName(server)
        : (server.name || server.host || "server");
      deps.showJoinOverlay("Connecting to " + label + "...");

      var browserEntry = server.id ? (deps.findServerById(server.id) || deps.findLocalServerById(server.id)) : null;
      if (browserEntry) {
        deps.sendAction("joinServer", { serverId: server.id });
        return;
      }

      var host = server.host || server.address;
      if (host) {
        deps.sendAction("openDirectConnect", {
          address: host,
          port: String(CO_ServerRecords.portNumFrom(server.port)),
          name: server.name || ""
        });
        return;
      }

      if (server.id) {
        deps.sendAction("joinServer", { serverId: server.id });
        return;
      }

      deps.showJoinOverlay("Server connection details unavailable.", true);
    }

    function joinServerById(serverId) {
      var server = deps.findServerById(serverId) || findServerInActiveList(serverId);
      if (!server) return;
      joinServerEntry(server);
    }

    function joinDirect(address, port, password) {
      var validationError = validateDirectConnect(address, port);
      if (validationError) {
        deps.showJoinOverlay(validationError, true);
        return;
      }
      deps.showJoinOverlay("Connecting to " + (String(address).indexOf(":") >= 0 && String(address).charAt(0) !== "[" ? "[" + address + "]" : address) + ":" + port + "...");
      deps.sendAction("openDirectConnect", { address: address, port: String(CO_ServerRecords.portNumFrom(port)), password: password || "" });
    }

    return {
      joinServerEntry: joinServerEntry,
      joinServerById: joinServerById,
      joinDirect: joinDirect
    };
  }

  window.CO_JoinFlow = {
    create: create
  };
})();
