(function () {
  "use strict";

  function create(options) {
    if (!options || !options.mount) {
      throw new Error("CO_Browser.create requires a mount element");
    }
    var mountEl = options.mount;
    var layout = options.layout || "embedded";
    var onBack = typeof options.onBack === "function" ? options.onBack : function () {};
    var exposeGlobals = options.exposeGlobals !== false;
    var captureKeys = options.captureKeys === true;
    var visible = false;

    if (!mountEl.querySelector("#coRoot")) {
      mountEl.innerHTML = CO_BrowserMarkup.build(layout);
    }

    var root = mountEl.querySelector("#coRoot");
    var closeSelectMenu = window.CO_SelectMenus ? CO_SelectMenus.bind(root) : function () {};
    var panel = mountEl.querySelector("#coPanel");
    var joinOverlay = mountEl.querySelector("#coJoinOverlay");
    var joinMessage = mountEl.querySelector("#coJoinMessage");
    var modpackCopy = window.CO_ModpackCopy
      ? window.CO_ModpackCopy.create({
        joinOverlay: joinOverlay,
        button: mountEl.querySelector("#coCopyModpackButton"),
        notice: mountEl.querySelector("#coCopyModpackNotice"),
        onDismiss: function () { if (state.joinOverlay && state.joinOverlay.error) hideJoinOverlay(); }
      })
      : null;

    if (!root || !panel || !joinOverlay || !joinMessage) {
      throw new Error("CO_Browser.create failed to mount browser markup");
    }

    var portNumFrom = CO_ServerRecords.portNumFrom;
    var selectionArmed = false;

    var state = {
      activeTab: "server-browser",
      focusZone: "server-list",
      selectedTabIndex: 1,
      selectedServerIndex: 0,
      selectedFilterIndex: 0,
      filters: CO_FilterState.defaults(),
      allServers: [],
      servers: [],
      favorites: [],
      recent: [],
      localServers: [],
      connectionStatus: "Online",
      directoryNotice: "",
      isRefreshing: false,
      isScanningLocal: false,
      directConnect: { address: "127.0.0.1", port: "7777" },
      directConnectFieldIndex: 0,
      retryAvailable: false,
      joinOverlay: null, sessionLifecycleSeen: false
    };

    var steamFriends = CO_SteamFriends.create({ root: root, state: state, render: render, sendAction: sendAction, showJoinOverlay: showJoinOverlay });
    var serverSettings = window.CO_ServerSettings
      ? CO_ServerSettings.create({ root: root, state: state, render: render, sendAction: sendAction })
      : null;
    state.steamP2pAvailable = options && typeof options.steamP2pAvailable === "boolean" ? options.steamP2pAvailable : window.CO_ENABLE_MOCKS === true;

    function showBridgeError(action) {
      state.connectionStatus = "Unavailable";
      state.isRefreshing = false;
      state.isScanningLocal = false;
      state.allServers = [];
      state.localServers = [];
      showJoinOverlay("Server browser bridge unavailable.", true);
      render();
      console.error("[CO] coAction bridge unavailable:", action);
    }

    function sendAction(action, payload) {
      var msg = JSON.stringify(Object.assign({ action: action }, payload || {}));
      if (typeof window.coAction === "function") {
        window.coAction(msg);
        return;
      }
      if (window.CO_ENABLE_MOCKS === true) {
        handleActionLocal(action, payload || {});
        return;
      }
      showBridgeError(action);
    }

    function handleActionLocal(action, payload) {
      switch (action) {
        case "refreshServerList":
          refreshLocalMock();
          break;
        case "scanLocalServers":
          scanLocalMock();
          break;
        case "setFilter":
          state.filters[payload.filterName] = payload.value;
          applyLocalFilters();
          render();
          break;
        case "resetFilters":
          resetLocalFilters();
          break;
        case "toggleFavorite":
          toggleFavoriteLocal(payload.serverId);
          break;
        case "steamHostStart":
          state.steamHost = { status: "hosted", friendCode: "76561198012345678", reason: "" };
          dispatchCoEvent({ type: "steamLobbyPeers", peers: [{ steamId: "76561198012345679", name: "Mock Friend" }] });
          break;
        case "steamHostEnter":
          showJoinOverlay("Entering the world...", false);
          break;
        case "steamHostStop":
          dispatchCoEvent({ type: "steamHostStopped" });
          break;
        case "steamJoin":
          showJoinOverlay("Connecting to Steam host...", false);
          break;
        case "joinServer":
          joinFlow.joinServerById(payload.serverId);
          break;
        case "openDirectConnect":
          joinFlow.joinDirect(payload.address, payload.port);
          break;
        case "switchTab":
          switchTab(payload.tabName, { skipAction: true });
          if (payload.tabName === "local" || payload.tabName === "steam-friends") {
            scanLocalMock();
          }
          break;
        case "closeBrowser":
          if (typeof window.closeBrowser === "function") {
            window.closeBrowser();
          } else {
            hide();
            onBack();
          }
          break;
        default:
          break;
      }
    }

    function applyDirectoryServers(list) {
      state.allServers = (list || []).map(CO_ServerRecords.normalizeDirectoryServer).filter(Boolean);
      applyLocalFilters();
    }

    function refreshLocalMock() {
      if (window.CO_MockData && window.CO_MockData.refreshDirectory) {
        window.CO_MockData.refreshDirectory(state, applyDirectoryServers, render);
      }
    }

    function scanLocalMock() {
      if (window.CO_MockData && window.CO_MockData.scanLocal) {
        window.CO_MockData.scanLocal(state, render);
      }
    }

    function resetLocalFilters() {
      state.filters = CO_FilterState.defaults();
      sendAction("resetFilters", {});
      applyLocalFilters();
      render();
    }

    function applyLocalFilters() {
      CO_FilterState.apply(state);
      state.selectedServerIndex = Math.min(state.selectedServerIndex, Math.max(0, getActiveList().length - 1));
      CO_ServerCounts.updateServer(root, state);
    }

    function toggleFavoriteLocal(serverId) {
      var server = findServerById(serverId);
      if (!server) {
        return;
      }
      server.favorite = !server.favorite;
      sendAction("toggleFavorite", { serverId: serverId });
      applyLocalFilters();
      render();
    }

    function findServerById(id) {
      for (var i = 0; i < state.allServers.length; i++) {
        if (state.allServers[i].id === id) {
          return state.allServers[i];
        }
      }
      return null;
    }

    function findLocalServerById(id) {
      for (var i = 0; i < state.localServers.length; i++) {
        if (state.localServers[i].id === id) {
          return state.localServers[i];
        }
      }
      return null;
    }

    function getSelectedServer() {
      return getActiveList()[state.selectedServerIndex] || null;
    }

    function getActiveList() {
      if (state.activeTab === "favorites") return state.favorites;
      if (state.activeTab === "recent") return state.recent;
      if (state.activeTab === "local") return state.localServers;
      return state.servers;
    }

    function showJoinOverlay(message, isError) {
      state.joinOverlay = { message: message, error: !!isError };
      joinOverlay.classList.remove("hidden");
      if (isError) joinOverlay.classList.add("co-error");
      else joinOverlay.classList.remove("co-error");
      var modpack = CO_ModpackDetail.split(message);
      joinMessage.textContent = modpack.headline;
      CO_ModpackDetail.show(root, modpack);
      if (modpackCopy) modpackCopy.show(modpack.lines, isError);
      var spinner = joinOverlay.querySelector(".co-spinner");
      if (spinner) spinner.style.display = isError ? "none" : "block";
      render();
    }

    function hideJoinOverlay() {
      state.joinOverlay = null;
      joinOverlay.classList.add("hidden");
      if (modpackCopy) modpackCopy.hide();
      render();
    }

    function cancelJoin() {
      showJoinOverlay("Cancelling...", false);
      sendAction("cancelJoin", {});
    }

    function retryLastJoin() {
      showJoinOverlay("Reconnecting...");
      sendAction("retryLastJoin", {});
    }

    function exitBrowser() {
      hide();
      onBack();
      if (layout !== "embedded") {
        sendAction("closeBrowser", {});
      }
      if (typeof window.closeBrowser === "function") {
        window.closeBrowser();
      }
    }

    function switchTab(tabName, switchOptions) {
      closeSelectMenu(false);
      switchOptions = switchOptions || {};
      if (tabName === "local") tabName = "steam-friends";
      if (tabName !== "steam-friends") steamFriends.tabClosed();
      state.activeTab = tabName;
      state.selectedTabIndex = CO_Tabs.TAB_ORDER.indexOf(tabName);
      if (state.selectedTabIndex < 0) state.selectedTabIndex = 0;
      releaseFieldFocus();
      if (tabName === "server-browser") state.focusZone = "server-list";
      else if (tabName === "direct-connect") {
        state.focusZone = "tabs";
        state.directConnectFieldIndex = 0;
      } else if (tabName === "steam-friends") {
        state.focusZone = "tabs";
        state.steamFieldIndex = 0;
        steamFriends.tabOpened();
      } else if (tabName === "favorites" || tabName === "recent" || tabName === "local") {
        var list = state.servers;
        if (tabName === "favorites") list = state.favorites;
        else if (tabName === "recent") list = state.recent;
        else if (tabName === "local") list = state.localServers;
        if (state.selectedServerIndex >= list.length) {
          state.selectedServerIndex = Math.max(0, list.length - 1);
        }
        state.focusZone = "server-list";
      }
      if (!switchOptions.skipAction) {
        sendAction("switchTab", { tabName: tabName });
      }
      render();
    }

    function render() {
      if (!root) return;

      panel.classList.toggle("co-loading", state.isRefreshing || state.isScanningLocal);

      state.keyboardOpen = CO_Keyboard.isOpen();
      CO_Keyboard.render(root);
      CO_Tabs.render(root, state);
      CO_ControllerPrompts.render(root, state);

      var statusEl = root.querySelector("#coConnectionStatus");
      if (statusEl) {
        statusEl.textContent = String(state.connectionStatus || "Online").toUpperCase();
      }

      root.querySelectorAll(".co-tab-pane").forEach(function (pane) {
        pane.classList.toggle("active", pane.getAttribute("data-pane") === state.activeTab);
      });

      if (state.activeTab === "direct-connect") {
        CO_DirectConnect.render(root, state);
      } else if (state.activeTab === "steam-friends") {
        CO_SteamFriends.render(root, state);
        if (serverSettings) serverSettings.ensureLoaded();
      } else {
        var listServers = state.servers;
        if (state.activeTab === "favorites") listServers = state.favorites;
        if (state.activeTab === "recent") listServers = state.recent;
        if (state.activeTab === "local") listServers = state.localServers;

        var listState = Object.assign({}, state, { servers: listServers });
        if (state.activeTab === "favorites" || state.activeTab === "recent" || state.activeTab === "local") {
          listState.focusZone = state.focusZone === "filters" ? "server-list" : state.focusZone;
        }

        if (state.activeTab === "server-browser") {
          CO_Filters.render(root, listState);
        }

        if (state.activeTab !== "direct-connect") {
          CO_ServerDetails.render(root, listState);
        }

        var activePane = root.querySelector('.co-tab-pane[data-pane="' + state.activeTab + '"]');
        if (activePane) {
          var listEl = activePane.querySelector(".co-server-list");
          if (listEl) {
            var tempRoot = {
              querySelector: function (sel) {
                if (sel === ".co-server-list-wrap") return activePane.querySelector(".co-server-list-wrap");
                if (sel === ".co-server-list") return listEl;
                return root.querySelector(sel);
              }
            };
            CO_ServerList.render(tempRoot, listState);
          }
        }
      }

      CO_ServerCounts.updateServer(root, state);
      CO_ServerCounts.updateLocal(root, state);
    }

    function bindUi() {
      CO_TextFocus.bind(root);
      window.addEventListener("prisma-controller-action", onPrismaControllerAction);
      window.addEventListener("prisma-input-device-change", function (event) {
        var detail = event.detail || {};
        if (window.CO_ControllerGlyphs) CO_ControllerGlyphs.setPlatform(detail.style === 1 ? "Playstation" : "Xbox");
        setPointerMode(detail.gamepad ? "gamepad" : "mouse");
      });
      if (window.CO_Hover) {
        CO_Hover.bind(root);
      }

      CO_Tabs.bind(root, function (tabName) {
        switchTab(tabName);
      });

      CO_Filters.bind(root, function (name, value) {
        state.filters[name] = value;
        sendAction("setFilter", { filterName: name, value: value });
        applyLocalFilters();
        render();
      }, resetLocalFilters);

      root.addEventListener("click", function (e) {
        if (e.target && e.target.closest("#coBackButton, [data-action='back']")) {
          exitBrowser();
          return;
        }
        var row = e.target.closest(".co-tab-pane.active .co-server-row");
        if (!row) return;
        var index = parseInt(row.getAttribute("data-index"), 10);
        if (isNaN(index)) return;

        if (selectionArmed && state.selectedServerIndex === index) {
          joinSelected();
          return;
        }

        state.selectedServerIndex = index;
        selectionArmed = true;
        updateServerSelectionInPlace(index);
      });

      root.addEventListener("dblclick", function (e) {
        var row = e.target.closest(".co-tab-pane.active .co-server-row");
        if (!row) return;
        var index = parseInt(row.getAttribute("data-index"), 10);
        if (isNaN(index)) return;
        state.selectedServerIndex = index;
        selectionArmed = true;
        joinSelected();
      });

      CO_DirectConnect.bind(root, function (address, port, password) {
        state.directConnect = { address: address, port: port };
        joinFlow.joinDirect(address, port, password);
      }, function (address, port) {
        state.directConnect = { address: address, port: port };
      }, retryLastJoin);

      if (modpackCopy) modpackCopy.bind();

      if (serverSettings && window.CO_ServerSettings) {
        CO_ServerSettings.bind(root, {
          onSave: serverSettings.save,
          onReload: serverSettings.reload,
          onToggle: serverSettings.toggleField,
          onSelect: serverSettings.selectField,
          onText: serverSettings.textField
        });
      }

      if (captureKeys) {
        document.addEventListener("keydown", onKeyDown);
      }
    }

    function releaseFieldFocus() {
      if (CO_Keyboard.isOpen()) CO_Keyboard.close();
      var active = document.activeElement;
      if (CO_ControllerInput.isKeyboardCaptureTarget(active) && active.blur) active.blur();
    }

    function onPrismaControllerAction(event) {
      CO_ControllerInput.onPrismaControllerAction(event, visible, state, setPointerMode, handleControllerButton);
    }

    function onKeyDown(e) {
      if (!visible) {
        return;
      }
      if (state.joinOverlay && state.joinOverlay.error) {
        if (e.key === "Escape" || e.key === "Enter" || e.key === "b" || e.key === "B") {
          hideJoinOverlay();
          return;
        }
      }
      if (e.repeat && !CO_ControllerInput.isDirectionalButton(mapKeyToButton(e.key))) return;

      if (CO_ControllerInput.isKeyboardCaptureTarget(e.target)) {
        if (e.key === "Escape") {
          handleControllerButton("b", "down");
        } else if (e.key === "Enter" && (state.activeTab === "direct-connect" || passwordPrompt.isOpen())) {
          handleControllerButton("a", "down", { confirmText: true });
        }
        return;
      }

      handleControllerButton(mapKeyToButton(e.key), "down");
    }

    function mapKeyToButton(key) {
      return CO_ControllerInput.mapKeyToButton(key);
    }

    function handleControllerButton(button, phase, options) {
      if (!visible) return;
      if (passwordPrompt.isOpen() && !CO_Keyboard.isOpen()) {
        passwordPrompt.handleButton(button, phase, options);
        return;
      }
      controls.handleButton(button, phase, options);
    }

    function updateServerSelectionInPlace(index) {
      var activePane = root.querySelector('.co-tab-pane[data-pane="' + state.activeTab + '"]');
      if (!activePane) {
        render();
        return;
      }

      var listEl = activePane.querySelector(".co-server-list");
      if (!listEl) {
        render();
        return;
      }

      listEl.querySelectorAll(".co-server-row").forEach(function (row) {
        var rowIndex = parseInt(row.getAttribute("data-index"), 10);
        row.classList.toggle("selected", rowIndex === index);
      });

      var listServers = getActiveList();
      var listState = Object.assign({}, state, { servers: listServers });
      if (window.CO_ServerDetails) {
        CO_ServerDetails.render(root, listState);
      }
      if (window.CO_ServerList && CO_ServerList.scrollToSelected) {
        CO_ServerList.scrollToSelected(activePane, index);
      }
    }

    function joinSelected() {
      if (state.activeTab === "steam-friends") return;
      var sel = getSelectedServer();
      if (!sel) return;
      joinFlow.joinServerEntry(sel);
    }

    function dispatchCoEvent(event) {
      if (!event || !event.type) return;

      switch (event.type) {
        case "serverListUpdated":
          state.allServers = event.servers || [];
          selectionArmed = false;
          applyLocalFilters();
          break;
        case "serverListRefreshStarted":
          state.isRefreshing = true;
          selectionArmed = false;
          break;
        case "serverListRefreshFinished":
          state.isRefreshing = false;
          if (!event.success) {
            state.connectionStatus = "Directory unavailable";
            state.directoryNotice = event.error || "";
            if (event.error) {
              console.warn("[CO] Refresh failed:", event.error);
            }
          } else {
            state.connectionStatus = "Online";
            state.directoryNotice = "";
          }
          render();
          break;
        case "localScanStarted":
          state.isScanningLocal = true;
          break;
        case "localServersUpdated":
          state.localServers = event.servers || [];
          if (state.activeTab === "local" && state.selectedServerIndex >= state.localServers.length) {
            state.selectedServerIndex = Math.max(0, state.localServers.length - 1);
          }
          break;
        case "localScanFinished":
          state.isScanningLocal = false;
          if (!event.success) {
            console.warn("[CO] Local scan found no servers.", event.error || "");
          }
          break;
        case "joinStarted":
          state.sessionLifecycleSeen = false; showJoinOverlay("Connecting...");
          break;
        case "joinLoading":
          showJoinOverlay(event.message || "Loading...");
          break;
        case "joinFailed":
          showJoinOverlay(event.reason || "Connection failed", true);
          break;
        case "joinPasswordRequired":
          hideJoinOverlay();
          passwordPrompt.show(event.serverId, event.name);
          break;
        case "sessionState": state.sessionLifecycleSeen = true; if (window.CO_SessionState) CO_SessionState.apply(event, { show: showJoinOverlay, hide: hideJoinOverlay }); break;
        case "recentServersUpdated":
          CO_RecentServers.apply(state, event.servers || []);
          break;
        case "joinSucceeded":
          if (!state.sessionLifecycleSeen) hideJoinOverlay();
          if (event.server) {
            CO_RecentServers.add(state, event.server, findServerById);
          } else if (event.serverId) {
            CO_RecentServers.add(state, event.serverId, findServerById);
          }
          break;
        case "retryAvailable":
          state.retryAvailable = !!event.available;
          break;
        case "connectionStatusChanged":
          state.connectionStatus = event.status || "Online";
          render();
          break;
        case "uiTheme":
          if (typeof window.applyCoTheme === "function") {
            window.applyCoTheme(event);
          }
          break;
        case "menuOpened":
          state.selectedTabIndex = Math.max(0, CO_Tabs.TAB_ORDER.indexOf(state.activeTab));
          break;
        case "menuClosed":
          hideJoinOverlay();
          break;
        case "uiSound":
          break;
        case "shareLogResult":
          if (shareLog) { shareLog.show(event); }
          break;
        case "serverSettingsResult":
          if (serverSettings) serverSettings.applyResult(event);
          break;
        case "capabilities":
          state.steamP2pAvailable = event.steamP2p === true;
          if (!state.steamP2pAvailable && state.activeTab === "steam-friends") switchTab("server-browser");
          state.selectedTabIndex = CO_Tabs.TAB_ORDER.indexOf(state.activeTab);
          break;
        default:
          break;
      }
      steamFriends.handleEvent(event, showJoinOverlay);
      render();
    }

    function setConnectionStatus(connected) {
      state.connectionStatus = connected ? "Connected" : "Online";
      render();
    }

    function setPointerMode(mode) {
      if (!root) return;
      var gamepad = mode === "gamepad";
      root.classList.toggle("co-gamepad-mode", gamepad);
      if (window.CO_ControllerIcons) CO_ControllerIcons.setInputMode(mode);
      if (!gamepad && CO_Keyboard.isOpen()) CO_Keyboard.close();
      render();
      if (window.CO_Hover && CO_Hover.clear) {
        CO_Hover.clear();
      }
    }

    function onShown() {
      if (window.CO_Layout && CO_Layout.update) {
        CO_Layout.update();
      }
      dispatchCoEvent({ type: "menuOpened" });
      render();
    }

    function show() {
      visible = true;
      if (state.activeTab === "steam-friends") steamFriends.tabOpened();
      render();
    }

    function hide() {
      visible = false;
      steamFriends.tabClosed();
      closeSelectMenu(false);
      hideJoinOverlay();
      releaseFieldFocus();
    }

    function handleKeydown(event) {
      if (!visible) {
        return false;
      }
      onKeyDown(event);
      return true;
    }

    function handleResize() {
      if (window.CO_Layout && CO_Layout.update) {
        CO_Layout.update();
      }
      render();
    }

    var joinFlow = CO_JoinFlow.create({
      sendAction: sendAction,
      showJoinOverlay: showJoinOverlay,
      findServerById: findServerById,
      findLocalServerById: findLocalServerById,
      getActiveList: getActiveList
    });

    var passwordPrompt = CO_PasswordPrompt.create({
      root: root,
      state: state,
      render: render,
      sendJoin: function (serverId, password) {
        sendAction("joinWithPassword", { serverId: serverId, password: password });
      }
    });

    var shareLog = window.CO_ShareLog ? window.CO_ShareLog.create({ root: root, sendAction: sendAction }) : null;

    var controls = CO_ControllerNavigation.create({
      state: state,
      root: root,
      getActiveList: getActiveList,
      getSelectedServer: getSelectedServer,
      render: render,
      sendAction: sendAction,
      cancelJoin: cancelJoin,
      retryLastJoin: retryLastJoin,
      hideJoinOverlay: hideJoinOverlay,
      exitBrowser: exitBrowser,
      switchTab: switchTab,
      releaseFieldFocus: releaseFieldFocus,
      joinDirect: joinFlow.joinDirect,
      joinSelected: joinSelected,
      steamFriends: steamFriends,
      toggleFavorite: toggleFavoriteLocal,
      applyLocalFilters: applyLocalFilters,
      resetLocalFilters: resetLocalFilters,
      armSelection: function () {
        selectionArmed = true;
      }
    });

    if (exposeGlobals) {
      window.dispatchCoEvent = dispatchCoEvent;
      window.setConnectionStatus = setConnectionStatus;
      window.onBrowserShown = onShown;
      window.CO_App = window.CO_App || {};
      window.CO_App.joinSelected = joinSelected;
      window.CO_App.getState = function () { return state; };
      window.CO_App.setPointerMode = setPointerMode;
    }

    CO_RecentServers.load(state);
    state.allServers = [];
    applyLocalFilters();
    bindUi();
    window.addEventListener("resize", handleResize);
    render();
    if (typeof window.coAction !== "function" && typeof window.CO_ENABLE_MOCKS === "boolean" && window.CO_ENABLE_MOCKS) {
      refreshLocalMock();
    }

    return {
      show: show,
      hide: hide,
      onShown: onShown,
      isVisible: function () { return visible; },
      handleKeydown: handleKeydown,
      handleControllerInput: handleControllerButton,
      render: render,
      dispatchCoEvent: dispatchCoEvent,
      setPointerMode: setPointerMode
    };
  }

  window.CO_Browser = {
    create: create
  };
})();
