(function () {
  "use strict";

  var TAB_SWITCH_PROMPTS = [
    { key: "LB", label: "Prev Tab" },
    { key: "RB", label: "Next Tab" }
  ];

  function renderIcon(key) {
    if (window.CO_ControllerIcons && CO_ControllerIcons.render) {
      return CO_ControllerIcons.render(key);
    }
    return '<span class="co-prompt-key">' + key + "</span>";
  }

  window.CO_ControllerPrompts = {
    render: function (root, state) {
      var el = root.querySelector(".co-prompts");
      if (!el) return;

      var prompts = CO_ControllerPrompts.getPrompts(state);
      el.innerHTML = prompts.map(function (p) {
        return (
          '<div class="co-prompt">' +
          renderIcon(p.key) +
          "<span>" + p.label + "</span>" +
          "</div>"
        );
      }).join("");

      if (window.CO_ControllerGlyphs && CO_ControllerGlyphs.hydrate) {
        CO_ControllerGlyphs.hydrate(el);
      }
    },

    getPrompts: function (state) {
      if (state.keyboardOpen) return [{ key: "A", label: "Type" }, { key: "B", label: "Cancel" }, { key: "LB", label: "Case" }, { key: "RB", label: "Case" }];
      if (state.passwordPrompt) return [{ key: "A", label: "Select" }, { key: "B", label: "Cancel" }];
      if (state.joinOverlay) {
        if (state.joinOverlay.error) {
          return [
            { key: "B", label: "Dismiss" },
            { key: "A", label: "Dismiss" }
          ];
        }
        return [{ key: "B", label: "Cancel" }];
      }

      if (state.activeTab === "direct-connect") {
        return TAB_SWITCH_PROMPTS.concat([
          { key: "A", label: state.focusZone !== "direct-form" ? "Select" : state.directConnectFieldIndex === 0 ? "Change" : state.directConnectFieldIndex < 3 ? "Edit" : state.directConnectFieldIndex === 4 ? "Retry" : "Connect" },
          { key: "B", label: "Back" }
        ]);
      }

      if (state.activeTab === "steam-friends") {
        var steamLabel = "Select";
        if (state.focusZone === "steam-form") {
          var steamId = (CO_SteamFriends.fieldIds(state)[state.steamFieldIndex]) || "";
          if (steamId.indexOf("co-lan-") === 0) steamLabel = "Join";
          else if (steamId === "co-steam-password" || steamId === "co-steam-friend-code" || steamId === "co-steam-join-password") steamLabel = "Edit";
          else if (window.CO_ServerSettings && CO_ServerSettings.ownsField(steamId)) steamLabel = CO_ServerSettings.promptLabel(steamId);
        }
        return TAB_SWITCH_PROMPTS.concat([
          { key: "A", label: steamLabel },
          { key: "X", label: "Scan" },
          { key: "B", label: "Back" }
        ]);
      }

      if (state.focusZone === "filters") {
        var isReset = state.selectedFilterIndex === CO_Filters.FILTER_ROWS.length;
        return TAB_SWITCH_PROMPTS.concat([
          { key: "A", label: isReset ? "Reset" : "Change" },
          { key: "B", label: "Back" }
        ]);
      }

      if (state.focusZone === "tabs") {
        return TAB_SWITCH_PROMPTS.concat([
          { key: "A", label: "Select" },
          { key: "B", label: "Back" }
        ]);
      }

      return TAB_SWITCH_PROMPTS.concat([
        { key: "A", label: "Join" },
        { key: "X", label: "Refresh" },
        { key: "Y", label: "Favorite" },
        { key: "B", label: "Back" }
      ]);
    }
  };
})();
