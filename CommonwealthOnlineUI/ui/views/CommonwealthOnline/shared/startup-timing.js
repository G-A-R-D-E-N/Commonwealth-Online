(function () {
  "use strict";
  var samples = [];
  var shown = false;
  window.CO_StartupTiming = {
    record: function (name, started) {
      var sample = { name: name, ms: Date.now() - started };
      if (shown) console.log("[StartupTiming] " + JSON.stringify(sample));
      else samples.push(sample);
    },
    flush: function () {
      if (shown) return;
      shown = true;
      var resources = [];
      if (window.performance && typeof window.performance.getEntriesByType === "function") {
        resources = window.performance.getEntriesByType("resource").map(function (entry) {
          return { name: entry.name, ms: entry.duration };
        });
      }
      console.log("[StartupTiming] " + JSON.stringify({ samples: samples, resources: resources }));
      samples = [];
    }
  };
})();
