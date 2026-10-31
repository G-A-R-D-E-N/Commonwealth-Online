(function () {
  "use strict";

  function sendCoAction(payload) {
    var msg = JSON.stringify(payload);
    if (typeof window.coAction === "function") {
      window.coAction(msg);
      return true;
    }
    console.warn("[CO Pause Menu] coAction bridge unavailable:", payload);
    return false;
  }

  window.CommonwealthOnlineUI = {
    openGameSettings: function () {
      console.log("[CO UI] Legacy openGameSettings ignored — use native Fallout 4 settings tab");
    },
    setMultiplayerSetting: function (key, value) { if (window.CO_VoiceSettings.forwardSetting(key, value)) { return true; } console.log("[CO UI] Multiplayer setting changed:", key, value); },
    sendSettingsAction: function (payload) {
      return sendCoAction(payload);
    },
    onBack: function () {
      console.log("[CO UI] Back requested from pause menu");
    },
    onOpenView: function (viewName) { return sendCoAction({ action: "openView", view: viewName }); }
  };

  window.CO_App = {};
  function updateScale() {
    var REF_W = 1920;
    var REF_H = 1080;
    var w = window.innerWidth || REF_W;
    var h = window.innerHeight || REF_H;
    var scale = Math.max(1, Math.min(w / REF_W, h / REF_H));
    document.documentElement.style.setProperty("--co-ui-scale", String(scale));
  }
  updateScale();
  window.addEventListener("resize", updateScale);

  window.CO_PromptGlyphs.hydrate();
  var pauseScreen = document.getElementById("pauseScreen");
  var menu = document.getElementById("menu");
  var settingsPanel = document.getElementById("settingsPanel");
  var quitConfirmPanel = document.getElementById("quitConfirmPanel");
  var quitConfirmOptions = document.getElementById("quitConfirmOptions");
  var sessionLockoutPanel = document.getElementById("sessionLockoutPanel");
  var sessionLockoutTitle = document.getElementById("sessionLockoutTitle");
  var sessionLockoutReason = document.getElementById("sessionLockoutReason");
  var sessionLockoutOptions = document.getElementById("sessionLockoutOptions");
  var travelConfirmPanel = document.getElementById("travelConfirmPanel");
  var travelConfirmOptions = document.getElementById("travelConfirmOptions");
  var travelConfirmTitle = document.getElementById("travelConfirmTitle");
  var travelConfirmDestination = document.getElementById("travelConfirmDestination");
  var pausePromptBar = document.getElementById("pausePromptBar");
  var settingsPromptBar = document.getElementById("settingsPromptBar");
  var mapPromptBar = document.getElementById("mapPromptBar");
  var mapSelectPromptLabel = document.getElementById("mapSelectPromptLabel");
  var mapBackButton = document.getElementById("mapBackButton");
  var serverPanel = document.getElementById("serverPanel");
  var serverNameEl = document.getElementById("serverName");
  var serverPlayersEl = document.getElementById("serverPlayers");
  var serverPingEl = document.getElementById("serverPing");
  var serverSignalBars = document.getElementById("serverSignalBars");
  var leftPanel = document.querySelector(".left-panel");
  var mapWorld = document.getElementById("mapWorld");
  var mapMarkers = document.getElementById("mapMarkers");
  var mapElsewhere = document.getElementById("mapElsewhere");

  if (
    !menu ||
    !settingsPanel ||
    !quitConfirmPanel ||
    !quitConfirmOptions ||
    !sessionLockoutPanel ||
    !sessionLockoutOptions ||
    !travelConfirmPanel ||
    !travelConfirmOptions ||
    !window.CO_Settings
  ) {
    return;
  }

  var settingsController = null;

  var items = Array.prototype.slice.call(menu.querySelectorAll(".menu-item"));
  var quitConfirmItems = Array.prototype.slice.call(quitConfirmOptions.querySelectorAll(".quit-confirm__option"));
  var sessionLockoutItems = Array.prototype.slice.call(sessionLockoutOptions.querySelectorAll(".quit-confirm__option"));
  var travelConfirmItems = Array.prototype.slice.call(travelConfirmOptions.querySelectorAll(".quit-confirm__option"));
  var SETTINGS_INDEX = 0;
  var QUIT_INDEX = 0;

  // viewMode: "pause" | "map" | "settings" | "quitConfirm" | "travelConfirm" | "sessionLockout"
  var viewMode = "pause";
  var selectedIndex = -1;
  var quitConfirmSelectedIndex = 1;
  var travelConfirmSelectedIndex = 0;
  var SETTINGS_TRANSITION_MS = 220;
  var PANEL_TRANSITION_MS = 220;
  var settingsHideFallbackId = 0;
  var settingsHideOnTransitionEnd = null;
  var quitting = false;
  var sessionLockoutActive = false;
  var sessionLockoutCode = "";
  var sessionLockoutSelectedIndex = 0;
  var lockoutMainMenuPending = false;
  var selectedMapMarkerId = null;
  var selectedMapMarkerCanTravel = false;
  var selectedMapMarkerName = "";
  var didPanThisGesture = false;
  var pointerMode = "mouse";
  var mapControls = null;
  var othersElsewhere = 0;
  var padAxes = { lx: 0, ly: 0, rx: 0, ry: 0 };
  var padAxesRafId = 0;

  function isGamepadMode() {
    return pointerMode === "gamepad";
  }

  function syncPromptVisibility() {
    var gamepad = isGamepadMode();
    if (pauseScreen) {
      pauseScreen.classList.toggle("is-gamepad-mode", gamepad);
    }

    var inMap = viewMode === "map" || viewMode === "travelConfirm";
    if (mapPromptBar) {
      mapPromptBar.hidden = !inMap;
    }
    if (mapBackButton) {
      if (inMap && !gamepad) {
        mapBackButton.classList.add("is-visible");
        mapBackButton.setAttribute("aria-hidden", "false");
      } else {
        mapBackButton.classList.remove("is-visible");
        mapBackButton.setAttribute("aria-hidden", "true");
      }
    }
    if (pausePromptBar) {
      pausePromptBar.hidden =
        (viewMode !== "pause" && viewMode !== "quitConfirm") || sessionLockoutActive;
    }
    syncElsewhereVisibility();
  }

  function syncElsewhereVisibility() {
    if (!mapElsewhere) {
      return;
    }
    var inMap = viewMode === "map" || viewMode === "travelConfirm";
    var count = othersElsewhere;
    mapElsewhere.hidden = !inMap || count < 1;
    mapElsewhere.textContent =
      count === 1 ? "1 ALLY ELSEWHERE" : count + " ALLIES ELSEWHERE";
  }

  function setSelected(index) {
    if (index >= items.length) {
      return;
    }
    selectedIndex = index < 0 ? -1 : index;
    Array.prototype.slice.call(menu.querySelectorAll(".menu-item")).forEach(function (item) {
      var isSelected = items[selectedIndex] === item;
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
    setSelected(next);
  }

  function activateSelected() {
    var item = items[selectedIndex];
    if (!item) {
      return;
    }

    var action = item.getAttribute("data-action");
    console.log("[CO Pause Menu] Selected:", action);

    if (action === "resume") {
      requestResume();
    } else if (action === "map") {
      enterMapMode();
    } else if (action === "settings") {
      enterSettingsMode();
    } else if (action === "social") {
      window.CommonwealthOnlineUI.onOpenView("social");
    } else if (action === "quests") {
      window.CommonwealthOnlineUI.onOpenView("quests");
    } else if (action === "server") {
      window.CommonwealthOnlineUI.onOpenView("server");
    } else if (action === "help") {
      window.CommonwealthOnlineUI.onOpenView("help");
    } else if (action === "quit") {
      enterQuitConfirmMode();
    }
  }

  function requestResume() {
    if (quitting || sessionLockoutActive) {
      return;
    }
    console.log("[CO Pause Menu] Resume game");
    sendCoAction({ action: "resume" });
    setSelected(-1);
  }

  function sessionLockoutTitleFor(code) {
    return ({ banned: "YOU HAVE BEEN BANNED", kicked: "YOU HAVE BEEN KICKED", server_full: "SERVER FULL" })[code] || "DISCONNECTED FROM SERVER";
  }

  function setSessionLockoutSelected(index) {
    sessionLockoutSelectedIndex = index;
    sessionLockoutItems.forEach(function (item, i) {
      var isSelected = i === index;
      item.classList.toggle("is-selected", isSelected);
      item.setAttribute("aria-selected", isSelected ? "true" : "false");
      item.tabIndex = isSelected ? 0 : -1;
    });
  }

  function moveSessionLockoutSelection(delta) {
    var count = sessionLockoutItems.length;
    setSessionLockoutSelected((sessionLockoutSelectedIndex + delta + count) % count);
  }

  function activateSessionLockoutSelected() {
    if (quitting) {
      return;
    }
    var item = sessionLockoutItems[sessionLockoutSelectedIndex];
    if (!item) {
      return;
    }
    if (item.getAttribute("data-choice") === "mainMenu") {
      if (lockoutMainMenuPending) {
        return;
      }
      lockoutMainMenuPending = true;
      quitting = true;
      if (pauseScreen) {
        pauseScreen.classList.add("is-quitting");
      }
      sendCoAction({ action: "lockoutMainMenu" });
      return;
    }
    requestQuit("quitToDesktop");
  }

  function enterSessionLockoutMode(code, reason) {
    if (!lockoutMainMenuPending) {
      quitting = false;
      if (pauseScreen) {
        pauseScreen.classList.remove("is-quitting");
      }
    }
    sessionLockoutActive = true;
    sessionLockoutCode = String(code || "").toLowerCase();
    var title = sessionLockoutTitleFor(sessionLockoutCode);

    if (sessionLockoutTitle) {
      sessionLockoutTitle.textContent = title;
    }
    if (sessionLockoutReason) {
      var reasonText = String(reason || "").trim();
      sessionLockoutReason.textContent = reasonText || "No reason provided.";
    }

    // Collapse any other modal/state into the lockout surface.
    if (viewMode === "settings") {
      cancelSettingsPanelHide();
      settingsController.hide();
      settingsPanel.classList.remove("is-open");
      settingsPanel.hidden = true;
    } else if (viewMode === "map" || viewMode === "travelConfirm") {
      sendCoAction({ action: "mapExit" });
      if (travelConfirmPanel) {
        travelConfirmPanel.classList.remove("is-open");
        travelConfirmPanel.hidden = true;
      }
      if (leftPanel) {
        leftPanel.classList.remove("is-hidden");
      }
      if (mapWorld) {
        mapWorld.classList.add("is-blurred");
        mapWorld.classList.remove("is-interactive");
      }
    }

    quitConfirmPanel.classList.remove("is-open");
    quitConfirmPanel.hidden = true;

    viewMode = "sessionLockout";
    if (pauseScreen) {
      pauseScreen.classList.add("is-session-lockout-mode");
      pauseScreen.classList.remove(
        "is-quit-confirm-mode",
        "is-travel-confirm-mode",
        "is-settings-mode"
      );
    }

    sessionLockoutPanel.hidden = false;
    sessionLockoutPanel.classList.remove("is-open");
    void sessionLockoutPanel.offsetWidth;
    sessionLockoutPanel.classList.add("is-open");

    setSessionLockoutSelected(0);

    syncPromptVisibility();
    console.log("[CO Pause Menu] Session lockout active:", sessionLockoutCode || "unknown");
  }

  window.showSessionLockout = function (code, reason) {
    enterSessionLockoutMode(code, reason);
  };

  window.hideSessionLockout = function () {
    sessionLockoutActive = false;
    lockoutMainMenuPending = false;
    sessionLockoutPanel.classList.remove("is-open");
    sessionLockoutPanel.hidden = true;
    if (pauseScreen) {
      pauseScreen.classList.remove("is-session-lockout-mode");
    }
    if (viewMode === "sessionLockout") {
      viewMode = "pause";
    }
    syncPromptVisibility();
  };

  function requestQuit(action) {
    if (quitting) {
      return;
    }
    quitting = true;
    if (pauseScreen) {
      pauseScreen.classList.add("is-quitting");
    }
    sendCoAction({ action: action });
  }

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
    if (pauseScreen) {
      pauseScreen.classList.add("is-quit-confirm-mode");
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
      if (pauseScreen) {
        pauseScreen.classList.remove("is-quit-confirm-mode");
      }
      quitConfirmPanel.hidden = true;
      if (done) {
        done();
      }
    }, PANEL_TRANSITION_MS + 80);
  }

  function enterQuitConfirmMode() {
    if (quitting || viewMode === "quitConfirm") {
      return;
    }

    if (viewMode === "settings") {
      exitSettingsMode();
    } else if (viewMode === "map" || viewMode === "travelConfirm") {
      if (viewMode === "travelConfirm") {
        travelConfirmPanel.classList.remove("is-open");
        travelConfirmPanel.hidden = true;
        if (pauseScreen) {
          pauseScreen.classList.remove("is-travel-confirm-mode");
        }
        viewMode = "map";
      }
      exitMapMode();
    }

    viewMode = "quitConfirm";
    console.log("[CO Pause Menu] Entering quit confirmation");
    setSelected(QUIT_INDEX);
    setQuitConfirmSelected(2);
    showQuitConfirmPanelAnimated();
    syncPromptVisibility();
  }

  function exitQuitConfirmMode() {
    if (quitting || viewMode !== "quitConfirm") {
      return;
    }

    viewMode = "pause";
    console.log("[CO Pause Menu] Exiting quit confirmation");

    hideQuitConfirmPanelAnimated(function () {
      if (viewMode === "pause") {
        syncPromptVisibility();
        setSelected(QUIT_INDEX);
      }
    });
  }

  function activateQuitConfirmSelected() {
    if (quitting) {
      return;
    }
    var item = quitConfirmItems[quitConfirmSelectedIndex];
    if (!item) {
      return;
    }

    var choice = item.getAttribute("data-choice");
    if (choice === "desktop" || choice === "mainMenu") {
      requestQuit(choice === "mainMenu" ? "quitToMainMenu" : "quitToDesktop");
      return;
    }

    exitQuitConfirmMode();
  }

  function resetToPauseRoot() {
    quitting = false;
    paintQuitSaveStatus("", "");
    if (pauseScreen) {
      pauseScreen.classList.remove("is-quitting", "is-quit-confirm-mode", "is-travel-confirm-mode");
    }

    if (viewMode === "settings") {
      cancelSettingsPanelHide();
      settingsController.hide();
      settingsPanel.classList.remove("is-open");
      settingsPanel.hidden = true;
      if (pauseScreen) {
        pauseScreen.classList.remove("is-settings-mode");
      }
    } else if (viewMode === "map" || viewMode === "travelConfirm") {
      sendCoAction({ action: "mapExit" });
      clearMapMarkerSelection();
      if (travelConfirmPanel) {
        travelConfirmPanel.classList.remove("is-open");
        travelConfirmPanel.hidden = true;
      }
      if (mapBackButton) {
        mapBackButton.classList.remove("is-visible");
        mapBackButton.setAttribute("aria-hidden", "true");
      }
      if (leftPanel) {
        leftPanel.classList.remove("is-hidden");
      }
      if (mapWorld) {
        mapWorld.classList.add("is-blurred");
        mapWorld.classList.remove("is-interactive");
      }
      if (mapPromptBar) {
        mapPromptBar.hidden = true;
      }
    } else if (viewMode === "quitConfirm") {
      quitConfirmPanel.classList.remove("is-open");
      quitConfirmPanel.hidden = true;
    }

    viewMode = "pause";
    syncServerPanelMapVisibility();
    if (settingsPromptBar) {
      settingsPromptBar.hidden = true;
    }
    syncPromptVisibility();
    setSelected(-1);
  }

  menu.addEventListener("click", function (event) {
    var el = event.target && event.target.closest ? event.target.closest(".menu-item") : null;
    var index = el ? items.indexOf(el) : -1;
    if (index < 0 || quitting || sessionLockoutActive || viewMode === "quitConfirm" || viewMode === "map") {
      return;
    }
    if (viewMode === "settings") {
      if (index === SETTINGS_INDEX) {
        return;
      }
      exitSettingsMode();
    }
    setSelected(index);
    activateSelected();
  });

  quitConfirmItems.forEach(function (item, index) {
    item.addEventListener("click", function () {
      if (quitting || sessionLockoutActive) {
        return;
      }
      setQuitConfirmSelected(index);
      activateQuitConfirmSelected();
    });
  });

  sessionLockoutItems.forEach(function (item, index) {
    item.addEventListener("click", function () {
      if (quitting) {
        return;
      }
      setSessionLockoutSelected(index);
      activateSessionLockoutSelected();
    });
  });

  travelConfirmItems.forEach(function (item, index) {
    item.addEventListener("click", function () {
      if (quitting) {
        return;
      }
      setTravelConfirmSelected(index);
      activateTravelConfirmSelected();
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
    if (pauseScreen) {
      pauseScreen.classList.add("is-settings-mode");
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
      if (pauseScreen) {
        pauseScreen.classList.remove("is-settings-mode");
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
    settingsHideFallbackId = window.setTimeout(finishHide, SETTINGS_TRANSITION_MS + 80);
  }

  function enterSettingsMode() {
    if (viewMode === "map") {
      exitMapMode();
    }

    viewMode = "settings";

    console.log("[CO Pause Menu] Entering settings mode");
    window.CommonwealthOnlineUI.onOpenView("settings");

    setSelected(SETTINGS_INDEX);

    showSettingsPanelAnimated();
    if (mapWorld) {
      mapWorld.classList.add("is-blurred");
      mapWorld.classList.remove("is-interactive");
    }
    syncPromptVisibility();

    settingsController.show();
  }

  function exitSettingsMode() {
    if (viewMode !== "settings") {
      return;
    }

    viewMode = "pause";
    console.log("[CO Pause Menu] Exiting settings mode");
    window.CommonwealthOnlineUI.onBack();

    settingsController.hide();

    hideSettingsPanelAnimated(function () {
      syncPromptVisibility();
      setSelected(SETTINGS_INDEX);
    });
  }

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

  function enterMapMode() {
    if (viewMode === "settings") {
      cancelSettingsPanelHide();
      settingsController.hide();
      settingsPanel.classList.remove("is-open");
      settingsPanel.hidden = true;
      if (pauseScreen) {
        pauseScreen.classList.remove("is-settings-mode");
      }
    }

    viewMode = "map";
    console.log("[CO Pause Menu] Entering map mode");
    window.CommonwealthOnlineUI.onOpenView("map");
    sendCoAction({ action: "mapEnter" });
    clearMapMarkerSelection();

    setSelected(-1);
    if (leftPanel) {
      leftPanel.classList.add("is-hidden");
    }
    syncServerPanelMapVisibility();
    if (mapWorld) {
      mapWorld.classList.remove("is-blurred");
      mapWorld.classList.add("is-interactive");
    }
    syncPromptVisibility();
    if (
      Math.abs(padAxes.lx) > 0.02 ||
      Math.abs(padAxes.ly) > 0.02 ||
      Math.abs(padAxes.ry) > 0.08
    ) {
      ensurePadAxesLoop();
    }
  }

  function exitMapMode() {
    if (viewMode === "travelConfirm") {
      exitTravelConfirmMode();
    }
    if (viewMode !== "map") {
      return;
    }

    viewMode = "pause";
    console.log("[CO Pause Menu] Exiting map mode");
    sendCoAction({ action: "mapExit" });
    window.CommonwealthOnlineUI.onBack();
    clearMapMarkerSelection();
    padAxes.lx = 0;
    padAxes.ly = 0;
    padAxes.rx = 0;
    padAxes.ry = 0;

    if (leftPanel) {
      leftPanel.classList.remove("is-hidden");
    }
    syncServerPanelMapVisibility();
    if (mapWorld) {
      mapWorld.classList.add("is-blurred");
      mapWorld.classList.remove("is-interactive");
    }
    syncPromptVisibility();

    setSelected(-1);
  }

  function clampPct(value) {
    var n = Number(value);
    if (!isFinite(n)) {
      return 0;
    }
    return Math.max(-5, Math.min(105, n));
  }

  function updateMapSelectPrompt() {
    if (!mapSelectPromptLabel) {
      return;
    }
    mapSelectPromptLabel.textContent =
      selectedMapMarkerId && selectedMapMarkerCanTravel ? "FAST TRAVEL" : "SELECT";
  }

  function clearMapMarkerSelection() {
    selectedMapMarkerId = null;
    selectedMapMarkerCanTravel = false;
    selectedMapMarkerName = "";
    if (mapMarkers) {
      var selected = mapMarkers.querySelectorAll(".marker--selected");
      for (var i = 0; i < selected.length; i++) {
        selected[i].classList.remove("marker--selected");
      }
    }
    updateMapSelectPrompt();
  }

  function selectMapMarker(id, canTravel, name) {
    if (!mapMarkers || !id) {
      return;
    }

    var previous = mapMarkers.querySelector(".marker--selected");
    if (previous) {
      previous.classList.remove("marker--selected");
    }

    var el = mapMarkers.querySelector('[data-marker-id="' + String(id).replace(/"/g, "") + '"]');
    if (!el || !el.classList.contains("marker--location")) {
      clearMapMarkerSelection();
      return;
    }

    selectedMapMarkerId = String(id);
    selectedMapMarkerCanTravel = !!canTravel;
    selectedMapMarkerName = name ? String(name) : "";
    el.classList.add("marker--selected");
    updateMapSelectPrompt();
  }

  function requestFastTravel(markerId) {
    if (!markerId || quitting) {
      return;
    }
    console.log("[CO Pause Menu] Fast travel requested:", markerId);
    sendCoAction({ action: "fastTravel", markerId: String(markerId) });
  }

  function setTravelConfirmSelected(index) {
    if (index >= travelConfirmItems.length) {
      return;
    }

    travelConfirmSelectedIndex = index < 0 ? 0 : index;

    travelConfirmItems.forEach(function (item, i) {
      var isSelected = i === travelConfirmSelectedIndex;
      item.classList.toggle("is-selected", isSelected);
      item.setAttribute("aria-selected", isSelected ? "true" : "false");
      item.tabIndex = isSelected ? 0 : -1;
    });
  }

  function moveTravelConfirmSelection(delta) {
    var next = travelConfirmSelectedIndex + delta;
    if (next < 0) {
      next = travelConfirmItems.length - 1;
    } else if (next >= travelConfirmItems.length) {
      next = 0;
    }
    setTravelConfirmSelected(next);
  }

  function showTravelConfirmPanelAnimated() {
    if (pauseScreen) {
      pauseScreen.classList.add("is-travel-confirm-mode");
    }
    travelConfirmPanel.hidden = false;
    travelConfirmPanel.classList.remove("is-open");
    void travelConfirmPanel.offsetWidth;
    travelConfirmPanel.classList.add("is-open");
  }

  function hideTravelConfirmPanelAnimated(done) {
    if (travelConfirmPanel.hidden) {
      if (done) {
        done();
      }
      return;
    }

    travelConfirmPanel.classList.remove("is-open");

    window.setTimeout(function finishHide() {
      if (viewMode === "travelConfirm" || travelConfirmPanel.classList.contains("is-open")) {
        if (done) {
          done();
        }
        return;
      }
      if (pauseScreen) {
        pauseScreen.classList.remove("is-travel-confirm-mode");
      }
      travelConfirmPanel.hidden = true;
      if (done) {
        done();
      }
    }, PANEL_TRANSITION_MS + 80);
  }

  function enterTravelConfirmMode() {
    if (quitting || viewMode === "travelConfirm") {
      return;
    }
    if (!selectedMapMarkerId || !selectedMapMarkerCanTravel) {
      return;
    }

    viewMode = "travelConfirm";
    console.log("[CO Pause Menu] Entering travel confirmation for", selectedMapMarkerId);

    var destination = selectedMapMarkerName || "LOCATION";
    if (travelConfirmTitle) {
      travelConfirmTitle.textContent = "TRAVEL TO LOCATION?";
    }
    if (travelConfirmDestination) {
      travelConfirmDestination.textContent = destination;
    }

    setTravelConfirmSelected(0);
    showTravelConfirmPanelAnimated();
    syncPromptVisibility();
  }

  function exitTravelConfirmMode() {
    if (quitting || viewMode !== "travelConfirm") {
      return;
    }

    viewMode = "map";
    console.log("[CO Pause Menu] Exiting travel confirmation");

    hideTravelConfirmPanelAnimated(function () {
      if (viewMode === "map") {
        syncPromptVisibility();
        updateMapSelectPrompt();
      }
    });
  }

  function activateTravelConfirmSelected() {
    if (quitting) {
      return;
    }

    var item = travelConfirmItems[travelConfirmSelectedIndex];
    if (!item) {
      return;
    }

    var choice = item.getAttribute("data-choice");
    if (choice === "travel") {
      var markerId = selectedMapMarkerId;
      viewMode = "map";
      travelConfirmPanel.classList.remove("is-open");
      travelConfirmPanel.hidden = true;
      if (pauseScreen) {
        pauseScreen.classList.remove("is-travel-confirm-mode");
      }
      if (markerId) {
        requestFastTravel(markerId);
      }
      return;
    }

    exitTravelConfirmMode();
  }

  function confirmSelectedFastTravel() {
    enterTravelConfirmMode();
  }

  function getLocationMarkerElements() {
    if (!mapMarkers) {
      return [];
    }
    return Array.prototype.slice.call(mapMarkers.querySelectorAll(".marker--location"));
  }

  function readMarkerTravelMeta(el) {
    if (!el) {
      return { id: "", canTravel: false, name: "" };
    }
    return {
      id: el.dataset.markerId ? String(el.dataset.markerId) : "",
      canTravel: el.dataset.discovered === "1",
      name: el.getAttribute("aria-label") || ""
    };
  }

  function centerMapOnMarkerElement(el) {
    if (!el || !mapControls || typeof mapControls.centerOnMarkerPct !== "function") {
      return;
    }
    var xPct = parseFloat(el.style.left);
    var yPct = parseFloat(el.style.top);
    if (!isFinite(xPct) || !isFinite(yPct)) {
      return;
    }
    mapControls.centerOnMarkerPct(xPct, yPct);
  }

  function cycleMapMarker(delta) {
    var markers = getLocationMarkerElements();
    if (!markers.length) {
      return;
    }

    var current = -1;
    if (selectedMapMarkerId) {
      for (var i = 0; i < markers.length; i++) {
        if (markers[i].dataset.markerId === selectedMapMarkerId) {
          current = i;
          break;
        }
      }
    }

    var next = current < 0 ? (delta > 0 ? 0 : markers.length - 1) : current + delta;
    if (next < 0) {
      next = markers.length - 1;
    } else if (next >= markers.length) {
      next = 0;
    }

    var el = markers[next];
    var meta = readMarkerTravelMeta(el);
    if (!meta.id) {
      return;
    }
    selectMapMarker(meta.id, meta.canTravel, meta.name);
    centerMapOnMarkerElement(el);
  }

  function selectNearestLocationMarker() {
    var markers = getLocationMarkerElements();
    if (!markers.length) {
      return false;
    }

    var playerEl = mapMarkers ? mapMarkers.querySelector('.marker--player') : null;
    var playerX = playerEl ? parseFloat(playerEl.style.left) : 50;
    var playerY = playerEl ? parseFloat(playerEl.style.top) : 50;
    if (!isFinite(playerX)) {
      playerX = 50;
    }
    if (!isFinite(playerY)) {
      playerY = 50;
    }

    var best = null;
    var bestDist = Infinity;
    for (var i = 0; i < markers.length; i++) {
      var el = markers[i];
      var x = parseFloat(el.style.left);
      var y = parseFloat(el.style.top);
      if (!isFinite(x) || !isFinite(y)) {
        continue;
      }
      var dx = x - playerX;
      var dy = y - playerY;
      var dist = dx * dx + dy * dy;
      if (dist < bestDist) {
        bestDist = dist;
        best = el;
      }
    }

    if (!best) {
      best = markers[0];
    }

    var meta = readMarkerTravelMeta(best);
    if (!meta.id) {
      return false;
    }
    selectMapMarker(meta.id, meta.canTravel, meta.name);
    centerMapOnMarkerElement(best);
    return true;
  }

  function activateMapSelect() {
    if (!selectedMapMarkerId) {
      if (!selectNearestLocationMarker()) {
        return;
      }
    }
    if (selectedMapMarkerCanTravel) {
      confirmSelectedFastTravel();
    }
  }

  function ensurePadAxesLoop() {
    if (padAxesRafId !== 0) {
      return;
    }
    padAxesRafId = requestAnimationFrame(tickPadAxes);
  }

  function tickPadAxes() {
    padAxesRafId = 0;
    if (viewMode !== "map" || quitting || !mapControls) {
      return;
    }

    var active = false;
    if (typeof mapControls.applyStickInput === "function") {
      active = mapControls.applyStickInput(padAxes.lx, padAxes.ly, padAxes.ry);
    }

    if (active) {
      ensurePadAxesLoop();
    }
  }

  function setMarkerPosition(el, xPct, yPct, rotDeg) {
    el.style.left = clampPct(xPct) + "%";
    el.style.top = clampPct(yPct) + "%";
    var rot = isFinite(Number(rotDeg)) ? Number(rotDeg) : 0;
    el.style.transform =
      "translate(-50%, -50%) scale(var(--map-marker-scale, 1)) rotate(" +
      rot +
      "deg)";
    el.style.setProperty("--marker-rot", rot + "deg");
  }

  function ensureMarkerEl(id, className) {
    var safeId = String(id).replace(/"/g, "");
    var el = mapMarkers.querySelector('[data-marker-id="' + safeId + '"]');
    if (el) {
      return el;
    }
    el = document.createElement("div");
    el.dataset.markerId = safeId;
    el.className = className;
    mapMarkers.appendChild(el);
    return el;
  }

  function pruneMarkers(keepIds) {
    var nodes = mapMarkers.querySelectorAll("[data-marker-id]");
    for (var i = 0; i < nodes.length; i++) {
      var node = nodes[i];
      if (!keepIds[node.dataset.markerId]) {
        node.remove();
      }
    }
  }

  function syncMarkerNameLabel(el, name) {
    var label = el.querySelector(".marker__name");
    if (!name) {
      if (label) {
        label.remove();
      }
      el.removeAttribute("aria-label");
      el.removeAttribute("tabindex");
      return;
    }
    if (!label) {
      label = document.createElement("span");
      label.className = "marker__name";
      el.appendChild(label);
    }
    if (label.textContent !== name) {
      label.textContent = name;
    }
    el.setAttribute("aria-label", name);
    el.tabIndex = 0;
  }

  function bindLocationMarkerInteraction(el, id, discovered, name) {
    el.dataset.discovered = discovered ? "1" : "0";
    el.onclick = null;
    el.onkeydown = null;
    el.onmousedown = function (event) {
      // Keep map pan from starting when interacting with a marker.
      event.stopPropagation();
    };
    el.onclick = function (event) {
      event.stopPropagation();
      if (viewMode !== "map" || quitting) {
        return;
      }

      selectMapMarker(id, discovered, name);
      if (discovered) {
        enterTravelConfirmMode();
      }
    };
    el.onkeydown = function (event) {
      if (viewMode !== "map" || quitting) {
        return;
      }
      if (event.key !== "Enter" && event.key !== " ") {
        return;
      }
      event.preventDefault();
      event.stopPropagation();
      selectMapMarker(id, discovered, name);
      if (discovered) {
        enterTravelConfirmMode();
      }
    };
  }

  function renderMapUpdate(payload) {
    if (!mapMarkers || !payload || typeof payload !== "object") {
      return;
    }

    var keepIds = {};

    var markers = Array.isArray(payload.markers) ? payload.markers : [];
    for (var i = 0; i < markers.length; i++) {
      var marker = markers[i];
      if (!marker) {
        continue;
      }

      var id = marker.id ? String(marker.id) : "loc-" + i;
      keepIds[id] = true;

      var loc = ensureMarkerEl(id, "marker marker--location");
      var discovered = !!marker.discovered;
      var className = "marker marker--location";
      if (marker.highlighted) {
        className += " marker--highlighted";
      }
      if (!discovered) {
        className += " marker--undiscovered";
      } else {
        className += " marker--can-travel";
      }
      if (selectedMapMarkerId === id) {
        className += " marker--selected";
        selectedMapMarkerCanTravel = discovered;
        if (name) {
          selectedMapMarkerName = name;
        }
      }
      loc.className = className;

      var name = marker.name ? String(marker.name) : "";
      syncMarkerNameLabel(loc, name);
      bindLocationMarkerInteraction(loc, id, discovered, name);

      var img = loc.querySelector(".marker__icon");
      if (!img) {
        img = document.createElement("img");
        img.className = "marker__icon";
        img.draggable = false;
        loc.insertBefore(img, loc.firstChild);
      }
      img.alt = name;
      var iconSrc = marker.icon || "../MapMarkers/icon_38.png";
      if (img.getAttribute("src") !== iconSrc) {
        img.src = iconSrc;
      }

      setMarkerPosition(loc, marker.xPct, marker.yPct, 0);
    }

    if (selectedMapMarkerId && !keepIds[selectedMapMarkerId]) {
      clearMapMarkerSelection();
    } else {
      updateMapSelectPrompt();
    }

    othersElsewhere = Math.max(0, Math.floor(Number(payload.othersElsewhere) || 0));
    syncElsewhereVisibility();

    var others = Array.isArray(payload.others) ? payload.others : [];
    for (var j = 0; j < others.length; j++) {
      var other = others[j];
      if (!other) {
        continue;
      }
      var ally = ensureMarkerEl("ally-" + (other.id != null ? String(other.id) : j), "marker marker--ally");
      keepIds[ally.dataset.markerId] = true;
      ally.classList.toggle("marker--faction", other.faction === true);
      syncMarkerNameLabel(ally, other.name ? String(other.name) : "");
      setMarkerPosition(ally, other.xPct, other.yPct, other.rotDeg || 0);
    }

    if (payload.player) {
      keepIds.player = true;
      var player = ensureMarkerEl("player", "marker marker--player");
      player.className = "marker marker--player";
      player.setAttribute("aria-label", "You");

      var playerImg = player.querySelector(".marker__icon");
      if (!playerImg) {
        playerImg = document.createElement("img");
        playerImg.className = "marker__icon";
        playerImg.alt = "Player";
        playerImg.draggable = false;
        playerImg.src = "../MapMarkers/icon_94.png";
        player.appendChild(playerImg);
      }

      setMarkerPosition(player, payload.player.xPct, payload.player.yPct, payload.player.rotDeg || 0);
    }

    pruneMarkers(keepIds);
  }

  window.onMapUpdate = function (payload) {
    try {
      renderMapUpdate(payload);
    } catch (err) {
      console.warn("[CO Pause Menu] onMapUpdate failed:", err);
    }
  };

  function handlePauseKeydown(event) {
    switch (event.key) {
      case "ArrowUp":
        event.preventDefault();
        moveSelection(-1);
        break;
      case "ArrowDown":
        event.preventDefault();
        moveSelection(1);
        break;
      case "Enter":
      case " ":
        event.preventDefault();
        activateSelected();
        break;
      case "Escape":
        event.preventDefault();
        requestResume();
        break;
      default:
        break;
    }
  }

  function handleQuitConfirmKeydown(event) {
    switch (event.key) {
      case "ArrowUp":
        event.preventDefault();
        moveQuitConfirmSelection(-1);
        break;
      case "ArrowDown":
        event.preventDefault();
        moveQuitConfirmSelection(1);
        break;
      case "Enter":
      case " ":
        event.preventDefault();
        activateQuitConfirmSelected();
        break;
      case "Escape":
        event.preventDefault();
        exitQuitConfirmMode();
        break;
      default:
        break;
    }
  }

  function handleTravelConfirmKeydown(event) {
    switch (event.key) {
      case "ArrowUp":
        event.preventDefault();
        moveTravelConfirmSelection(-1);
        break;
      case "ArrowDown":
        event.preventDefault();
        moveTravelConfirmSelection(1);
        break;
      case "Enter":
      case " ":
        event.preventDefault();
        activateTravelConfirmSelected();
        break;
      case "Escape":
        event.preventDefault();
        exitTravelConfirmMode();
        break;
      default:
        break;
    }
  }

  function handleMapKeydown(event) {
    if (event.key === "Escape") {
      event.preventDefault();
      exitMapMode();
      return;
    }
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      activateMapSelect();
      return;
    }
    if (event.key === "[" || event.key === "PageUp") {
      event.preventDefault();
      cycleMapMarker(-1);
      return;
    }
    if (event.key === "]" || event.key === "PageDown") {
      event.preventDefault();
      cycleMapMarker(1);
      return;
    }
    if (!mapControls) {
      return;
    }
    var pan = mapControls.keyPanSpeed || 28;
    if (event.key === "ArrowLeft") {
      event.preventDefault();
      mapControls.panBy(-pan, 0);
    } else if (event.key === "ArrowRight") {
      event.preventDefault();
      mapControls.panBy(pan, 0);
    } else if (event.key === "ArrowUp") {
      event.preventDefault();
      mapControls.panBy(0, -pan);
    } else if (event.key === "ArrowDown") {
      event.preventDefault();
      mapControls.panBy(0, pan);
    } else if (event.key === "+" || event.key === "=") {
      event.preventDefault();
      mapControls.zoomBy(1);
    } else if (event.key === "-" || event.key === "_") {
      event.preventDefault();
      mapControls.zoomBy(-1);
    }
  }

  document.addEventListener("keydown", function (event) {
    if (quitting) {
      event.preventDefault();
      return;
    }
    if (viewMode === "sessionLockout" || sessionLockoutActive) {
      if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        activateSessionLockoutSelected();
      } else if (event.key === "ArrowUp" || event.key === "ArrowLeft") {
        event.preventDefault();
        moveSessionLockoutSelection(-1);
      } else if (event.key === "ArrowDown" || event.key === "ArrowRight") {
        event.preventDefault();
        moveSessionLockoutSelection(1);
      } else {
        // Escape / Back must not dismiss a ban/kick lockout.
        event.preventDefault();
      }
      return;
    }
    if (viewMode === "settings") {
      settingsController.handleKeydown(event);
      return;
    }
    if (viewMode === "travelConfirm") {
      handleTravelConfirmKeydown(event);
      return;
    }
    if (viewMode === "map") {
      handleMapKeydown(event);
      return;
    }
    if (viewMode === "quitConfirm") {
      handleQuitConfirmKeydown(event);
      return;
    }
    handlePauseKeydown(event);
  });

  window.CO_App.setPointerMode = function (mode) {
    pointerMode = mode === "gamepad" ? "gamepad" : "mouse";
    syncPromptVisibility();
  };

  window.CO_ServerPanel.init({
    panel: serverPanel,
    name: serverNameEl,
    players: serverPlayersEl,
    ping: serverPingEl,
    signalBars: serverSignalBars,
    isMapMode: function () { return viewMode === "map"; }
  });

  function syncServerPanelMapVisibility() {
    window.CO_ServerPanel.syncMapVisibility();
  }

  window.setCurrentServerInfo = function (info) {
    window.CO_ServerPanel.setInfo(info);
  };

  function indexOfAction(action) {
    return items.findIndex(function (item) {
      return item.getAttribute("data-action") === action;
    });
  }

  function refreshItems() {
    var selected = items[selectedIndex] || null;
    items = Array.prototype.slice.call(menu.querySelectorAll(".menu-item")).filter(function (item) {
      return !item.hidden;
    });
    SETTINGS_INDEX = indexOfAction("settings");
    QUIT_INDEX = indexOfAction("quit");
    setSelected(selected ? items.indexOf(selected) : -1);
  }

  refreshItems();

  window.onPauseMenuShown = function () {
    if (sessionLockoutActive) {
      enterSessionLockoutMode(
        sessionLockoutCode,
        sessionLockoutReason ? sessionLockoutReason.textContent : ""
      );
      return;
    }
    resetToPauseRoot();
    syncPromptVisibility();
    syncServerPanelMapVisibility();
  };

  function paintQuitSaveStatus(state, message) {
    var text = message || "";
    document.querySelectorAll(".quit-save-status").forEach(function (el) {
      el.hidden = !text;
      el.textContent = text;
      if (state && text) {
        el.setAttribute("data-state", state);
      } else {
        el.removeAttribute("data-state");
      }
    });
  }

  window.onPauseMenuQuitStarted = function () {
    quitting = true;
    if (pauseScreen) {
      pauseScreen.classList.add("is-quitting");
    }
  };

  window.onSessionExitSave = function (detail) {
    var state = detail && detail.state;
    if (state !== "saving" && state !== "retrying" && state !== "saved" && state !== "failed") {
      return;
    }
    paintQuitSaveStatus(state, detail.message || "");
  };

  window.onPauseMenuQuitFailed = function (message) {
    lockoutMainMenuPending = false;
    resetToPauseRoot();
    enterQuitConfirmMode();
    paintQuitSaveStatus("failed", message || "Quit did not finish.");
  };

  window.handleControllerAxes = function (leftX, leftY, rightX, rightY) {
    padAxes.lx = Number(leftX) || 0;
    padAxes.ly = Number(leftY) || 0;
    padAxes.rx = Number(rightX) || 0;
    padAxes.ry = Number(rightY) || 0;

    if (viewMode !== "map" || quitting) {
      return;
    }

    if (
      Math.abs(padAxes.lx) > 0.02 ||
      Math.abs(padAxes.ly) > 0.02 ||
      Math.abs(padAxes.ry) > 0.08
    ) {
      ensurePadAxesLoop();
    }
  };

  window.handleControllerInput = function (button, phase) {
    if (phase !== "down") {
      return;
    }

    if (quitting) {
      return;
    }

    if (viewMode === "sessionLockout" || sessionLockoutActive) {
      switch (button) {
        case "dpad_up":
        case "dpad_left":
          moveSessionLockoutSelection(-1);
          break;
        case "dpad_down":
        case "dpad_right":
          moveSessionLockoutSelection(1);
          break;
        case "a":
          activateSessionLockoutSelected();
          break;
      }
      return;
    }

    if (viewMode === "settings") {
      settingsController.handleControllerInput(button, phase);
      return;
    }

    if (viewMode === "map") {
      switch (button) {
        case "b":
          exitMapMode();
          break;
        case "a":
          activateMapSelect();
          break;
        case "lb":
          cycleMapMarker(-1);
          break;
        case "rb":
          cycleMapMarker(1);
          break;
        default:
          break;
      }
      return;
    }

    if (viewMode === "travelConfirm") {
      switch (button) {
        case "dpad_up":
        case "dpad_left":
          moveTravelConfirmSelection(-1);
          break;
        case "dpad_down":
        case "dpad_right":
          moveTravelConfirmSelection(1);
          break;
        case "a":
          activateTravelConfirmSelected();
          break;
        case "b":
          exitTravelConfirmMode();
          break;
      }
      return;
    }

    if (viewMode === "quitConfirm") {
      switch (button) {
        case "dpad_up":
        case "dpad_left":
          moveQuitConfirmSelection(-1);
          break;
        case "dpad_down":
        case "dpad_right":
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
        requestResume();
        break;
    }
  };

  if (mapBackButton) {
    mapBackButton.addEventListener("click", function (event) {
      event.stopPropagation();
      exitMapMode();
    });
  }

  if (mapWorld) {
    mapWorld.addEventListener("click", function (event) {
      if (viewMode !== "map" || quitting) {
        return;
      }
      if (didPanThisGesture) {
        didPanThisGesture = false;
        return;
      }
      var target = event.target;
      if (target && target.closest && target.closest(".marker--location")) {
        return;
      }
      clearMapMarkerSelection();
    });
  }

  if (mapWorld) {
    mapWorld.classList.add("is-blurred");
  }

  if (pauseScreen && mapWorld) {
    var MAX_ZOOM = 4;
    var ZOOM_STEP = 0.18;
    var ZOOM_SMOOTHING = 0.18;
    var ZOOM_SNAP_EPSILON = 0.001;
    var PAN_SMOOTHING = 0.28;
    var PAN_SNAP_EPSILON = 0.15;
    var PAN_INERTIA_FRICTION = 0.92;
    var PAN_INERTIA_MIN_SPEED = 0.35;
    var PAN_VELOCITY_IDLE_MS = 80;
    var zoom = 1;
    var targetZoom = 1;
    var MIN_ZOOM = 1;
    var panX = 0;
    var panY = 0;
    var targetPanX = 0;
    var targetPanY = 0;
    var velocityX = 0;
    var velocityY = 0;
    var isDragging = false;
    var lastX = 0;
    var lastY = 0;
    var lastMoveTime = 0;
    var zoomRafId = 0;
    var panRafId = 0;
    var zoomFocusScreenX = 0;
    var zoomFocusScreenY = 0;
    var zoomFocusWorldX = 0;
    var zoomFocusWorldY = 0;

    var mapImg = new Image();
    var mapNaturalWidth = 1024;
    var mapNaturalHeight = 768;
    var mapDisplayWidth = 1024;
    var mapDisplayHeight = 768;

    mapImg.onload = function () {
      mapNaturalWidth = mapImg.naturalWidth;
      mapNaturalHeight = mapImg.naturalHeight;
      updateMapDisplayBounds();
    };
    mapImg.src = "assets/maps/commonwealth/map.png";

    function updateMapDisplayBounds() {
      var viewportWidth = window.innerWidth;
      var viewportHeight = window.innerHeight;
      var mapAspect = mapNaturalWidth / mapNaturalHeight;
      var viewportAspect = viewportWidth / viewportHeight;

      if (mapAspect > viewportAspect) {
        mapDisplayWidth = viewportWidth;
        mapDisplayHeight = viewportWidth / mapAspect;
      } else {
        mapDisplayHeight = viewportHeight;
        mapDisplayWidth = viewportHeight * mapAspect;
      }

      if (mapMarkers) {
        mapMarkers.style.width = mapDisplayWidth + "px";
        mapMarkers.style.height = mapDisplayHeight + "px";
        mapMarkers.style.left = (viewportWidth - mapDisplayWidth) / 2 + "px";
        mapMarkers.style.top = (viewportHeight - mapDisplayHeight) / 2 + "px";
        mapMarkers.style.right = "auto";
        mapMarkers.style.bottom = "auto";
      }

      MIN_ZOOM = Math.max(
        viewportWidth / mapDisplayWidth,
        viewportHeight / mapDisplayHeight
      );

      if (zoom < MIN_ZOOM) {
        zoom = MIN_ZOOM;
        targetZoom = MIN_ZOOM;
        panX = 0;
        panY = 0;
        applyMapTransform();
      }
    }

    updateMapDisplayBounds();
    window.addEventListener("resize", updateMapDisplayBounds);

    function applyMapTransform() {
      // Markers stay mostly screen-constant, but grow ~45% from min→max zoom
      // so icons/names stay readable when fully zoomed in.
      var zoomRange = Math.max(0.001, MAX_ZOOM - MIN_ZOOM);
      var zoomT = Math.max(0, Math.min(1, (zoom - MIN_ZOOM) / zoomRange));
      var markerScreenScale = 1 + 0.45 * zoomT;
      pauseScreen.style.setProperty("--map-zoom", String(zoom));
      pauseScreen.style.setProperty(
        "--map-marker-scale",
        String(markerScreenScale / zoom)
      );
      pauseScreen.style.setProperty("--map-pan-x", panX + "px");
      pauseScreen.style.setProperty("--map-pan-y", panY + "px");
    }

    applyMapTransform();

    function getPanLimits() {
      var zoomedMapWidth = mapDisplayWidth * zoom;
      var zoomedMapHeight = mapDisplayHeight * zoom;
      var viewportWidth = window.innerWidth;
      var viewportHeight = window.innerHeight;
      return {
        maxX: Math.max(0, (zoomedMapWidth - viewportWidth) / 2),
        maxY: Math.max(0, (zoomedMapHeight - viewportHeight) / 2),
      };
    }

    function clampPanValue(x, y) {
      var limits = getPanLimits();
      return {
        x: Math.max(-limits.maxX, Math.min(limits.maxX, x)),
        y: Math.max(-limits.maxY, Math.min(limits.maxY, y)),
      };
    }

    function clampPan() {
      var clamped = clampPanValue(panX, panY);
      panX = clamped.x;
      panY = clamped.y;
    }

    function clampTargetPan() {
      var clamped = clampPanValue(targetPanX, targetPanY);
      targetPanX = clamped.x;
      targetPanY = clamped.y;
    }

    function stopZoomAnimation() {
      if (zoomRafId !== 0) {
        cancelAnimationFrame(zoomRafId);
        zoomRafId = 0;
      }
      targetZoom = zoom;
    }

    function stopPanAnimation() {
      if (panRafId !== 0) {
        cancelAnimationFrame(panRafId);
        panRafId = 0;
      }
      targetPanX = panX;
      targetPanY = panY;
      velocityX = 0;
      velocityY = 0;
    }

    function applyZoomFocusPan() {
      // Keep the focused map point under the cursor while scale changes
      // (transform-origin is center; pan is applied after scale).
      panX = zoomFocusScreenX - zoomFocusWorldX * zoom;
      panY = zoomFocusScreenY - zoomFocusWorldY * zoom;
      clampPan();
      targetPanX = panX;
      targetPanY = panY;
    }

    function tickZoomAnimation() {
      zoomRafId = 0;

      var zoomDelta = targetZoom - zoom;
      if (Math.abs(zoomDelta) <= ZOOM_SNAP_EPSILON) {
        zoom = targetZoom;
      } else {
        zoom += zoomDelta * ZOOM_SMOOTHING;
      }

      applyZoomFocusPan();
      applyMapTransform();

      if (zoom !== targetZoom) {
        zoomRafId = requestAnimationFrame(tickZoomAnimation);
      }
    }

    function startZoomAnimation() {
      stopPanAnimation();
      if (zoomRafId === 0) {
        zoomRafId = requestAnimationFrame(tickZoomAnimation);
      }
    }

    function tickPanAnimation() {
      panRafId = 0;
      var keepGoing = false;

      if (isDragging) {
        var dragDx = targetPanX - panX;
        var dragDy = targetPanY - panY;
        if (Math.abs(dragDx) <= PAN_SNAP_EPSILON && Math.abs(dragDy) <= PAN_SNAP_EPSILON) {
          panX = targetPanX;
          panY = targetPanY;
        } else {
          panX += dragDx * PAN_SMOOTHING;
          panY += dragDy * PAN_SMOOTHING;
          keepGoing = true;
        }
        clampPan();
      } else {
        var nextX = panX + velocityX;
        var nextY = panY + velocityY;
        var clamped = clampPanValue(nextX, nextY);
        panX = clamped.x;
        panY = clamped.y;
        targetPanX = panX;
        targetPanY = panY;

        if (panX !== nextX) {
          velocityX = 0;
        }
        if (panY !== nextY) {
          velocityY = 0;
        }

        velocityX *= PAN_INERTIA_FRICTION;
        velocityY *= PAN_INERTIA_FRICTION;

        if (
          Math.abs(velocityX) > PAN_INERTIA_MIN_SPEED ||
          Math.abs(velocityY) > PAN_INERTIA_MIN_SPEED
        ) {
          keepGoing = true;
        } else {
          velocityX = 0;
          velocityY = 0;
        }
      }

      applyMapTransform();

      if (keepGoing) {
        panRafId = requestAnimationFrame(tickPanAnimation);
      }
    }

    function startPanAnimation() {
      if (panRafId === 0) {
        panRafId = requestAnimationFrame(tickPanAnimation);
      }
    }

    mapWorld.addEventListener(
      "wheel",
      function (event) {
        if (viewMode !== "map") {
          return;
        }
        event.preventDefault();

        var nextTarget = Math.max(
          MIN_ZOOM,
          Math.min(MAX_ZOOM, targetZoom + (event.deltaY > 0 ? -ZOOM_STEP : ZOOM_STEP))
        );
        if (nextTarget === targetZoom && zoom === targetZoom) {
          return;
        }

        // Anchor on the map point currently under the cursor.
        zoomFocusScreenX = event.clientX - window.innerWidth / 2;
        zoomFocusScreenY = event.clientY - window.innerHeight / 2;
        zoomFocusWorldX = (zoomFocusScreenX - panX) / zoom;
        zoomFocusWorldY = (zoomFocusScreenY - panY) / zoom;
        targetZoom = nextTarget;
        startZoomAnimation();
      },
      { passive: false }
    );

    mapWorld.addEventListener("mousedown", function (event) {
      if (viewMode !== "map" || zoom <= MIN_ZOOM) {
        return;
      }
      stopZoomAnimation();
      isDragging = true;
      didPanThisGesture = false;
      targetPanX = panX;
      targetPanY = panY;
      velocityX = 0;
      velocityY = 0;
      lastX = event.clientX;
      lastY = event.clientY;
      lastMoveTime = performance.now();
      pauseScreen.classList.add("is-panning");
      startPanAnimation();
    });

    window.addEventListener("mousemove", function (event) {
      if (!isDragging) {
        return;
      }

      var dx = event.clientX - lastX;
      var dy = event.clientY - lastY;
      var now = performance.now();

      if (dx !== 0 || dy !== 0) {
        didPanThisGesture = true;
      }

      targetPanX += dx;
      targetPanY += dy;
      clampTargetPan();

      // Blend recent movement into release velocity for inertia.
      velocityX = velocityX * 0.55 + dx * 0.45;
      velocityY = velocityY * 0.55 + dy * 0.45;

      lastX = event.clientX;
      lastY = event.clientY;
      lastMoveTime = now;
      startPanAnimation();
    });

    window.addEventListener("mouseup", function () {
      if (!isDragging) {
        return;
      }

      isDragging = false;
      pauseScreen.classList.remove("is-panning");

      // If the pointer stopped before release, do not coast.
      if (performance.now() - lastMoveTime > PAN_VELOCITY_IDLE_MS) {
        velocityX = 0;
        velocityY = 0;
      }

      // Fold any remaining ease-catchup into the coast.
      velocityX += (targetPanX - panX) * 0.2;
      velocityY += (targetPanY - panY) * 0.2;
      targetPanX = panX;
      targetPanY = panY;
      startPanAnimation();
    });

    mapControls = {
      keyPanSpeed: 28,
      panBy: function (dx, dy) {
        if (viewMode !== "map") {
          return;
        }
        stopZoomAnimation();
        isDragging = false;
        velocityX = 0;
        velocityY = 0;
        targetPanX = panX + dx;
        targetPanY = panY + dy;
        clampTargetPan();
        panX = targetPanX;
        panY = targetPanY;
        applyMapTransform();
      },
      zoomBy: function (steps) {
        if (viewMode !== "map") {
          return;
        }
        var nextTarget = Math.max(
          MIN_ZOOM,
          Math.min(MAX_ZOOM, targetZoom + steps * ZOOM_STEP)
        );
        if (nextTarget === targetZoom && zoom === targetZoom) {
          return;
        }
        // Zoom toward viewport center for stick/keyboard.
        zoomFocusScreenX = 0;
        zoomFocusScreenY = 0;
        zoomFocusWorldX = (zoomFocusScreenX - panX) / zoom;
        zoomFocusWorldY = (zoomFocusScreenY - panY) / zoom;
        targetZoom = nextTarget;
        startZoomAnimation();
      },
      applyStickInput: function (lx, ly, ry) {
        if (viewMode !== "map") {
          return false;
        }

        var active = false;
        if (Math.abs(lx) > 0.02 || Math.abs(ly) > 0.02) {
          // Pan speed scales with zoom so travel feels even when zoomed in.
          // Stick moves the viewport (look direction), opposite of mouse-drag.
          // XInput Y is up-positive; map panY grows downward.
          var speed = 18 * Math.max(1, zoom);
          mapControls.panBy(-lx * speed, ly * speed);
          active = true;
        }

        if (Math.abs(ry) > 0.08) {
          // Direct zoom nudge each frame while RS is held (center-anchored).
          var zoomDelta = ry * 0.035;
          var nextTarget = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, targetZoom + zoomDelta));
          if (nextTarget !== targetZoom || zoom !== targetZoom) {
            zoomFocusScreenX = 0;
            zoomFocusScreenY = 0;
            zoomFocusWorldX = (zoomFocusScreenX - panX) / Math.max(zoom, 0.001);
            zoomFocusWorldY = (zoomFocusScreenY - panY) / Math.max(zoom, 0.001);
            targetZoom = nextTarget;
            startZoomAnimation();
            active = true;
          }
        }

        return active;
      },
      centerOnMarkerPct: function (xPct, yPct) {
        if (viewMode !== "map" && viewMode !== "travelConfirm") {
          return;
        }
        stopZoomAnimation();
        stopPanAnimation();
        var worldX = (xPct / 100 - 0.5) * mapDisplayWidth;
        var worldY = (yPct / 100 - 0.5) * mapDisplayHeight;
        var nextPan = clampPanValue(-worldX * zoom, -worldY * zoom);
        panX = nextPan.x;
        panY = nextPan.y;
        targetPanX = panX;
        targetPanY = panY;
        velocityX = 0;
        velocityY = 0;
        applyMapTransform();
      }
    };
  }

  syncPromptVisibility();
})();
