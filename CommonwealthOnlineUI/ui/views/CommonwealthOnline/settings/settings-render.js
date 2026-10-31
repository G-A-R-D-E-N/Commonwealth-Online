(function () {
  "use strict";

  window.CO_SettingsRender = function (ctx, model) {
    ctx.formatSettingValue = function (setting) {
      if (setting.type === "unsupported") {
        return "Unavailable";
      }
      if (setting.type === "toggle") {
        return setting.value ? "ON" : "OFF";
      }
      if (setting.type === "slider") {
        var display = setting.displayValue != null ? setting.displayValue : setting.value;
        if (setting.uiUsesPercent) {
          return Math.round(display) + "%";
        }
        return String(display);
      }
      if (setting.type === "colour") {
        return model.colourToHex(setting.value);
      }
      if (setting.type === "resolution") {
        var res = setting.value || {};
        return (res.width || "?") + " x " + (res.height || "?");
      }
      if (setting.type === "select") {
        var optionsList = setting.options || [];
        for (var i = 0; i < optionsList.length; i += 1) {
          var option = optionsList[i];
          var optionValue = option && Object.prototype.hasOwnProperty.call(option, "value") ? option.value : option;
          if (model.valuesEqual(optionValue, setting.value)) {
            return option.label || String(optionValue);
          }
        }
        return String(setting.value);
      }
      if (setting.type === "keybind") {
        return setting.value ? String(setting.value) : "Unbound";
      }
      return String(setting.value);
    };
    ctx.renderTabs = function () {
      if (!ctx.settingsTabsEl) {
        return;
      }
      ctx.tabButtons.forEach(function (tab) {
        var tabId = tab.getAttribute("data-tab");
        var isSelected = tabId === ctx.activeTab;
        tab.classList.toggle("is-selected", isSelected);
        tab.setAttribute("aria-selected", isSelected ? "true" : "false");
        tab.tabIndex = isSelected ? 0 : -1;
      });
      ctx.settingsTabsEl.classList.toggle("is-focus-region", ctx.settingsFocus === "tabs");
    };
    ctx.buildValueMarkup = function (setting) {
      if (setting.type === "slider") {
        var min = setting.displayMin != null ? setting.displayMin : setting.min != null ? setting.min : 0;
        var max = setting.displayMax != null ? setting.displayMax : setting.max != null ? setting.max : 100;
        var display = setting.displayValue != null ? setting.displayValue : setting.value;
        var step = setting.displayStep != null ? setting.displayStep : setting.step != null ? setting.step : 1;
        return '<span class="settings-slider"><input class="settings-range" type="range" min="' + min + '" max="' + max + '" step="' + step + '" value="' + display + '">' + '<span class="settings-slider__pct">' + ctx.formatSettingValue(setting) + '</span></span>';
      }
      if (setting.type === "colour") {
        return '<span class="settings-colour">' + '<span class="settings-colour__swatch" style="background:' + model.colourToHex(setting.value) + '"></span>' + '<span class="settings-cycler">' + '<span class="settings-cycler__arrow settings-cycler__arrow--prev" aria-hidden="true">&lt;</span>' + '<span class="settings-row__value-text"></span>' + '<span class="settings-cycler__arrow" aria-hidden="true">&gt;</span>' + "</span>" + "</span>";
      }
      var usesCycler = setting.type === "toggle" || setting.type === "select" || setting.type === "resolution";
      if (usesCycler) {
        return '<span class="settings-cycler">' + '<span class="settings-cycler__arrow settings-cycler__arrow--prev" aria-hidden="true">&lt;</span>' + '<span class="settings-row__value-text"></span>' + '<span class="settings-cycler__arrow" aria-hidden="true">&gt;</span>' + "</span>";
      }
      return '<span class="settings-row__value-text"></span>';
    };
    ctx.renderSectionRows = function (targetEl, sections, selectedIndex, focusContent) {
      if (!targetEl) {
        return;
      }
      var scrollTop = targetEl.scrollTop;
      var focused = document.activeElement;
      var typing = focused && focused.classList && focused.classList.contains("settings-text-input") && targetEl.contains(focused) ? focused : null;
      var typedKey = typing ? typing.closest(".settings-row").getAttribute("data-setting-key") : "";
      var typedValue = typing ? typing.value : "";
      targetEl.innerHTML = "";
      var flatIndex = 0;
      sections.forEach(function (section) {
        var sectionEl = document.createElement("div");
        sectionEl.className = "settings-section";
        var heading = document.createElement("h3");
        heading.className = "settings-section__heading";
        heading.textContent = section.section;
        sectionEl.appendChild(heading);
        section.items.forEach(function (setting) {
          var index = flatIndex;
          flatIndex += 1;
          var isText = setting.type === "text" && setting.editable !== false;
          var isSlider = setting.type === "slider";
          var button = document.createElement(isText || isSlider ? "div" : "button");
          button.type = "button";
          button.className = "settings-row";
          button.setAttribute("role", "option");
          button.setAttribute("data-setting-index", String(index));
          button.setAttribute("data-setting-key", setting.key);
          if (index === selectedIndex) {
            button.classList.add("is-selected");
          }
          if (setting.type === "unsupported" || setting.editable === false) {
            button.classList.add("is-readonly");
          }
          if (setting.supported === false || setting.type === "unsupported") {
            button.classList.add("is-unsupported");
          }
          if (setting.dirty) {
            button.classList.add("is-dirty");
          }
          if (setting.requiresRestart) {
            button.classList.add("is-restart");
          }
          button.innerHTML = '<span class="settings-row__label"></span>' + '<span class="settings-row__meta">' + (setting.requiresRestart ? '<span class="settings-badge settings-badge--restart">RESTART</span>' : "") + (setting.dirty ? '<span class="settings-badge settings-badge--dirty">CHANGED</span>' : "") + "</span>" + '<span class="settings-row__value">' + ctx.buildValueMarkup(setting) + "</span>" + '<span class="settings-row__bar" aria-hidden="true"></span>';
          button.querySelector(".settings-row__label").textContent = setting.label;
          var valueText = button.querySelector(".settings-row__value-text");
          if (valueText) {
            valueText.textContent = ctx.formatSettingValue(setting);
          }
          if (isSlider) {
            var range = button.querySelector('.settings-range');
            range.setAttribute('aria-label', setting.label);
            range.disabled = setting.supported === false || setting.editable === false;
            range.addEventListener('input', function () {
              ctx.settingIndex = index;
              ctx.settingsFocus = 'content';
              button.querySelector('.settings-slider__pct').textContent = range.value + (setting.uiUsesPercent ? '%' : '');
              ctx.updateSelection();
            });
            range.addEventListener('change', function () {
              var value = Number(range.value);
              if (ctx.activeTab === 'game') {
                if (setting.uiUsesPercent && setting.nativeType === 'float' && setting.max != null && setting.max <= 1) value /= 100;
                ctx.setPendingValue(setting, value);
              } else {
                ctx.setMultiplayerValue(setting, value);
                ctx.render();
              }
            });
          }
          if (isText) {
            var valueEl = button.querySelector(".settings-row__value");
            valueEl.textContent = "";
            var input = document.createElement("input");
            input.type = "text";
            input.className = "settings-text-input";
            input.maxLength = 128;
            input.value = setting.value || "";
            input.placeholder = "Use character name";
            input.setAttribute("aria-label", setting.label);
            input.addEventListener("change", function () {
              var value = input.value.trim();
              if (value === setting.value) {
                if (ctx.profileError) {
                  ctx.profileError = "";
                  ctx.renderDescription();
                }
                return;
              }
              var byteLength;
              try { byteLength = encodeURIComponent(value).replace(/%[0-9A-F]{2}/g, "x").length; } catch (_) { byteLength = -1; }
              if (byteLength < 0 || byteLength > 128) {
                ctx.profileError = byteLength < 0 ? "Display name contains invalid characters." : "Display name must be 128 bytes or fewer in UTF-8.";
                ctx.renderDescription();
                return;
              }
              ctx.sendSettingsAction("settingsSetDisplayName", {
                value: value
              }, function (result) {
                ctx.profileError = "";
                if (result && result.success && result.data) {
                  setting.value = result.data.displayName;
                  input.value = setting.value;
                } else {
                  ctx.profileError = result && result.error ? result.error.message : "Display name could not be saved.";
                }
                ctx.renderDescription();
              });
            });
            input.addEventListener("keydown", function (event) {
              if (event.key === "Escape" || event.key === "Enter") {
                event.preventDefault();
                event.stopPropagation();
                if (event.key === "Escape") input.value = setting.value || "";
                input.blur();
              }
            });
            valueEl.appendChild(input);
          }
          button.addEventListener("click", function (event) {
            ctx.settingIndex = index;
            ctx.settingsFocus = "content";
            if (isText) {
              input.focus();
              return;
            }
            if (isSlider) { ctx.updateSelection(); return; }
            if (event.target && event.target.classList && event.target.classList.contains("settings-cycler__arrow--prev")) {
              ctx.adjustSetting(-1);
            } else {
              ctx.activateSetting();
            }
            ctx.render();
          });
          sectionEl.appendChild(button);
        });
        targetEl.appendChild(sectionEl);
      });
      if (typing) {
        var kept = targetEl.querySelector('[data-setting-key="' + typedKey + '"]');
        var keptInput = kept && kept.querySelector(".settings-text-input");
        if (keptInput) {
          keptInput.value = typedValue;
          keptInput.focus();
        }
      }
      targetEl.classList.toggle("is-focus-region", focusContent);
      targetEl.scrollTop = scrollTop;
    };
    ctx.renderDescription = function () {
      var setting = ctx.getCurrentSetting();
      var text = setting && setting.description ? setting.description : "";
      if (ctx.activeTab === "multiplayer" && ctx.profileError) text = ctx.profileError;
      if (setting && setting.requiresRestart) {
        text += (text ? " " : "") + "Requires a game restart to take full effect.";
      }
      if (setting && setting.applyTiming === "requiresReverseEngineering") {
        text += (text ? " " : "") + "Value is saved; live apply may require additional engine work.";
      }
      if (ctx.activeTab === "game") {
        if (ctx.gameDescriptionEl) {
          ctx.gameDescriptionEl.textContent = text;
        }
      } else if (ctx.settingsDescriptionEl) {
        ctx.settingsDescriptionEl.textContent = text;
      }
    };
    ctx.renderActions = function () {
      if (!ctx.gameActionsEl) {
        return;
      }
      var dirty = ctx.hasDirtyGameSettings();
      ctx.gameActionsEl.classList.toggle("is-focus-region", ctx.settingsFocus === "actions" && ctx.activeTab === "game");
      ctx.actionButtons.forEach(function (button, index) {
        var actionName = button.getAttribute("data-co-settings-action");
        button.classList.toggle("is-focused", ctx.settingsFocus === "actions" && index === ctx.actionIndex);
        button.classList.toggle("is-idle", (actionName === "apply" || actionName === "cancel") && !dirty);
      });
      ctx.confirmButtons.forEach(function (button, index) {
        button.classList.toggle("is-focused", ctx.confirmOpen && index === ctx.confirmIndex);
      });
      if (ctx.confirmEl) {
        ctx.confirmEl.hidden = !ctx.confirmOpen;
      }
    };
    ctx.renderPanes = function () {
      if (ctx.gameSettingsPane) {
        ctx.gameSettingsPane.hidden = ctx.activeTab !== "game";
        ctx.gameSettingsPane.classList.toggle("is-focus-region", ctx.settingsFocus === "content" && ctx.activeTab === "game");
      }
      if (ctx.multiplayerSettingsPane) {
        ctx.multiplayerSettingsPane.hidden = ctx.activeTab !== "multiplayer";
      }
    };
    ctx.updateSelection = function () {
      ctx.renderTabs();
      ctx.renderPanes();
      ctx.renderActions();
      [[ctx.gameRowsEl, "game"], [ctx.settingsRowsEl, "multiplayer"]].forEach(function (target) {
        var rowsEl = target[0];
        var tabId = target[1];
        if (!rowsEl) {
          return;
        }
        var selectedIndex = ctx.activeTab === tabId ? ctx.settingIndex : -1;
        rowsEl.querySelectorAll(".settings-row").forEach(function (row) {
          row.classList.toggle("is-selected", Number(row.getAttribute("data-setting-index")) === selectedIndex);
        });
        rowsEl.classList.toggle("is-focus-region", ctx.settingsFocus === "content" && ctx.activeTab === tabId);
      });
      ctx.renderDescription();
    };
    ctx.render = function () {
      ctx.renderTabs();
      ctx.renderPanes();
      ctx.renderSectionRows(ctx.gameRowsEl, ctx.gameSettings, ctx.activeTab === "game" ? ctx.settingIndex : -1, ctx.settingsFocus === "content" && ctx.activeTab === "game");
      ctx.renderSectionRows(ctx.settingsRowsEl, ctx.multiplayerSettings, ctx.activeTab === "multiplayer" ? ctx.settingIndex : -1, ctx.settingsFocus === "content" && ctx.activeTab === "multiplayer");
      ctx.renderDescription();
      ctx.renderActions();
    };
  };
})();
