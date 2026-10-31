(function () {
  "use strict";

  function create(options) {
    var ctx = {};
    var model = window.CO_SettingsModel;
    if (!options || !options.mount) {
      throw new Error("CO_Settings.create requires a mount element");
    }
    ctx.mountEl = options.mount;
    ctx.promptBarEl = options.promptBar || null;
    ctx.bridge = options.bridge || {};
    ctx.onBack = typeof options.onBack === "function" ? options.onBack : function () {};
    ctx.layout = options.layout || "embedded";
    ctx.showHeading = options.showHeading !== false;
    ctx.multiplayerSettings = model.deepCloneSettingsData(options.settingsData || window.CO_SettingsData);
    ctx.flatMultiplayerSettings = model.flattenSettings(ctx.multiplayerSettings);
    ctx.gameSettings = [];
    ctx.flatGameSettings = [];
    ctx.loadedNativeSettings = [];
    ctx.visible = false;
    ctx.settingsFocus = "tabs";
    ctx.activeTab = options.defaultTab || "game";
    ctx.settingIndex = 0;
    ctx.actionIndex = 0;
    ctx.confirmOpen = false;
    ctx.confirmIndex = 0;
    ctx.pendingLeave = false;
    ctx.requestSeq = 0;
    ctx.pendingRequests = {};
    ctx.controllerReadyAt = 0;
    ctx.controllerButtonTimes = {};
    ctx.CONTROLLER_DEBOUNCE_MS = 160;
    ctx.mountEl.innerHTML = model.buildMarkup({
      layout: ctx.layout,
      showHeading: ctx.showHeading,
      gameTabLabel: options.gameTabLabel,
      multiplayerTabLabel: options.multiplayerTabLabel
    });
    ctx.rootEl = ctx.mountEl.querySelector(".co-settings");
    ctx.settingsTabsEl = ctx.mountEl.querySelector("[data-co-settings-tabs]");
    ctx.settingsRowsEl = ctx.mountEl.querySelector("[data-co-settings-rows]");
    ctx.settingsDescriptionEl = ctx.mountEl.querySelector("[data-co-settings-description]");
    ctx.gameRowsEl = ctx.mountEl.querySelector("[data-co-game-settings-rows]");
    ctx.gameDescriptionEl = ctx.mountEl.querySelector("[data-co-game-settings-description]");
    ctx.gameStatusEl = ctx.mountEl.querySelector("[data-co-game-settings-status]");
    ctx.gameActionsEl = ctx.mountEl.querySelector("[data-co-game-settings-actions]");
    ctx.loadTimeoutId = null;
    ctx.statusTimerId = null;
    ctx.flashTimerIds = {};
    ctx.profileError = "";
    ctx.gameSettingsPane = ctx.mountEl.querySelector('[data-co-settings-pane="game"]');
    ctx.multiplayerSettingsPane = ctx.mountEl.querySelector('[data-co-settings-pane="multiplayer"]');
    ctx.confirmEl = ctx.mountEl.querySelector("[data-co-settings-confirm]");
    ctx.tabButtons = ctx.settingsTabsEl ? Array.prototype.slice.call(ctx.settingsTabsEl.querySelectorAll(".settings-tab")) : [];
    ctx.actionButtons = ctx.gameActionsEl ? Array.prototype.slice.call(ctx.gameActionsEl.querySelectorAll("[data-co-settings-action]")) : [];
    ctx.confirmButtons = ctx.confirmEl ? Array.prototype.slice.call(ctx.confirmEl.querySelectorAll("[data-co-confirm-choice]")) : [];
    ctx.getActiveFlat = function () {
      return ctx.activeTab === "game" ? ctx.flatGameSettings : ctx.flatMultiplayerSettings;
    };
    ctx.getCurrentSetting = function () {
      var flat = ctx.getActiveFlat();
      return flat[ctx.settingIndex] || null;
    };
    ctx.hasDirtyGameSettings = function () {
      return ctx.flatGameSettings.some(function (setting) {
        return setting.dirty && setting.supported !== false;
      });
    };
    ctx.syncDisplayValue = function (setting) {
      if (setting.type !== "slider") {
        return;
      }
      if (setting.uiUsesPercent && setting.nativeType === "float" && setting.max != null && setting.max <= 1) {
        setting.displayValue = Math.round((Number(setting.value) || 0) * 100);
      } else {
        setting.displayValue = Number(setting.value) || 0;
      }
    };
    ctx.setMultiplayerValue = function (setting, value) {
      if (typeof ctx.bridge.setMultiplayerSetting !== "function" || ctx.bridge.setMultiplayerSetting(setting.key, value) !== true) {
        setting.value = value;
      }
    };
    ctx.setPendingValue = function (setting, value) {
      if (!setting || !setting.supported || setting.editable === false) {
        return;
      }
      setting.value = value;
      setting.dirty = !model.valuesEqual(setting.value, setting.baselineValue);
      ctx.syncDisplayValue(setting);
      ctx.sendSettingsAction("settingsSet", {
        id: setting.key,
        value: setting.value
      });
      ctx.render();
    };
    ctx.setActiveTab = function (tabId) {
      if (model.TAB_IDS.indexOf(tabId) === -1) {
        return;
      }
      ctx.activeTab = tabId;
      ctx.settingIndex = 0;
      ctx.settingsFocus = "tabs";
      ctx.render();
    };
    ctx.moveTab = function (delta) {
      var current = model.TAB_IDS.indexOf(ctx.activeTab);
      if (current < 0) {
        current = 0;
      }
      var next = current + delta;
      if (next < 0) {
        next = model.TAB_IDS.length - 1;
      } else if (next >= model.TAB_IDS.length) {
        next = 0;
      }
      ctx.setActiveTab(model.TAB_IDS[next]);
    };
    ctx.setSettingsFocus = function (region) {
      ctx.settingsFocus = region;
      ctx.render();
    };
    ctx.moveSettingRow = function (delta) {
      var flat = ctx.getActiveFlat();
      if (!flat.length) {
        return;
      }
      var next = ctx.settingIndex + delta;
      if (next < 0) {
        next = flat.length - 1;
      } else if (next >= flat.length) {
        next = 0;
      }
      ctx.settingIndex = next;
      ctx.render();
      var rowsEl = ctx.activeTab === "game" ? ctx.gameRowsEl : ctx.settingsRowsEl;
      if (rowsEl) {
        var selectedRow = rowsEl.querySelector('[data-setting-index="' + ctx.settingIndex + '"]');
        if (selectedRow && typeof selectedRow.scrollIntoView === "function") {
          selectedRow.scrollIntoView({
            block: "nearest"
          });
        }
      }
    };
    ctx.moveAction = function (delta) {
      if (!ctx.actionButtons.length) {
        return;
      }
      ctx.actionIndex = (ctx.actionIndex + delta + ctx.actionButtons.length) % ctx.actionButtons.length;
      ctx.render();
    };
    ctx.adjustColour = function (setting, delta) {
      var colour = model.deepClone(setting.value) || {
        r: 0,
        g: 0,
        b: 0
      };
      var channelNames = ["r", "g", "b"];
      if (delta === 0) {
        setting.colourChannel = ((setting.colourChannel || 0) + 1) % 3;
        ctx.render();
        return;
      }
      var channel = channelNames[setting.colourChannel || 0];
      colour[channel] = Math.max(0, Math.min(255, Number(colour[channel] || 0) + delta * 5));
      ctx.setPendingValue(setting, colour);
    };
    ctx.cycleColourPreset = function (setting, delta) {
      var current = model.colourToHex(setting.value);
      var index = 0;
      for (var i = 0; i < model.COLOUR_PRESETS.length; i += 1) {
        if (model.colourToHex(model.COLOUR_PRESETS[i]) === current) {
          index = i;
          break;
        }
      }
      var next = (index + delta + model.COLOUR_PRESETS.length) % model.COLOUR_PRESETS.length;
      ctx.setPendingValue(setting, model.deepClone(model.COLOUR_PRESETS[next]));
    };
    ctx.activateSetting = function () {
      var setting = ctx.getCurrentSetting();
      if (!setting) {
        return;
      }
      if (ctx.activeTab === "multiplayer") {
        if (setting.type === "text" && setting.editable !== false) {
          var input = ctx.settingsRowsEl.querySelector('[data-setting-key="' + setting.key + '"] input');
          if (input) input.focus();
          return;
        }
        if (setting.type === "keybind" && typeof ctx.bridge.beginKeyCapture === "function") {
          ctx.bridge.beginKeyCapture(setting.key);
        }
        if (setting.type === "toggle" || setting.type === "select" || setting.type === "slider") {
          ctx.adjustSetting(setting.type === "toggle" ? 0 : 1);
        }
        return;
      }
      if (setting.type === "colour") {
        ctx.adjustColour(setting, 0);
        return;
      }
      if (setting.type === "toggle" || setting.type === "select" || setting.type === "slider" || setting.type === "resolution") {
        ctx.adjustSetting(setting.type === "toggle" ? 0 : 1);
      }
    };
    ctx.showTransientGameStatus = function (message, isSuccess) {
      if (ctx.statusTimerId) {
        clearTimeout(ctx.statusTimerId);
        ctx.statusTimerId = null;
      }
      ctx.setGameStatus(message, false, isSuccess);
      ctx.statusTimerId = setTimeout(function () {
        ctx.statusTimerId = null;
        if (ctx.gameStatusEl && ctx.gameStatusEl.textContent === message) {
          ctx.setGameStatus("");
        }
      }, 3000);
    };
    ctx.settleSettingsAction = function (actionName, label, successMessage, result) {
      var button = ctx.mountEl.querySelector('[data-co-settings-action="' + actionName + '"]');
      if (button && result && result.success) {
        button.classList.add("is-confirmed");
        clearTimeout(ctx.flashTimerIds[actionName]);
        ctx.flashTimerIds[actionName] = setTimeout(function () {
          button.classList.remove("is-confirmed");
        }, 600);
      }
      if (result && result.success) {
        ctx.showTransientGameStatus(successMessage, true);
        return;
      }
      var detail = result && result.error ? result.error.message || result.error.code : "";
      ctx.setGameStatus(label + " failed: " + (detail || "unknown error"), true);
    };
    ctx.applyGameSettings = function (callback) {
      ctx.sendSettingsAction("settingsApply", {}, function (result) {
        if (result && result.success && result.data) {
          ctx.applySnapshot(result.data);
        }
        ctx.settleSettingsAction("apply", "Apply", "Settings applied.", result);
        if (typeof callback === "function") {
          callback(result);
        }
      });
    };
    ctx.cancelGameSettings = function () {
      ctx.sendSettingsAction("settingsCancel", {}, function (result) {
        if (result && result.success && result.data) {
          ctx.applySnapshot(result.data);
        }
        ctx.settleSettingsAction("cancel", "Cancel", "Changes discarded.", result);
      });
    };
    ctx.restoreFromMenuOpen = function () {
      ctx.sendSettingsAction("settingsResetAll", {}, function (result) {
        if (result && result.success && result.data) {
          ctx.applySnapshot(result.data);
        }
        ctx.settleSettingsAction("restore", "Restore", "Restored values from when the menu opened.", result);
      });
    };
    ctx.requestLeave = function () {
      if (ctx.activeTab === "game" && ctx.hasDirtyGameSettings()) {
        ctx.confirmOpen = true;
        ctx.confirmIndex = Math.min(1, ctx.confirmButtons.length - 1);
        ctx.pendingLeave = true;
        ctx.render();
        return;
      }
      ctx.hide();
      ctx.onBack();
    };
    ctx.discardAndLeave = function () {
      if (ctx.activeTab === "game" && ctx.hasDirtyGameSettings()) {
        ctx.cancelGameSettings();
      }
      ctx.hide();
      ctx.onBack();
    };
    ctx.resolveConfirm = function (choice) {
      if (choice === "stay") {
        ctx.confirmOpen = false;
        ctx.pendingLeave = false;
        ctx.render();
        return;
      }
      if (choice === "discard") {
        ctx.confirmOpen = false;
        ctx.pendingLeave = false;
        ctx.cancelGameSettings();
        ctx.hide();
        ctx.onBack();
        return;
      }
      if (choice === "apply") {
        ctx.applyGameSettings(function (result) {
          ctx.confirmOpen = false;
          ctx.pendingLeave = false;
          if (result && result.success) {
            ctx.hide();
            ctx.onBack();
          } else {
            ctx.render();
          }
        });
      }
    };
    ctx.runAction = function (actionName) {
      if ((actionName === "apply" || actionName === "cancel") && !ctx.hasDirtyGameSettings()) {
        ctx.showTransientGameStatus(actionName === "apply" ? "No changes to apply." : "No changes to discard.", false);
        return;
      }
      if (actionName === "apply") {
        ctx.applyGameSettings();
        return;
      }
      if (actionName === "cancel") {
        ctx.cancelGameSettings();
        return;
      }
      if (actionName === "restore") {
        ctx.restoreFromMenuOpen();
      }
    };
    ctx.show = function () {
      ctx.visible = true;
      ctx.controllerReadyAt = Date.now() + 180;
      ctx.controllerButtonTimes = {};
      ctx.settingsFocus = "tabs";
      ctx.activeTab = options.defaultTab || "game";
      ctx.settingIndex = 0;
      ctx.actionIndex = 0;
      ctx.confirmOpen = false;
      ctx.pendingLeave = false;
      if (ctx.promptBarEl) {
        ctx.promptBarEl.hidden = false;
      }
      ctx.setGameStatus("Loading Fallout 4 settings…", false);
      if (ctx.loadTimeoutId) {
        clearTimeout(ctx.loadTimeoutId);
      }
      ctx.loadTimeoutId = setTimeout(function () {
        if (!ctx.flatGameSettings.length) {
          var bridgeState = typeof window.coSettingsAction === "function" ? "coSettingsAction present" : typeof window.coAction === "function" ? "coAction present" : "bridge MISSING";
          ctx.setGameStatus("Timed out waiting for native settings (" + bridgeState + "). Confirm CommonwealthOnlineUI.dll is loaded, then restart the game.", true);
        }
      }, 6000);
      window.dispatchCoSettingsEvent = ctx.handleSettingsEvent;
      ctx.sendSettingsAction("settingsGetMultiplayerProfile", {}, function (result) {
        if (result && result.success && result.data) {
          ctx.flatMultiplayerSettings.forEach(function (setting) {
            if (setting.key === "profile.displayName") setting.value = result.data.displayName;
          });
          ctx.render();
        }
      });
      ctx.sendSettingsAction("settingsGetAll", {}, function (result) {
        if (result && result.success && result.data) {
          ctx.applySnapshot(result.data);
        } else if (!ctx.flatGameSettings.length) {
          var message = result && result.error && result.error.message ? result.error.message : "Native settings bridge did not return data.";
          ctx.setGameStatus(message, true);
        }
      });
      ctx.render();
    };
    ctx.hide = function () {
      ctx.visible = false;
      ctx.confirmOpen = false;
      if (ctx.loadTimeoutId) {
        clearTimeout(ctx.loadTimeoutId);
        ctx.loadTimeoutId = null;
      }
      if (ctx.promptBarEl) {
        ctx.promptBarEl.hidden = true;
      }
    };
    ctx.destroy = function () {
      ctx.hide();
      if (window.dispatchCoSettingsEvent === ctx.handleSettingsEvent) {
        delete window.dispatchCoSettingsEvent;
      }
      ctx.mountEl.innerHTML = "";
    };
    window.CO_SettingsNative(ctx, model);
    window.CO_SettingsRender(ctx, model);
    window.CO_SettingsInput(ctx, model);
    window.dispatchCoSettingsEvent = ctx.handleSettingsEvent;
    window.CO_Settings_onNativeEvent = ctx.handleSettingsEvent;
    if (window.__coSettingsEventQueue && window.__coSettingsEventQueue.length) {
      var queued = window.__coSettingsEventQueue.slice();
      window.__coSettingsEventQueue.length = 0;
      queued.forEach(function (payload) {
        ctx.handleSettingsEvent(payload);
      });
    }
    ctx.tabButtons.forEach(function (tab) {
      tab.addEventListener("click", function () {
        ctx.settingsFocus = "tabs";
        ctx.setActiveTab(tab.getAttribute("data-tab"));
      });
    });
    ctx.actionButtons.forEach(function (button, index) {
      button.addEventListener("click", function () {
        ctx.actionIndex = index;
        ctx.settingsFocus = "actions";
        ctx.runAction(button.getAttribute("data-co-settings-action"));
        ctx.render();
      });
    });
    ctx.confirmButtons.forEach(function (button, index) {
      button.addEventListener("click", function () {
        ctx.confirmIndex = index;
        ctx.resolveConfirm(button.getAttribute("data-co-confirm-choice"));
      });
    });
    return {
      show: ctx.show,
      hide: ctx.hide,
      isVisible: function () {
        return ctx.visible;
      },
      handleKeydown: ctx.handleKeydown,
      handleControllerInput: ctx.handleControllerInput,
      back: ctx.discardAndLeave,
      handleSettingsEvent: ctx.handleSettingsEvent,
      render: ctx.render,
      destroy: ctx.destroy,
      getSettingsData: function () {
        return ctx.multiplayerSettings;
      },
      getGameSettingsData: function () {
        return ctx.gameSettings;
      },
      hasUnappliedChanges: ctx.hasDirtyGameSettings
    };
  }
  window.CO_Settings = {
    create: create
  };
})();
