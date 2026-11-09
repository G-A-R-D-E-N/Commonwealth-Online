(function () {
  "use strict";

  var TAB_ORDER = ["direct-connect", "server-browser", "steam-friends", "favorites", "recent"];

  var TAGS = {
    "direct-connect": "DIR",
    "server-browser": "01",
    "steam-friends": "STM",
    favorites: "03",
    recent: "04"
  };

  function isVisible(tabId, state) {
    return tabId !== "steam-friends" || state.steamP2pAvailable === true;
  }

  window.CO_Tabs = {
    TAB_ORDER: TAB_ORDER,

    nextIndex: function (index, step, state) {
      var count = TAB_ORDER.length;
      var next = index;
      for (var i = 0; i < count; i++) {
        next = (next + step + count) % count;
        if (isVisible(TAB_ORDER[next], state)) return next;
      }
      return index;
    },

    render: function (root, state) {
      var navEl = root.querySelector(".co-nav-list");
      if (!navEl) return;

      var labels = {
        "direct-connect": "Direct Connect",
        "server-browser": "Browse",
        "steam-friends": "Friends (Steam)",
        favorites: "Favorites",
        recent: "Recent"
      };

      navEl.innerHTML = TAB_ORDER.map(function (tabId, index) {
        if (!isVisible(tabId, state)) return "";
        var active = state.activeTab === tabId ? " active" : "";
        var focused = state.focusZone === "tabs" && state.selectedTabIndex === index ? " focused" : "";
        var tag = TAGS[tabId] || ("0" + (index + 1));
        return (
          '<button type="button" class="co-nav-item' + active + focused + '" data-tab="' + tabId + '" data-index="' + index + '">' +
          '<span class="co-nav-tag">[' + tag + ']</span>' +
          '<span class="co-nav-label">' + labels[tabId] + '</span>' +
          '<span class="co-nav-indicator" aria-hidden="true"></span>' +
          "</button>"
        );
      }).join("");

      navEl.className = "co-nav-list" + (state.focusZone === "tabs" ? " focused" : "");
    },

    bind: function (root, onTabClick) {
      var navEl = root.querySelector(".co-nav-list");
      if (!navEl) return;

      navEl.addEventListener("click", function (e) {
        var btn = e.target.closest(".co-nav-item");
        if (!btn) return;
        onTabClick(btn.getAttribute("data-tab"));
      });
    }
  };
})();
