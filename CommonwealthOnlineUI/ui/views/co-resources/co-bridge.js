(function () {
  "use strict";

  var listeners = [], state = {};

  function notify() {
    listeners.slice().forEach(function (listener) {
      listener(state);
    });
  }

  window.addEventListener("coState", function (event) {
    state = event.detail && typeof event.detail === "object" ? event.detail : {};
    notify();
  });

  window.addEventListener("coBag", function (event) {
    var detail = event.detail || {};
    if (typeof detail.bag !== "string") return;
    if (detail.value === null || detail.value === undefined) delete state[detail.bag];
    else state[detail.bag] = detail.value;
    notify();
  });

  window.coResource = {
    subscribe: function (listener) {
      if (typeof listener !== "function") return function () {};
      listeners.push(listener);
      return function () {
        listeners = listeners.filter(function (entry) {
          return entry !== listener;
        });
      };
    },
    send: function (data) {
      if (typeof window.coResourceSend === "function") window.coResourceSend(JSON.stringify(data === undefined ? null : data));
    }
  };
})();
