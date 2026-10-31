(function () {
  "use strict";

  var MAX_VISIBLE = 6;
  var FADE_MS = 10000;
  var TICK_MS = 250;

  function createFeed() {
    var entries = [];
    return {
      add: function (message, now) {
        entries = entries.concat([{ message: message, at: now }]).slice(-MAX_VISIBLE);
      },
      view: function (now) {
        entries = entries.filter(function (entry) {
          return now - entry.at < FADE_MS;
        });
        return entries.map(function (entry) {
          return { message: entry.message, opacity: 1 - (now - entry.at) / FADE_MS };
        });
      }
    };
  }

  var feed = createFeed();
  var list = null;

  function render() {
    if (!list) {
      return;
    }
    var rows = feed.view(Date.now());
    if (!rows.length && !list.firstChild) {
      return;
    }
    list.textContent = "";
    rows.forEach(function (row) {
      var line = document.createElement("li");
      line.className = "chat-feed-line";
      line.style.opacity = String(row.opacity);
      var name = document.createElement("span");
      name.className = "chat-feed-name";
      name.textContent = String(row.message.fromName == null ? "" : row.message.fromName);
      var text = document.createElement("span");
      text.className = "chat-feed-text";
      text.textContent = ": " + String(row.message.text == null ? "" : row.message.text);
      if (row.message.channel === "faction") {
        var label = document.createElement("span");
        label.className = "chat-feed-channel";
        label.textContent = "[Faction] ";
        line.appendChild(label);
      }
      line.appendChild(name);
      line.appendChild(text);
      list.appendChild(line);
    });
  }

  function init() {
    list = document.getElementById("coChatFeed");
    if (!list) {
      return;
    }
    setInterval(render, TICK_MS);
  }

  window.coChatEvent = function (event) {
    if (event && event.kind === "message" && event.message && typeof event.message === "object") {
      feed.add(event.message, Date.now());
      render();
    }
  };
  window.coChatFeedModel = {
    MAX_VISIBLE: MAX_VISIBLE,
    FADE_MS: FADE_MS,
    createFeed: createFeed
  };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
