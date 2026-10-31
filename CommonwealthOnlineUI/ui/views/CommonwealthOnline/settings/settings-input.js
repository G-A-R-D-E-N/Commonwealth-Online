(function () {
  "use strict";

  window.CO_SettingsInput = function (ctx, model) {
    ctx.handleKeydown = function (event) {
      if (event.target && event.target.classList && event.target.classList.contains("settings-text-input")) return false;
      if (!ctx.visible) {
        return false;
      }
      if (ctx.confirmOpen) {
        switch (event.key) {
          case "ArrowLeft":
          case "ArrowUp":
            event.preventDefault();
            ctx.confirmIndex = (ctx.confirmIndex + ctx.confirmButtons.length - 1) % ctx.confirmButtons.length;
            ctx.render();
            return true;
          case "ArrowRight":
          case "ArrowDown":
            event.preventDefault();
            ctx.confirmIndex = (ctx.confirmIndex + 1) % ctx.confirmButtons.length;
            ctx.render();
            return true;
          case "Enter":
          case " ":
            event.preventDefault();
            ctx.resolveConfirm(ctx.confirmButtons[ctx.confirmIndex].getAttribute("data-co-confirm-choice"));
            return true;
          case "Escape":
            event.preventDefault();
            ctx.resolveConfirm("stay");
            return true;
          default:
            return true;
        }
      }
      switch (event.key) {
        case "ArrowUp":
          event.preventDefault();
          if (ctx.settingsFocus === "actions") {
            ctx.setSettingsFocus("content");
          } else if (ctx.settingsFocus === "content") {
            ctx.moveSettingRow(-1);
          }
          return true;
        case "ArrowDown":
          event.preventDefault();
          if (ctx.settingsFocus === "content" && ctx.activeTab === "game" && ctx.settingIndex >= ctx.getActiveFlat().length - 1) {
            ctx.setSettingsFocus("actions");
          } else if (ctx.settingsFocus === "content") {
            ctx.moveSettingRow(1);
          }
          return true;
        case "ArrowLeft":
          event.preventDefault();
          if (ctx.settingsFocus === "tabs") {
            ctx.moveTab(-1);
          } else if (ctx.settingsFocus === "actions") {
            ctx.moveAction(-1);
          } else {
            ctx.adjustSetting(-1);
          }
          return true;
        case "ArrowRight":
          event.preventDefault();
          if (ctx.settingsFocus === "tabs") {
            ctx.moveTab(1);
          } else if (ctx.settingsFocus === "actions") {
            ctx.moveAction(1);
          } else {
            ctx.adjustSetting(1);
          }
          return true;
        case "Tab":
          event.preventDefault();
          if (ctx.activeTab === "game") {
            if (ctx.settingsFocus === "tabs") {
              ctx.setSettingsFocus("content");
            } else if (ctx.settingsFocus === "content") {
              ctx.setSettingsFocus("actions");
            } else {
              ctx.setSettingsFocus("tabs");
            }
          } else {
            ctx.setSettingsFocus(ctx.settingsFocus === "tabs" ? "content" : "tabs");
          }
          return true;
        case "Enter":
        case " ":
          event.preventDefault();
          if (ctx.settingsFocus === "tabs") {
            ctx.setSettingsFocus("content");
          } else if (ctx.settingsFocus === "actions") {
            ctx.runAction(ctx.actionButtons[ctx.actionIndex].getAttribute("data-co-settings-action"));
          } else {
            ctx.activateSetting();
          }
          return true;
        case "Escape":
          event.preventDefault();
          ctx.requestLeave();
          return true;
        default:
          return false;
      }
    };
    ctx.handleControllerInput = function (button, phase) {
      if (!ctx.visible || phase !== "down") {
        return false;
      }
      var now = Date.now();
      if (now < ctx.controllerReadyAt) {
        return true;
      }
      if (ctx.controllerButtonTimes[button] && now - ctx.controllerButtonTimes[button] < ctx.CONTROLLER_DEBOUNCE_MS) {
        return true;
      }
      ctx.controllerButtonTimes[button] = now;
      if (ctx.confirmOpen) {
        if (button === "dpad_left" || button === "dpad_up") {
          ctx.confirmIndex = (ctx.confirmIndex + ctx.confirmButtons.length - 1) % ctx.confirmButtons.length;
          ctx.render();
          return true;
        }
        if (button === "dpad_right" || button === "dpad_down") {
          ctx.confirmIndex = (ctx.confirmIndex + 1) % ctx.confirmButtons.length;
          ctx.render();
          return true;
        }
        if (button === "a") {
          ctx.resolveConfirm(ctx.confirmButtons[ctx.confirmIndex].getAttribute("data-co-confirm-choice"));
          return true;
        }
        if (button === "b") {
          ctx.resolveConfirm(ctx.pendingLeave ? "discard" : "stay");
          return true;
        }
        return true;
      }
      switch (button) {
        case "dpad_up":
          if (ctx.settingsFocus === "actions") {
            ctx.setSettingsFocus("content");
          } else if (ctx.settingsFocus === "content") {
            ctx.moveSettingRow(-1);
          } else if (ctx.settingsFocus === "tabs") {
            ctx.setSettingsFocus("content");
          }
          return true;
        case "dpad_down":
          if (ctx.settingsFocus === "tabs") {
            ctx.setSettingsFocus("content");
          } else if (ctx.settingsFocus === "content" && ctx.activeTab === "game" && ctx.settingIndex >= ctx.getActiveFlat().length - 1) {
            ctx.setSettingsFocus("actions");
          } else if (ctx.settingsFocus === "content") {
            ctx.moveSettingRow(1);
          }
          return true;
        case "dpad_left":
          if (ctx.settingsFocus === "tabs") {
            ctx.moveTab(-1);
          } else if (ctx.settingsFocus === "actions") {
            ctx.moveAction(-1);
          } else {
            ctx.adjustSetting(-1);
          }
          return true;
        case "dpad_right":
          if (ctx.settingsFocus === "tabs") {
            ctx.moveTab(1);
          } else if (ctx.settingsFocus === "actions") {
            ctx.moveAction(1);
          } else {
            ctx.adjustSetting(1);
          }
          return true;
        case "y":
          if (ctx.activeTab === "game") {
            if (ctx.settingsFocus === "tabs") {
              ctx.setSettingsFocus("content");
            } else if (ctx.settingsFocus === "content") {
              ctx.setSettingsFocus("actions");
            } else {
              ctx.setSettingsFocus("tabs");
            }
          } else {
            ctx.setSettingsFocus(ctx.settingsFocus === "tabs" ? "content" : "tabs");
          }
          return true;
        case "lb":
          ctx.moveTab(-1);
          return true;
        case "rb":
          ctx.moveTab(1);
          return true;
        case "x":
          if (ctx.activeTab === "game") {
            ctx.applyGameSettings();
          }
          return true;
        case "a":
          if (ctx.settingsFocus === "tabs") {
            ctx.setSettingsFocus("content");
          } else if (ctx.settingsFocus === "actions") {
            ctx.runAction(ctx.actionButtons[ctx.actionIndex].getAttribute("data-co-settings-action"));
          } else {
            ctx.activateSetting();
          }
          return true;
        case "b":
          ctx.discardAndLeave();
          return true;
        default:
          return false;
      }
    };

    ctx.adjustSetting = function (delta) {
      var setting = ctx.getCurrentSetting();
      if (!setting) {
        return;
      }
      if (ctx.activeTab === "multiplayer") {
        if (setting.type === "text" || setting.type === "keybind") {
          return;
        }
        if (setting.type === "toggle") {
          ctx.setMultiplayerValue(setting, !setting.value);
          ctx.render();
          return;
        }
        if (setting.type === "select") {
          var mpOptions = setting.options || [];
          if (!mpOptions.length) {
            return;
          }
          var mpCurrent = mpOptions.indexOf(setting.value);
          if (mpCurrent < 0) {
            mpCurrent = 0;
          }
          var mpNext = mpCurrent + (delta === 0 ? 1 : delta);
          if (mpNext < 0) {
            mpNext = mpOptions.length - 1;
          } else if (mpNext >= mpOptions.length) {
            mpNext = 0;
          }
          ctx.setMultiplayerValue(setting, mpOptions[mpNext]);
          ctx.render();
          return;
        }
        if (setting.type === "slider") {
          var mpStep = setting.step != null ? setting.step : 5;
          var mpMin = setting.min != null ? setting.min : 0;
          var mpMax = setting.max != null ? setting.max : 100;
          var mpAmount = delta === 0 ? mpStep : delta * mpStep;
          var mpNextValue = Math.max(mpMin, Math.min(mpMax, setting.value + mpAmount));
          if (mpNextValue === setting.value) {
            return;
          }
          ctx.setMultiplayerValue(setting, mpNextValue);
          ctx.render();
        }
        return;
      }
      if (!setting.supported || setting.type === "unsupported" || setting.editable === false) {
        return;
      }
      if (setting.type === "toggle") {
        ctx.setPendingValue(setting, !setting.value);
        return;
      }
      if (setting.type === "select") {
        var optionsList = setting.options || [];
        if (!optionsList.length) {
          return;
        }
        var current = -1;
        for (var i = 0; i < optionsList.length; i += 1) {
          var optionValue = Object.prototype.hasOwnProperty.call(optionsList[i], "value") ? optionsList[i].value : optionsList[i];
          if (model.valuesEqual(optionValue, setting.value)) {
            current = i;
            break;
          }
        }
        if (current < 0) {
          current = 0;
        }
        var next = current + (delta === 0 ? 1 : delta);
        if (next < 0) {
          next = optionsList.length - 1;
        } else if (next >= optionsList.length) {
          next = 0;
        }
        var selected = optionsList[next];
        ctx.setPendingValue(setting, Object.prototype.hasOwnProperty.call(selected, "value") ? selected.value : selected);
        return;
      }
      if (setting.type === "resolution") {
        var presets = setting.options || model.RESOLUTION_PRESETS;
        var resIndex = 0;
        for (var r = 0; r < presets.length; r += 1) {
          if (setting.value && presets[r].width === setting.value.width && presets[r].height === setting.value.height) {
            resIndex = r;
            break;
          }
        }
        var resNext = resIndex + (delta === 0 ? 1 : delta);
        if (resNext < 0) {
          resNext = presets.length - 1;
        } else if (resNext >= presets.length) {
          resNext = 0;
        }
        ctx.setPendingValue(setting, model.deepClone(presets[resNext]));
        return;
      }
      if (setting.type === "colour") {
        if (delta === 0) {
          ctx.adjustColour(setting, 0);
        } else {
          ctx.cycleColourPreset(setting, delta);
        }
        return;
      }
      if (setting.type === "slider") {
        var step = setting.displayStep != null ? setting.displayStep : 1;
        var min = setting.displayMin != null ? setting.displayMin : 0;
        var max = setting.displayMax != null ? setting.displayMax : 100;
        var amount = delta === 0 ? step : delta * step;
        var nextDisplay = Math.max(min, Math.min(max, (setting.displayValue != null ? setting.displayValue : 0) + amount));
        if (nextDisplay === setting.displayValue) {
          return;
        }
        var nextValue = nextDisplay;
        if (setting.uiUsesPercent && setting.nativeType === "float" && setting.max != null && setting.max <= 1) {
          nextValue = nextDisplay / 100;
        }
        ctx.setPendingValue(setting, nextValue);
      }
    };
  };
})();
