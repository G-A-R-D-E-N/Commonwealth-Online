(function () {
  "use strict";

  var FOCUS_ZONES = ["tabs", "server-list", "filters"];
  var DIRECT_FIELD_IDS = ["#co-dc-address", "#co-dc-port", "#co-dc-password"];

  function create(deps) {
    var state = deps.state;
    var root = deps.root;

    function nextFocusZone(fromIdx, direction) {
      var zones = state.activeTab === "direct-connect"
        ? ["tabs", "direct-form"]
        : state.activeTab === "steam-friends"
          ? ["tabs", "steam-form"]
          : state.activeTab === "settings"
            ? ["tabs"]
            : FOCUS_ZONES.filter(function (z) {
              return z !== "filters" || state.activeTab === "server-browser";
            });
      var current = zones.indexOf(state.focusZone);
      if (current < 0) current = fromIdx;
      var next = current + direction;
      return next < 0 || next >= zones.length ? null : zones[next];
    }

    function commitTabPreview() {
      var tabName = CO_Tabs.TAB_ORDER[state.selectedTabIndex];
      if (tabName === state.activeTab && (tabName === "direct-connect" || tabName === "steam-friends" || tabName === "settings")) return;
      deps.switchTab(tabName, { skipAction: tabName === state.activeTab });
    }

    function cycleFilterValue(filterKey, filterOptions) {
      var current = state.filters[filterKey];
      var index = filterOptions.indexOf(current);
      if (index < 0) index = 0;
      var next = filterOptions[(index + 1) % filterOptions.length];
      state.filters[filterKey] = next;
      deps.sendAction("setFilter", { filterName: filterKey, value: next });
      deps.applyLocalFilters();
    }

    function activateFocusedFilter() {
      var maxFilter = CO_Filters.FILTER_ROWS.length;
      if (state.selectedFilterIndex === maxFilter) {
        deps.resetLocalFilters();
        return;
      }

      var row = CO_Filters.FILTER_ROWS[state.selectedFilterIndex];
      if (!row) return;

      if (row.type === "text") {
        CO_Keyboard.open(root.querySelector('[data-filter="' + row.key + '"]'));
      } else if (row.type === "select") {
        cycleFilterValue(row.key, row.options);
      } else if (row.type === "checkbox") {
        state.filters[row.key] = !state.filters[row.key];
        deps.sendAction("setFilter", { filterName: row.key, value: state.filters[row.key] });
        deps.applyLocalFilters();
      }
    }

    function navigateFocus(button) {
      var list = deps.getActiveList();
      var outcome = "unmatched";
      if (window.CO_ControllerTrace) {
        CO_ControllerTrace.enter("navigateFocus", button, state);
      }
      if (button === "dpad_left" || button === "dpad_right") {
        var zone = nextFocusZone(FOCUS_ZONES.indexOf(state.focusZone), button === "dpad_left" ? -1 : 1);
        if (zone) {
          deps.releaseFieldFocus();
          state.focusZone = zone;
          if (zone === "tabs") state.selectedTabIndex = CO_Tabs.TAB_ORDER.indexOf(state.activeTab);
          outcome = "focusZone";
          deps.render();
        } else {
          outcome = "edge";
        }
      } else if (state.focusZone === "tabs") {
        if (button === "dpad_up" || button === "dpad_down") {
          state.selectedTabIndex = CO_Tabs.nextIndex(state.selectedTabIndex, button === "dpad_up" ? -1 : 1, state);
          outcome = "tabPreview";
          deps.render();
        }
      } else if (state.focusZone === "filters") {
        var maxFilter = CO_Filters.FILTER_ROWS.length;
        if (button === "dpad_up") state.selectedFilterIndex = Math.max(0, state.selectedFilterIndex - 1);
        if (button === "dpad_down") state.selectedFilterIndex = Math.min(maxFilter, state.selectedFilterIndex + 1);
        if (button === "a") activateFocusedFilter();
        outcome = "filterIndex";
        deps.render();
      } else if (state.focusZone === "server-list") {
        if (button === "dpad_up") state.selectedServerIndex = Math.max(0, state.selectedServerIndex - 1);
        if (button === "dpad_down") state.selectedServerIndex = Math.max(0, Math.min(list.length - 1, state.selectedServerIndex + 1));
        if (button === "dpad_up" || button === "dpad_down") deps.armSelection();
        outcome = "serverIndex";
        deps.render();
        CO_ServerList.scrollToSelected(root, state.selectedServerIndex);
      }
      if (window.CO_ControllerTrace) {
        CO_ControllerTrace.exit("navigateFocus", button, state, outcome);
      }
    }

    function handleButton(button, phase, options) {
      if (!button || phase !== "down") return;
      if (CO_Keyboard.isOpen()) {
        CO_Keyboard.handleButton(button);
        deps.render();
        return;
      }
      if (state.joinOverlay) {
        if (button === "b" && !state.joinOverlay.error) {
          deps.cancelJoin();
        } else if (button === "b" || (state.joinOverlay.error && button === "a")) {
          deps.hideJoinOverlay();
        }
        return;
      }

      if (button === "lb" || button === "rb") {
        var idx = CO_Tabs.nextIndex(CO_Tabs.TAB_ORDER.indexOf(state.activeTab), button === "lb" ? -1 : 1, state);
        deps.switchTab(CO_Tabs.TAB_ORDER[idx]);
        return;
      }

      function activateSteamField() {
        var ids = CO_SteamFriends.fieldIds(state);
        var id = ids[state.steamFieldIndex];
        var el = id ? root.querySelector("#" + id) : null;
        if (!el) return;
        if (window.CO_ServerSettings && CO_ServerSettings.ownsField(id)) {
          CO_ServerSettings.activate(root, id, state);
          return;
        }
        var lanOrScan = id === "co-steam-scan" || id.indexOf("co-lan-") === 0;
        if (!lanOrScan && state.steamP2pAvailable !== true) return;
        if (el.tagName === "BUTTON") {
          el.dispatchEvent({ type: "click", bubbles: true });
          return;
        }
        CO_Keyboard.open(el);
      }

      if (button === "b") {
        if ((state.activeTab === "direct-connect" && state.focusZone === "direct-form") ||
            (state.activeTab === "steam-friends" && state.focusZone === "steam-form")) {
          deps.releaseFieldFocus();
          state.focusZone = "tabs";
          state.selectedTabIndex = CO_Tabs.TAB_ORDER.indexOf(state.activeTab);
          deps.render();
          return;
        }
        deps.exitBrowser();
        return;
      }

      if (state.activeTab === "direct-connect") {
        if (button === "dpad_left" || button === "dpad_right") {
          navigateFocus(button);
          return;
        }
        if (state.focusZone === "direct-form") {
          var last = state.retryAvailable ? 4 : 3;
          if (button === "dpad_up") state.directConnectFieldIndex = Math.max(0, state.directConnectFieldIndex - 1);
          if (button === "dpad_down") state.directConnectFieldIndex = Math.min(last, state.directConnectFieldIndex + 1);
          if (button === "a") {
            if (options && options.confirmText || state.directConnectFieldIndex === 3) {
              var vals = CO_DirectConnect.getValues(root);
              deps.releaseFieldFocus();
              deps.joinDirect(vals.address, vals.port, vals.password);
              CO_DirectConnect.clearPassword(root);
            } else if (state.directConnectFieldIndex === 4 && state.retryAvailable) {
              deps.retryLastJoin();
            } else {
              CO_Keyboard.open(root.querySelector(DIRECT_FIELD_IDS[state.directConnectFieldIndex]));
            }
          }
          deps.render();
        } else if (button === "a" && state.focusZone === "tabs") {
          commitTabPreview();
        } else {
          navigateFocus(button);
        }
        return;
      }

      if (state.activeTab === "steam-friends") {
        if (button === "x") {
          deps.sendAction("scanLocalServers", {});
          return;
        }
        if (button === "dpad_left" || button === "dpad_right") {
          navigateFocus(button);
          return;
        }
        if (state.focusZone === "steam-form") {
          var fields = CO_SteamFriends.fieldIds(state);
          var lastField = Math.max(0, fields.length - 1);
          if (button === "dpad_up") state.steamFieldIndex = Math.max(0, state.steamFieldIndex - 1);
          if (button === "dpad_down") state.steamFieldIndex = Math.min(lastField, state.steamFieldIndex + 1);
          if (button === "a") activateSteamField();
          deps.render();
        } else if (button === "a" && state.focusZone === "tabs") {
          commitTabPreview();
        } else {
          navigateFocus(button);
        }
        return;
      }

      if (button === "a") {
        if (state.focusZone === "filters") {
          activateFocusedFilter();
        } else if (state.focusZone === "tabs") {
          commitTabPreview();
        } else {
          deps.joinSelected();
        }
        deps.render();
        return;
      }
      if (button === "x") {
        if (state.activeTab === "local") {
          deps.sendAction("scanLocalServers", {});
        } else {
          deps.sendAction("refreshServerList", {});
        }
        return;
      }
      if (button === "y") {
        var sel = deps.getSelectedServer();
        if (sel) deps.toggleFavorite(sel.id);
        return;
      }

      navigateFocus(button);
    }

    return {
      handleButton: handleButton
    };
  }

  window.CO_ControllerNavigation = {
    create: create
  };
})();
