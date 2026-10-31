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

  function apply(state) {
    var list = state.allServers.slice();
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
    if (f.ping === "< 50 ms") {
      list = list.filter(function (s) {
        return s.ping < 50;
      });
    } else if (f.ping === "< 100 ms") {
      list = list.filter(function (s) {
        return s.ping < 100;
      });
    } else if (f.ping === "< 150 ms") {
      list = list.filter(function (s) {
        return s.ping < 150;
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

    state.servers = list;
    state.favorites = state.allServers.filter(function (s) {
      return s.favorite;
    });
  }

  window.CO_FilterState = {
    defaults: defaults,
    apply: apply
  };
})();
