(function () {
  "use strict";

  function describe(event) {
    if (!event || !event.state) return null;
    if (event.state === "connecting" || event.state === "connected") {
      return { message: event.message || "Connecting to the server...", error: false };
    }
    if (event.state === "ready" || event.state === "characterLoaded") {
      return { hide: true };
    }
    if (event.state === "spaceChange") {
      return { message: event.message || "Loading area...", error: false };
    }
    if (event.state === "disconnected") {
      return { message: event.reason || "Disconnected.", error: true };
    }
    return null;
  }

  function apply(event, deps) {
    var view = describe(event);
    if (!view || !deps) return;
    if (view.hide) {
      deps.hide();
      return;
    }
    deps.show(view.message, view.error);
  }

  window.CO_SessionState = {
    describe: describe,
    apply: apply
  };
})();
