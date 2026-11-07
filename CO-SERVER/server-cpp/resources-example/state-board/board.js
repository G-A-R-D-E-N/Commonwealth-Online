(function () {
  "use strict";

  var listeners = [];
  var state = {};

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

(function () {
  "use strict";

  var KEY = "state-board.pins";
  var pins = document.querySelector("[data-pins]");
  var field = document.querySelector("[data-text]");
  var status = document.querySelector("[data-status]");

  function text(value) {
    return value === null || value === undefined ? "" : String(value);
  }

  function render(state) {
    var global = state.global || {};
    var entries = Array.isArray(global[KEY]) ? global[KEY] : [];
    pins.replaceChildren();
    if (!entries.length) {
      var empty = document.createElement("li");
      empty.className = "board-empty";
      empty.textContent = "No pins yet.";
      pins.append(empty);
      return;
    }
    entries.forEach(function (entry) {
      var row = document.createElement("li");
      row.textContent = text(entry.text) + " — player " + text(entry.playerId);
      pins.append(row);
    });
  }

  window.coResource.subscribe(render);

  document.querySelector("[data-pin]").addEventListener("click", function () {
    var value = field.value.trim();
    if (!value) {
      status.textContent = "Type something first.";
      return;
    }
    window.coResource.send({ action: "pin", text: value });
    field.value = "";
    status.textContent = "Pinned.";
  });

  document.querySelector("[data-close]").addEventListener("click", function () {
    window.coResource.send({ action: "close" });
    status.textContent = "Closing.";
  });
})();
