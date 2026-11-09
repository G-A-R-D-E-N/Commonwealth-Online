(function () {
  "use strict";

  var TABS = [
    { tab: "coTabFaction", panel: "coFaction" },
    { tab: "coTabFriends", panel: "coFriends" },
    { tab: "coTabVoice", panel: "coVoice" }
  ];
  var active = "coTabFaction";
  var painters = {};
  var SELECTOR = "button:not([disabled]), input, textarea, select:not([disabled])";

  function byId(id) {
    return document.getElementById(id) || null;
  }

  function present() {
    return TABS.filter(function (entry) {
      return byId(entry.tab) !== null && byId(entry.panel) !== null;
    });
  }

  function entryFor(entries, tabId) {
    for (var index = 0; index < entries.length; index += 1) {
      if (entries[index].tab === tabId) {
        return entries[index];
      }
    }
  }

  function show(tabId) {
    var entries = present();
    var wanted = entryFor(entries, tabId);
    if (!wanted) {
      return active;
    }
    for (var index = 0; index < entries.length; index += 1) {
      var entry = entries[index];
      var selected = entry.tab === tabId;
      byId(entry.panel).hidden = !selected;
      byId(entry.tab).setAttribute("aria-selected", String(selected));
    }
    active = tabId;
    var paint = painters[tabId];
    if (typeof paint === "function") {
      paint();
    }
    return active;
  }

  function registerPanel(tabId, painter) {
    if (typeof painter === "function") {
      painters[tabId] = painter;
    }
  }

  function panelForActive() {
    for (var index = 0; index < TABS.length; index += 1) {
      if (TABS[index].tab === active) {
        return byId(TABS[index].panel);
      }
    }
    return null;
  }

  function focusables() {
    var panel = panelForActive();
    if (!panel || panel.hidden) {
      return [];
    }
    return Array.prototype.slice.call(panel.querySelectorAll(SELECTOR));
  }

  function focusFirst() {
    var items = focusables();
    if (items.length && typeof items[0].focus === "function") {
      items[0].focus();
    }
  }

  function cycle(direction) {
    var entries = present();
    if (!entries.length) {
      return;
    }
    var at = 0;
    for (var index = 0; index < entries.length; index += 1) {
      if (entries[index].tab === active) {
        at = index;
      }
    }
    var step = direction < 0 ? -1 : 1;
    var next = entries[(at + step + entries.length) % entries.length];
    show(next.tab);
    focusFirst();
  }

  window.coFactionTabs = {
    show: show,
    registerPanel: registerPanel,
    cycle: cycle,
    focusables: focusables,
    active: function () {
      return active;
    }
  };
})();
