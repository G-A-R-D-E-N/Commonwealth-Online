(function () {
  "use strict";

  var TAB_IDS = ["game", "multiplayer"];
  var GAME_CATEGORY_ORDER = ["interface", "appearance", "controls", "save", "gameplay", "audio", "display"];
  var GAME_CATEGORY_LABELS = {
    interface: "INTERFACE",
    appearance: "HUD & PIP-BOY",
    controls: "CONTROLS",
    save: "SAVES",
    gameplay: "GAMEPLAY",
    audio: "AUDIO",
    display: "DISPLAY"
  };
  var RESOLUTION_PRESETS = [{
    width: 1280,
    height: 720
  }, {
    width: 1600,
    height: 900
  }, {
    width: 1920,
    height: 1080
  }, {
    width: 2560,
    height: 1440
  }, {
    width: 3840,
    height: 2160
  }];
  var COLOUR_PRESETS = [{
    r: 16,
    g: 247,
    b: 18
  }, {
    r: 18,
    g: 255,
    b: 140
  }, {
    r: 201,
    g: 168,
    b: 74
  }, {
    r: 62,
    g: 207,
    b: 74
  }, {
    r: 0,
    g: 255,
    b: 180
  }, {
    r: 255,
    g: 120,
    b: 40
  }, {
    r: 80,
    g: 180,
    b: 255
  }, {
    r: 255,
    g: 255,
    b: 255
  }];
  function deepClone(source) {
    return JSON.parse(JSON.stringify(source || null));
  }
  function deepCloneSettingsData(source) {
    return JSON.parse(JSON.stringify(source || []));
  }
  function flattenSettings(sections) {
    var flat = [];
    sections.forEach(function (section) {
      section.items.forEach(function (item) {
        flat.push(item);
      });
    });
    return flat;
  }
  function pad2(value) {
    var hex = Number(value).toString(16).toUpperCase();
    return hex.length === 1 ? "0" + hex : hex;
  }
  function colourToHex(colour) {
    if (!colour) {
      return "#000000";
    }
    return "#" + pad2(colour.r) + pad2(colour.g) + pad2(colour.b);
  }
  function valuesEqual(a, b) {
    return JSON.stringify(a) === JSON.stringify(b);
  }
  function buildMarkup(options) {
    var showHeading = options.showHeading !== false;
    var gameTabLabel = options.gameTabLabel || "FALLOUT 4";
    var multiplayerTabLabel = options.multiplayerTabLabel || "MULTIPLAYER";
    return '<div class="co-settings' + (options.layout === "standalone" ? " co-settings--standalone" : "") + '">' + '<div class="settings-panel">' + (showHeading ? '<h1 class="settings-panel__heading">SETTINGS</h1>' : "") + '<div class="settings-tabs" data-co-settings-tabs role="tablist" aria-label="Settings tabs">' + '<button type="button" class="settings-tab is-selected" role="tab" data-tab="game" aria-selected="true" aria-controls="gameSettingsPane">' + gameTabLabel + "</button>" + '<button type="button" class="settings-tab" role="tab" data-tab="multiplayer" aria-selected="false" aria-controls="multiplayerSettingsPane">' + multiplayerTabLabel + "</button>" + "</div>" + '<div class="settings-pane-stack">' + '<div class="settings-pane settings-pane--game" data-co-settings-pane="game" role="tabpanel">' + '<p class="settings-status" data-co-game-settings-status>Loading Fallout 4 settings…</p>' + '<div class="settings-rows" data-co-game-settings-rows aria-label="Fallout 4 setting options" role="listbox"></div>' + '<p class="settings-description" data-co-game-settings-description></p>' + '<div class="settings-actions" data-co-game-settings-actions>' + '<button type="button" class="settings-action-btn" data-co-settings-action="apply">APPLY</button>' + '<button type="button" class="settings-action-btn" data-co-settings-action="cancel">CANCEL</button>' + '<button type="button" class="settings-action-btn" data-co-settings-action="restore">RESTORE VALUES FROM MENU OPEN</button>' + "</div>" + "</div>" + '<div class="settings-pane settings-pane--multiplayer" data-co-settings-pane="multiplayer" role="tabpanel" hidden>' + '<div class="settings-rows" data-co-settings-rows aria-label="Multiplayer setting options" role="listbox"></div>' + '<p class="settings-description" data-co-settings-description></p>' + "</div>" + "</div>" + "</div>" + '<div class="settings-confirm" data-co-settings-confirm hidden>' + '<div class="settings-confirm__dialog" role="dialog" aria-modal="true" aria-labelledby="coSettingsConfirmTitle">' + '<h2 class="settings-confirm__title" id="coSettingsConfirmTitle">UNAPPLIED CHANGES</h2>' + '<p class="settings-confirm__body">Apply your Fallout 4 settings changes before leaving?</p>' + '<div class="settings-confirm__options">' + '<button type="button" class="settings-action-btn" data-co-confirm-choice="apply">APPLY</button>' + '<button type="button" class="settings-action-btn" data-co-confirm-choice="discard">DISCARD</button>' + '<button type="button" class="settings-action-btn" data-co-confirm-choice="stay">KEEP EDITING</button>' + "</div>" + "</div>" + "</div>" + "</div>";
  }
  function mapNativeSettingToRow(setting) {
    var row = {
      key: setting.id,
      label: setting.label || setting.id,
      description: setting.description || "",
      type: "toggle",
      value: setting.value,
      baselineValue: setting.defaultValue,
      supported: setting.supported !== false,
      dirty: !!setting.dirty,
      requiresRestart: !!setting.requiresRestart,
      editable: setting.editable !== false,
      applyTiming: setting.applyTiming || "immediate",
      options: null,
      min: setting.minimum,
      max: setting.maximum,
      step: setting.step,
      uiUsesPercent: !!setting.uiUsesPercent,
      nativeType: setting.type,
      category: setting.category,
      colourChannel: 0
    };
    if (!row.supported) {
      row.type = "unsupported";
      row.value = "Unavailable";
      return row;
    }
    if (setting.type === "boolean") {
      row.type = "toggle";
      return row;
    }
    if (setting.type === "colour") {
      row.type = "colour";
      return row;
    }
    if (setting.type === "resolution") {
      row.type = "resolution";
      row.options = RESOLUTION_PRESETS.slice();
      var current = setting.value || {};
      var exists = row.options.some(function (preset) {
        return preset.width === current.width && preset.height === current.height;
      });
      if (!exists && current.width && current.height) {
        row.options.unshift({
          width: current.width,
          height: current.height
        });
      }
      return row;
    }
    if (setting.type === "windowMode" || setting.options && setting.options.length) {
      row.type = "select";
      row.options = (setting.options || []).map(function (option) {
        return {
          value: option.value,
          label: option.label
        };
      });
      return row;
    }
    if (setting.type === "float" || setting.type === "integer" || setting.type === "unsignedInteger") {
      row.type = "slider";
      if (row.uiUsesPercent && setting.type === "float" && row.max != null && row.max <= 1) {
        row.displayMin = 0;
        row.displayMax = 100;
        row.displayStep = Math.max(1, Math.round((row.step || 0.05) * 100));
        row.displayValue = Math.round((Number(row.value) || 0) * 100);
      } else if (row.uiUsesPercent) {
        row.displayMin = row.min != null ? row.min : 0;
        row.displayMax = row.max != null ? row.max : 100;
        row.displayStep = row.step != null ? row.step : 1;
        row.displayValue = Number(row.value) || 0;
      } else {
        row.displayMin = row.min != null ? row.min : 0;
        row.displayMax = row.max != null ? row.max : 100;
        row.displayStep = row.step != null ? row.step : 1;
        row.displayValue = Number(row.value) || 0;
      }
      return row;
    }
    if (setting.type === "string") {
      row.type = "select";
      row.options = (setting.options || []).map(function (option) {
        return {
          value: option.value,
          label: option.label
        };
      });
      return row;
    }
    row.type = "unsupported";
    row.value = "Unsupported";
    row.supported = false;
    return row;
  }
  function groupGameSettings(settings) {
    var byCategory = {};
    GAME_CATEGORY_ORDER.forEach(function (category) {
      byCategory[category] = [];
    });
    (settings || []).forEach(function (setting) {
      if (setting.showInUI === false) {
        return;
      }
      var category = setting.category || "interface";
      if (!byCategory[category]) {
        byCategory[category] = [];
      }
      byCategory[category].push(mapNativeSettingToRow(setting));
    });
    var sections = [];
    GAME_CATEGORY_ORDER.forEach(function (category) {
      if (!byCategory[category] || !byCategory[category].length) {
        return;
      }
      sections.push({
        section: GAME_CATEGORY_LABELS[category] || category.toUpperCase(),
        category: category,
        items: byCategory[category]
      });
    });
    return sections;
  }
  window.CO_SettingsModel = {
    TAB_IDS,
    GAME_CATEGORY_ORDER,
    GAME_CATEGORY_LABELS,
    RESOLUTION_PRESETS,
    COLOUR_PRESETS,
    deepClone,
    deepCloneSettingsData,
    flattenSettings,
    pad2,
    colourToHex,
    valuesEqual,
    buildMarkup,
    mapNativeSettingToRow,
    groupGameSettings
  };
})();
