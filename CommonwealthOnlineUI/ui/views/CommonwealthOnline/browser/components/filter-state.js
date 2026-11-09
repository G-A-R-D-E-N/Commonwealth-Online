(function () {
  "use strict";

  function defaults() {
    return {
      search: "",
      region: "Any Region",
      mode: "Any Mode",
      worldspace: "Any Worldspace",
      ping: "Any",
      showFull: false,
      hasPassword: false,
      modded: false
    };
  }

  var PREFERRED_REGIONS = ["North America", "Europe", "Asia", "Oceania"];
  var PING_LIMITS = { "< 50 ms": 50, "< 100 ms": 100, "< 150 ms": 150 };

  function sortByRegion(servers, preference) {
    if (!preference || preference === "Auto") {
      return servers.slice();
    }
    var matching = [];
    var unknown = [];
    var others = [];
    servers.forEach(function (s) {
      if (s.region === preference) {
        matching.push(s);
      } else if (PREFERRED_REGIONS.indexOf(s.region) < 0) {
        unknown.push(s);
      } else {
        others.push(s);
      }
    });
    return matching.concat(unknown, others);
  }

  function apply(state) {
    var list = state.allServers.filter(function (s) {
      return !s.savedOnly;
    });
    var f = state.filters;

    if (f.search) {
      var q = f.search.toLowerCase();
      list = list.filter(function (s) {
        var name = String(s.name || "").toLowerCase();
        var host = String(s.host || s.address || "").toLowerCase();
        var description = String(s.description || "").toLowerCase();
        return name.indexOf(q) >= 0 || host.indexOf(q) >= 0 || description.indexOf(q) >= 0;
      });
    }
    if (f.region !== "Any Region") {
      list = list.filter(function (s) {
        return s.region === f.region;
      });
    }
    if (f.mode !== "Any Mode") {
      list = list.filter(function (s) {
        return s.mode === f.mode;
      });
    }
    if (f.worldspace !== "Any Worldspace") {
      list = list.filter(function (s) {
        return s.worldspace === f.worldspace;
      });
    }
    var pingLimit = PING_LIMITS[f.ping];
    if (pingLimit) {
      list = list.filter(function (s) {
        var ping = window.CO_ServerRecords.effectivePing(s);
        return ping !== null && ping < pingLimit;
      });
    }
    if (!f.showFull) {
      list = list.filter(function (s) {
        return s.status !== "Full";
      });
    }
    if (f.hasPassword) {
      list = list.filter(function (s) {
        return s.passworded;
      });
    }
    if (f.modded) {
      list = list.filter(function (s) {
        return s.modded;
      });
    }

    state.servers = sortByRegion(list, state.regionPreference);
    state.favorites = state.allServers.filter(function (s) {
      return s.favorite;
    });
  }

  window.CO_FilterState = {
    defaults: defaults,
    apply: apply,
    sortByRegion: sortByRegion
  };
})();
