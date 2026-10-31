(function () {
  "use strict";
  var mainScriptStarted = Date.now();
  function sendCoAction(payload) {
    var msg = JSON.stringify(payload);
    if (typeof window.coAction === "function") {
      window.coAction(msg);
      return true;
    }
    console.warn("[CO Main Menu] coAction bridge unavailable:", payload);
    return false;
  }
  window.CommonwealthOnlineUI = {
    openGameSettings: function () {
      console.log("[CO UI] Legacy openGameSettings ignored — use native Fallout 4 settings tab");
    },
    setMultiplayerSetting: function (key, value) {
      if (window.CO_VoiceSettings.forwardSetting(key, value)) {
        return true;
      }
      console.log("[CO UI] Multiplayer setting changed: " + key + " = " + value);
    },
    sendSettingsAction: function (payload) {
      return sendCoAction(payload);
    },
    onBack: function () {
      console.log("[CO UI] Back requested from main menu");
    },
    onOpenView: function (viewName) {
      console.log("[CO UI] Open view:", viewName);
    }
  };
  window.CO_App = {};
  // Floor at 1 so opening Multiplayer / Settings does not jump scale.
  function updateScale() {
    if (window.CO_Layout && typeof CO_Layout.update === "function") {
      CO_Layout.update();
      return;
    }
    var REF_W = 1920;
    var REF_H = 1080;
    var w = window.innerWidth || REF_W;
    var h = window.innerHeight || REF_H;
    var scale = Math.max(1, Math.min(w / REF_W, h / REF_H));
    document.documentElement.style.setProperty("--co-ui-scale", String(scale));
  }
  updateScale();
  window.addEventListener("resize", updateScale);
  var GLYPH_WHITE_THRESHOLD = 200;
  var GLYPH_BLACK_THRESHOLD = 40;
  var glyphTintCache = {};
  function getAmberTint() {
    var style = getComputedStyle(document.documentElement);
    var amber = style.getPropertyValue("--co-amber").trim() || "#c9a84a";
    var hex = amber.replace("#", "");
    if (hex.length === 3) {
      hex = hex[0] + hex[0] + hex[1] + hex[1] + hex[2] + hex[2];
    }
    return {
      r: parseInt(hex.slice(0, 2), 16) || 201,
      g: parseInt(hex.slice(2, 4), 16) || 168,
      b: parseInt(hex.slice(4, 6), 16) || 74
    };
  }
  function classifyGlyphPixel(r, g, b) {
    if (r >= GLYPH_WHITE_THRESHOLD && g >= GLYPH_WHITE_THRESHOLD && b >= GLYPH_WHITE_THRESHOLD) {
      return "white";
    }
    if (r <= GLYPH_BLACK_THRESHOLD && g <= GLYPH_BLACK_THRESHOLD && b <= GLYPH_BLACK_THRESHOLD) {
      return "black";
    }
    return "edge";
  }
  function tintGlyphImageData(imageData, color) {
    var data = imageData.data;
    for (var i = 0; i < data.length; i += 4) {
      if (data[i + 3] < 4) {
        data[i + 3] = 0;
        continue;
      }
      var kind = classifyGlyphPixel(data[i], data[i + 1], data[i + 2]);
      if (kind === "white") {
        data[i] = color.r;
        data[i + 1] = color.g;
        data[i + 2] = color.b;
      } else if (kind === "black") {
        data[i] = 0;
        data[i + 1] = 0;
        data[i + 2] = 0;
      } else {
        var lum = (data[i] + data[i + 1] + data[i + 2]) / (3 * 255);
        data[i] = Math.round(lum * color.r);
        data[i + 1] = Math.round(lum * color.g);
        data[i + 2] = Math.round(lum * color.b);
      }
    }
    return imageData;
  }
  function tintGlyphImage(url, color, done) {
    var key = url + "|" + color.r + "," + color.g + "," + color.b;
    if (glyphTintCache[key]) {
      done(glyphTintCache[key]);
      return;
    }
    var img = new Image();
    img.onload = function () {
      var canvas = document.createElement("canvas");
      canvas.width = img.naturalWidth;
      canvas.height = img.naturalHeight;
      var ctx = canvas.getContext("2d");
      if (!ctx) {
        done(null);
        return;
      }
      ctx.drawImage(img, 0, 0);
      try {
        ctx.putImageData(
          tintGlyphImageData(ctx.getImageData(0, 0, canvas.width, canvas.height), color),
          0,
          0
        );
      } catch (err) {
        done(null);
        return;
      }
      var dataUrl = canvas.toDataURL("image/png");
      glyphTintCache[key] = dataUrl;
      done(dataUrl);
    };
    img.onerror = function () {
      done(null);
    };
    img.src = url;
  }
  function hydratePromptGlyphs() {
    var color = getAmberTint();
    var imgs = document.querySelectorAll(".prompt__glyph-img[data-glyph-src]");
    for (var i = 0; i < imgs.length; i++) {
      (function (img) {
        var src = img.getAttribute("data-glyph-src");
        if (!src) {
          return;
        }
        tintGlyphImage(src, color, function (dataUrl) {
          if (dataUrl) {
            img.src = dataUrl;
          }
        });
      })(imgs[i]);
    }
  }
  hydratePromptGlyphs();
  var mainScreen = document.getElementById("mainScreen");
  var menu = document.getElementById("menu");
  var settingsPanel = document.getElementById("settingsPanel");
  var browserPanel = document.getElementById("browserPanel");
  var quitConfirmPanel = document.getElementById("quitConfirmPanel");
  var quitConfirmOptions = document.getElementById("quitConfirmOptions");
  var mainPromptBar = document.getElementById("mainPromptBar");
  var settingsPromptBar = document.getElementById("settingsPromptBar");
  if (!menu || !settingsPanel || !browserPanel || !quitConfirmPanel || !quitConfirmOptions ||
      !window.CO_Settings || !window.CO_Browser) {
    return;
  }
  var settingsController = null;
  var browserController = null;
  var items = Array.prototype.slice.call(menu.querySelectorAll(".menu-item"));
  var quitConfirmItems = Array.prototype.slice.call(
    quitConfirmOptions.querySelectorAll(".quit-confirm__option")
  );
  // These are positions in the live `items` array, not the sparse data-index
  // values left over from the commented-out menu rows in index.html.
  var MULTIPLAYER_INDEX = items.findIndex(function (item) {
    return item.getAttribute("data-action") === "multiplayer";
  });
  var SETTINGS_INDEX = items.findIndex(function (item) {
    return item.getAttribute("data-action") === "settings";
  });
  var QUIT_INDEX = items.findIndex(function (item) {
    return item.getAttribute("data-action") === "quit";
  });
  // viewMode: "menu" | "settings" | "browser" | "quitConfirm"
  var viewMode = "menu";
  var selectedIndex = -1;
  var quitConfirmSelectedIndex = 0;
  var PANEL_TRANSITION_MS = 220;
  var settingsHideFallbackId = 0;
  var settingsHideOnTransitionEnd = null;
  var browserHideFallbackId = 0;
  var browserHideOnTransitionEnd = null;
  function setSelected(index) {
    if (index >= items.length) {
      return;
    }
    selectedIndex = index < 0 ? -1 : index;
    items.forEach(function (item, i) {
      var isSelected = i === selectedIndex;
      item.classList.toggle("is-selected", isSelected);
      item.setAttribute("aria-selected", isSelected ? "true" : "false");
      item.tabIndex = isSelected ? 0 : -1;
    });
  }
  function moveSelection(delta) {
    var next = selectedIndex + delta;
    if (selectedIndex < 0) {
      next = delta > 0 ? 0 : items.length - 1;
    } else if (next < 0) {
      next = items.length - 1;
    } else if (next >= items.length) {
      next = 0;
    }
    // Skip disabled items
    var guard = 0;
    while (items[next] && items[next].classList.contains("is-disabled") && guard < items.length) {
      next += delta > 0 ? 1 : -1;
      if (next < 0) {
        next = items.length - 1;
      } else if (next >= items.length) {
        next = 0;
      }
      guard += 1;
    }
    setSelected(next);
  }
  function activateSelected() {
    var item = items[selectedIndex];
    if (!item || item.classList.contains("is-disabled")) {
      return;
    }
    var action = item.getAttribute("data-action");
    console.log("[CO Main Menu] Selected:", action);
    if (action === "settings") {
      enterSettingsMode();
    } else if (action === "multiplayer") {
      enterBrowserMode();
    } else if (action === "singlePlayer") {
      sendCoAction({ action: "singlePlayer" });
    } else if (action === "quit") {
      enterQuitConfirmMode();
    } else {
      window.CommonwealthOnlineUI.onOpenView(action);
      setSelected(-1);
    }
  }
  items.forEach(function (item, index) {
    item.addEventListener("click", function () {
      if (viewMode === "settings") {
        if (index === SETTINGS_INDEX) {
          return;
        }
        exitSettingsMode();
      } else if (viewMode === "browser") {
        if (index === MULTIPLAYER_INDEX) {
          return;
        }
        exitBrowserMode();
      } else if (viewMode === "quitConfirm") {
        if (index === QUIT_INDEX) {
          return;
        }
        exitQuitConfirmMode();
      }
      if (item.classList.contains("is-disabled")) {
        return;
      }
      setSelected(index);
      activateSelected();
    });
  });
  function cancelSettingsPanelHide() {
    if (settingsHideFallbackId) {
      window.clearTimeout(settingsHideFallbackId);
      settingsHideFallbackId = 0;
    }
    if (settingsHideOnTransitionEnd) {
      settingsPanel.removeEventListener("transitionend", settingsHideOnTransitionEnd);
      settingsHideOnTransitionEnd = null;
    }
  }
  function showSettingsPanelAnimated() {
    cancelSettingsPanelHide();
    if (mainScreen) {
      mainScreen.classList.add("is-settings-mode");
    }
    settingsPanel.hidden = false;
    settingsPanel.classList.remove("is-open");
    void settingsPanel.offsetWidth;
    settingsPanel.classList.add("is-open");
  }
  function hideSettingsPanelAnimated(done) {
    if (settingsPanel.hidden) {
      if (done) {
        done();
      }
      return;
    }
    cancelSettingsPanelHide();
    settingsPanel.classList.remove("is-open");
    var completed = false;
    function finishHide() {
      if (completed) {
        return;
      }
      completed = true;
      cancelSettingsPanelHide();
      if (viewMode === "settings" || settingsPanel.classList.contains("is-open")) {
        if (done) {
          done();
        }
        return;
      }
      if (mainScreen) {
        mainScreen.classList.remove("is-settings-mode");
      }
      settingsPanel.hidden = true;
      if (done) {
        done();
      }
    }
    settingsHideOnTransitionEnd = function (event) {
      if (event.target !== settingsPanel || event.propertyName !== "opacity") {
        return;
      }
      finishHide();
    };
    settingsPanel.addEventListener("transitionend", settingsHideOnTransitionEnd);
    settingsHideFallbackId = window.setTimeout(finishHide, PANEL_TRANSITION_MS + 80);
  }
  function cancelBrowserPanelHide() {
    if (browserHideFallbackId) {
      window.clearTimeout(browserHideFallbackId);
      browserHideFallbackId = 0;
    }
    if (browserHideOnTransitionEnd) {
      browserPanel.removeEventListener("transitionend", browserHideOnTransitionEnd);
      browserHideOnTransitionEnd = null;
    }
  }
  function showBrowserPanelAnimated() {
    cancelBrowserPanelHide();
    if (mainScreen) {
      mainScreen.classList.add("is-browser-mode");
    }
    browserPanel.hidden = false;
    browserPanel.classList.remove("is-open");
    void browserPanel.offsetWidth;
    browserPanel.classList.add("is-open");
  }
  function hideBrowserPanelAnimated(done) {
    if (browserPanel.hidden) {
      if (done) {
        done();
      }
      return;
    }
    cancelBrowserPanelHide();
    browserPanel.classList.remove("is-open");
    var completed = false;
    function finishHide() {
      if (completed) {
        return;
      }
      completed = true;
      cancelBrowserPanelHide();
      if (viewMode === "browser" || browserPanel.classList.contains("is-open")) {
        if (done) {
          done();
        }
        return;
      }
      if (mainScreen) {
        mainScreen.classList.remove("is-browser-mode");
      }
      browserPanel.hidden = true;
      if (done) {
        done();
      }
    }
    browserHideOnTransitionEnd = function (event) {
      if (event.target !== browserPanel || event.propertyName !== "opacity") {
        return;
      }
      finishHide();
    };
    browserPanel.addEventListener("transitionend", browserHideOnTransitionEnd);
    browserHideFallbackId = window.setTimeout(finishHide, PANEL_TRANSITION_MS + 80);
  }
  function enterBrowserMode(fromNative) {
    if (viewMode === "browser") {
      if (fromNative) {
        browserController.onShown();
      }
      return;
    }
    if (viewMode === "settings") {
      exitSettingsMode();
    }
    viewMode = "browser";
    console.log("[CO Main Menu] Entering browser mode");
    window.CommonwealthOnlineUI.onOpenView("multiplayer");
    setSelected(MULTIPLAYER_INDEX);
    showBrowserPanelAnimated();
    if (mainPromptBar) {
      mainPromptBar.hidden = true;
    }
    browserController.show();
    browserController.onShown();
    if (!fromNative) {
      sendCoAction({ action: "openView", view: "multiplayer" });
    }
  }
  function exitBrowserMode(fromNative) {
    if (viewMode !== "browser") {
      return;
    }
    viewMode = "menu";
    console.log("[CO Main Menu] Exiting browser mode");
    window.CommonwealthOnlineUI.onBack();
    browserController.hide();
    hideBrowserPanelAnimated(function () {
      if (mainPromptBar && viewMode !== "browser" && viewMode !== "settings") {
        mainPromptBar.hidden = false;
      }
      if (viewMode === "menu") {
        setSelected(MULTIPLAYER_INDEX);
      }
    });
    if (!fromNative) {
      sendCoAction({ action: "closeBrowser" });
    }
  }
  window.onBrowserShown = function () {
    enterBrowserMode(true);
  };
  window.closeBrowser = function () {
    exitBrowserMode(true);
  };
  window.onBrowserHidden = function () {
    exitBrowserMode(true);
  };
  function setQuitConfirmSelected(index) {
    if (index >= quitConfirmItems.length) {
      return;
    }
    quitConfirmSelectedIndex = index < 0 ? 0 : index;
    quitConfirmItems.forEach(function (item, i) {
      var isSelected = i === quitConfirmSelectedIndex;
      item.classList.toggle("is-selected", isSelected);
      item.setAttribute("aria-selected", isSelected ? "true" : "false");
      item.tabIndex = isSelected ? 0 : -1;
    });
  }
  function moveQuitConfirmSelection(delta) {
    var next = quitConfirmSelectedIndex + delta;
    if (next < 0) {
      next = quitConfirmItems.length - 1;
    } else if (next >= quitConfirmItems.length) {
      next = 0;
    }
    setQuitConfirmSelected(next);
  }
  function showQuitConfirmPanelAnimated() {
    if (mainScreen) {
      mainScreen.classList.add("is-quit-confirm-mode");
    }
    quitConfirmPanel.hidden = false;
    quitConfirmPanel.classList.remove("is-open");
    void quitConfirmPanel.offsetWidth;
    quitConfirmPanel.classList.add("is-open");
  }
  function hideQuitConfirmPanelAnimated(done) {
    if (quitConfirmPanel.hidden) {
      if (done) {
        done();
      }
      return;
    }
    quitConfirmPanel.classList.remove("is-open");
    window.setTimeout(function finishHide() {
      if (viewMode === "quitConfirm" || quitConfirmPanel.classList.contains("is-open")) {
        if (done) {
          done();
        }
        return;
      }
      if (mainScreen) {
        mainScreen.classList.remove("is-quit-confirm-mode");
      }
      quitConfirmPanel.hidden = true;
      if (done) {
        done();
      }
    }, PANEL_TRANSITION_MS + 80);
  }
  function enterQuitConfirmMode() {
    if (viewMode === "quitConfirm") {
      return;
    }
    if (viewMode === "settings") {
      exitSettingsMode();
    } else if (viewMode === "browser") {
      exitBrowserMode();
    }
    viewMode = "quitConfirm";
    console.log("[CO Main Menu] Entering quit confirmation");
    setSelected(QUIT_INDEX);
    setQuitConfirmSelected(1);
    showQuitConfirmPanelAnimated();
  }
  function exitQuitConfirmMode() {
    if (viewMode !== "quitConfirm") {
      return;
    }
    viewMode = "menu";
    console.log("[CO Main Menu] Exiting quit confirmation");
    hideQuitConfirmPanelAnimated(function () {
      if (viewMode === "menu") {
        setSelected(QUIT_INDEX);
      }
    });
  }
  function confirmQuit() {
    console.log("[CO Main Menu] Quit confirmed");
    sendCoAction({ action: "quit" });
  }
  function activateQuitConfirmSelected() {
    var item = quitConfirmItems[quitConfirmSelectedIndex];
    if (!item) {
      return;
    }
    var choice = item.getAttribute("data-choice");
    if (choice === "yes") {
      confirmQuit();
      return;
    }
    exitQuitConfirmMode();
  }
  quitConfirmItems.forEach(function (item, index) {
    item.addEventListener("click", function () {
      setQuitConfirmSelected(index);
      activateQuitConfirmSelected();
    });
  });
  window.onMainMenuShown = function () {
    if (window.CO_StartupTiming) window.CO_StartupTiming.flush();
    if (mainScreen) {
      mainScreen.classList.remove("is-session-loading");
    }
  };
  window.onSessionLoading = function () {
    if (viewMode === "browser") {
      cancelBrowserPanelHide();
      browserController.hide();
      browserPanel.classList.remove("is-open");
      browserPanel.hidden = true;
    } else if (viewMode === "settings") {
      cancelSettingsPanelHide();
      settingsController.hide();
      settingsPanel.classList.remove("is-open");
      settingsPanel.hidden = true;
    } else if (viewMode === "quitConfirm") {
      quitConfirmPanel.classList.remove("is-open");
      quitConfirmPanel.hidden = true;
    }
    viewMode = "menu";
    if (mainScreen) {
      mainScreen.classList.remove("is-browser-mode", "is-settings-mode", "is-quit-confirm-mode");
      mainScreen.classList.add("is-session-loading");
    }
    if (mainPromptBar) {
      mainPromptBar.hidden = true;
    }
    if (settingsPromptBar) {
      settingsPromptBar.hidden = true;
    }
    setSelected(-1);
  };
  function enterSettingsMode() {
    var settings = ensureSettings();
    if (viewMode === "browser") {
      cancelBrowserPanelHide();
      browserController.hide();
      browserPanel.classList.remove("is-open");
      browserPanel.hidden = true;
      if (mainScreen) {
        mainScreen.classList.remove("is-browser-mode");
      }
    }
    viewMode = "settings";
    console.log("[CO Main Menu] Entering settings mode");
    window.CommonwealthOnlineUI.onOpenView("settings");
    setSelected(SETTINGS_INDEX);
    showSettingsPanelAnimated();
    if (mainPromptBar) {
      mainPromptBar.hidden = true;
    }
    settings.show();
  }
  function exitSettingsMode() {
    if (viewMode !== "settings") {
      return;
    }
    viewMode = "menu";
    console.log("[CO Main Menu] Exiting settings mode");
    window.CommonwealthOnlineUI.onBack();
    settingsController.hide();
    hideSettingsPanelAnimated(function () {
      if (mainPromptBar && viewMode !== "browser" && viewMode !== "settings") {
        mainPromptBar.hidden = false;
      }
      if (viewMode === "menu") {
        setSelected(SETTINGS_INDEX);
      }
    });
  }
  function ensureSettings() {
    if (settingsController) return settingsController;
    var started = Date.now();
    settingsController = window.CO_Settings.create({
      mount: settingsPanel,
      promptBar: settingsPromptBar,
      bridge: window.CommonwealthOnlineUI,
      layout: "embedded",
      onBack: function () {
        exitSettingsMode();
      }
    });
    window.CO_VoiceSettings.attach(settingsController, window.CommonwealthOnlineUI);
    if (window.CO_StartupTiming) window.CO_StartupTiming.record("settings-create", started);
    return settingsController;
  }
  var browserStarted = Date.now();
  browserController = window.CO_Browser.create({
    mount: browserPanel,
    layout: "embedded",
    exposeGlobals: true,
    captureKeys: false,
    onBack: function () {
      exitBrowserMode();
    }
  });
  window.CO_App.setPointerMode = function (mode) {
    var gamepad = mode === "gamepad";
    if (mainScreen) {
      mainScreen.classList.toggle("is-gamepad-mode", gamepad);
    }
    // Browser keeps its own prompt/cursor chrome when Multiplayer is open.
    if (browserController && typeof browserController.setPointerMode === "function") {
      browserController.setPointerMode(mode);
    }
  };
  if (window.CO_StartupTiming) window.CO_StartupTiming.record("browser-create", browserStarted);
  document.addEventListener("keydown", function (event) {
    if (viewMode === "settings") {
      settingsController.handleKeydown(event);
      return;
    }
    if (viewMode === "browser") {
      browserController.handleKeydown(event);
      return;
    }
    if (viewMode === "quitConfirm") {
      window.CO_MainMenuKeys(event, moveQuitConfirmSelection, activateQuitConfirmSelected, exitQuitConfirmMode);
      return;
    }
    window.CO_MainMenuKeys(event, moveSelection, activateSelected, function () {
      console.log("[CO Main Menu] Back");
      window.CommonwealthOnlineUI.onBack();
    });
  });
  window.handleControllerInput = function (button, phase) {
    if (phase !== "down") {
      return;
    }
    if (viewMode === "browser") {
      browserController.handleControllerInput(button, phase);
      return;
    }
    if (viewMode === "settings") {
      if (button === "b") {
        settingsController.back();
        return;
      }
      settingsController.handleControllerInput(button, phase);
      return;
    }
    if (viewMode === "quitConfirm") {
      switch (button) {
        case "dpad_up":
          moveQuitConfirmSelection(-1);
          break;
        case "dpad_down":
          moveQuitConfirmSelection(1);
          break;
        case "a":
          activateQuitConfirmSelected();
          break;
        case "b":
          exitQuitConfirmMode();
          break;
      }
      return;
    }
    switch (button) {
      case "dpad_up":
        moveSelection(-1);
        break;
      case "dpad_down":
        moveSelection(1);
        break;
      case "a":
        activateSelected();
        break;
      case "b":
        console.log("[CO Main Menu] Back");
        window.CommonwealthOnlineUI.onBack();
        break;
    }
  };
  if (window.CO_StartupTiming) window.CO_StartupTiming.record("main-script-setup", mainScriptStarted);
})();
