(function () {
  "use strict";

  window.CO_SettingsNative = function (ctx, model) {
    ctx.nextRequestId = function () {
      ctx.requestSeq += 1;
      return "settings-req-" + ctx.requestSeq;
    };
    ctx.sendSettingsAction = function (action, payload, callback) {
      var requestId = ctx.nextRequestId();
      var body = Object.assign({
        action: action,
        requestId: requestId
      }, payload || {});
      if (typeof callback === "function") {
        ctx.pendingRequests[requestId] = callback;
      }
      var sent = false;
      var msg = JSON.stringify(body);
      if (typeof window.coSettingsAction === "function") {
        window.coSettingsAction(msg);
        sent = true;
      } else if (typeof ctx.bridge.sendSettingsAction === "function") {
        sent = !!ctx.bridge.sendSettingsAction(body);
      } else if (typeof window.coAction === "function") {
        window.coAction(msg);
        sent = true;
      }
      if (!sent) {
        console.warn("[CO Settings] Native settings bridge unavailable:", body);
        if (typeof callback === "function") {
          delete ctx.pendingRequests[requestId];
          callback({
            success: false,
            error: {
              code: "ENGINE_NOT_READY",
              message: "coSettingsAction/coAction unavailable. Is CommonwealthOnlineUI.dll loaded?"
            }
          });
        }
        return false;
      }
      return true;
    };
    ctx.setGameStatus = function (message, isError, isSuccess) {
      if (!ctx.gameStatusEl) {
        return;
      }
      if (!message) {
        ctx.gameStatusEl.hidden = true;
        ctx.gameStatusEl.textContent = "";
        ctx.gameStatusEl.classList.remove("is-error");
        ctx.gameStatusEl.classList.remove("is-success");
        return;
      }
      ctx.gameStatusEl.hidden = false;
      ctx.gameStatusEl.textContent = message;
      ctx.gameStatusEl.classList.toggle("is-error", !!isError);
      ctx.gameStatusEl.classList.toggle("is-success", !!isSuccess);
    };
    ctx.applySnapshot = function (data) {
      ctx.gameSettings = model.groupGameSettings(data && data.settings || []);
      ctx.flatGameSettings = model.flattenSettings(ctx.gameSettings);
      if (ctx.settingIndex >= ctx.flatGameSettings.length) {
        ctx.settingIndex = Math.max(0, ctx.flatGameSettings.length - 1);
      }
      if (ctx.flatGameSettings.length) {
        ctx.setGameStatus("");
      } else {
        ctx.setGameStatus("No Fallout 4 settings were returned by the game.", true);
      }
      ctx.render();
    };
    ctx.handleSettingsEvent = function (rawEvent) {
      var event = rawEvent;
      if (typeof event === "string") {
        try {
          event = JSON.parse(event);
        } catch (err) {
          console.warn("[CO Settings] Failed to parse settings event:", err);
          return;
        }
      }
      if (!event || typeof event !== "object") {
        return;
      }
      if (event.requestId) {
        var deliveryKey = event.requestId + ":" + event.type;
        if (event.type === "settingsBatch") {
          deliveryKey += ":" + String(event.offset == null ? 0 : event.offset);
        }
        if (ctx.handleSettingsEvent._seen && ctx.handleSettingsEvent._seen[deliveryKey]) {
          return;
        }
        ctx.handleSettingsEvent._seen = ctx.handleSettingsEvent._seen || {};
        ctx.handleSettingsEvent._seen[deliveryKey] = true;
      }
      if (event.type === "settingsAck") {
        if (ctx.loadTimeoutId) {
          clearTimeout(ctx.loadTimeoutId);
          ctx.loadTimeoutId = null;
        }
        ctx.setGameStatus("Reading Fallout 4 settings…", false);
        return;
      }
      if (event.type === "settingsBatch" && event.settings) {
        if (ctx.loadTimeoutId) {
          clearTimeout(ctx.loadTimeoutId);
          ctx.loadTimeoutId = null;
        }
        if (event.reset) {
          ctx.loadedNativeSettings = [];
        }
        ctx.loadedNativeSettings = ctx.loadedNativeSettings.concat(event.settings || []);
        ctx.applySnapshot({
          settings: ctx.loadedNativeSettings
        });
        ctx.setGameStatus("Loading settings… " + ctx.loadedNativeSettings.length + " loaded", false);
        return;
      }
      if (event.type === "settingsSnapshot" && event.success && event.data) {
        if (ctx.loadTimeoutId) {
          clearTimeout(ctx.loadTimeoutId);
          ctx.loadTimeoutId = null;
        }
        ctx.applySnapshot(event.data);
        return;
      }
      if (event.type === "settingsResult") {
        if (ctx.loadTimeoutId && event.action === "settingsGetAll") {
          clearTimeout(ctx.loadTimeoutId);
          ctx.loadTimeoutId = null;
        }
        if (event.success && event.data && event.data.settings) {
          ctx.loadedNativeSettings = event.data.settings.slice();
          ctx.applySnapshot(event.data);
          if (event.data.complete) {
            ctx.setGameStatus("");
          }
        } else if (event.success && event.data && event.data.id) {
          for (var i = 0; i < ctx.flatGameSettings.length; i += 1) {
            if (ctx.flatGameSettings[i].key === event.data.id) {
              ctx.flatGameSettings[i].value = event.data.value;
              ctx.flatGameSettings[i].dirty = !!event.data.dirty;
              ctx.flatGameSettings[i].supported = event.data.supported !== false;
              ctx.flatGameSettings[i].requiresRestart = !!event.data.requiresRestart;
              ctx.syncDisplayValue(ctx.flatGameSettings[i]);
              break;
            }
          }
          ctx.render();
        }
        var callback = ctx.pendingRequests[event.requestId];
        if (callback) {
          delete ctx.pendingRequests[event.requestId];
          callback(event);
        }
        if (!event.success && event.error) {
          console.warn("[CO Settings] Native error:", event.error.code, event.error.message);
          if (event.action === "settingsGetAll" && !ctx.flatGameSettings.length) {
            ctx.setGameStatus("Failed to load settings: " + (event.error.message || event.error.code), true);
          }
        }
      }
    };
  };
})();
