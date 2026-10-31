(function () {
  "use strict";

  var MAX_PLAYERS = 16;
  var DEFAULT_MAX_PLAYERS = 4;
  var UINT64_MAX = "18446744073709551615";
  var FRIEND_CODE_ERROR = "Friend Code should be a number from the host's Steam profile";
  var HOSTS_REFRESH_MS = 10000;
  var MAX_HOST_ROWS = 50;
  var NOT_AVAILABLE_TEXT = "Steam hosting is not available in this version yet.";

  function cleanText(value, max) {
    if (value == null || typeof value === "object") return "";
    return Array.from(String(value).replace(/[\u0000-\u001f\u007f]/g, " ")).slice(0, max).join("");
  }

  function cleanCount(value, max) {
    var n = Number(value);
    if (!isFinite(n)) return 0;
    return Math.min(max, Math.max(0, Math.floor(n)));
  }

  function normalizeHosts(list) {
    var out = [];
    if (!Array.isArray(list)) return out;
    for (var i = 0; i < list.length && out.length < MAX_HOST_ROWS; i++) {
      var item = list[i];
      if (!item || typeof item !== "object" || typeof item.friendCode !== "string") continue;
      var code = parseFriendCode(item.friendCode);
      if (!code) continue;
      var compatible = item.compatible === true;
      out.push({
        friendCode: code,
        name: cleanText(item.name, 64) || "Steam friend",
        host: cleanText(item.host, 64) || "Server",
        players: cleanCount(item.players, 999),
        maxPlayers: cleanCount(item.maxPlayers, 999),
        build: cleanText(item.build, 64),
        compatible: compatible,
        reason: compatible ? "" : (cleanText(item.reason, 200) || "Host is not compatible with your game.")
      });
    }
    return out;
  }

  function unavailableText(state) {
    if (state.steamUnavailableReason) return "Steam unavailable: " + state.steamUnavailableReason;
    if (state.steamP2pAvailable !== true) return NOT_AVAILABLE_TEXT;
    return "";
  }

  function parseFriendCode(value) {
    var code = value == null ? "" : String(value);
    if (!/^[1-9][0-9]*$/.test(code)) return null;
    if (code.length > UINT64_MAX.length) return null;
    if (code.length === UINT64_MAX.length && code > UINT64_MAX) return null;
    return code;
  }

  function isHosting(state) {
    return !!state.steamHost && state.steamHost.status === "hosted";
  }

  function fieldIds(state) {
    var ids = isHosting(state)
      ? ["co-steam-copy", "co-steam-invite", "co-steam-enter", "co-steam-stop"]
      : ["co-steam-password", "co-steam-host"];
    if (window.CO_ServerSettings) ids = ids.concat(CO_ServerSettings.fieldIds(state));
    ids.push("co-steam-friend-code", "co-steam-join-password", "co-steam-join", "co-steam-hosts-refresh");
    var hosts = state.steamFriendHosts || [];
    for (var i = 0; i < hosts.length; i++) ids.push("co-friend-host-" + i);
    if (window.CO_SteamLan) ids = ids.concat(CO_SteamLan.extraFieldIds(state));
    return ids;
  }

  function hostStatusText(host) {
    if (host.status === "starting") return "Starting your server...";
    if (host.status === "hosted") return "Your server is running. Share this Friend Code:";
    if (host.status === "failed") {
      return host.reason ? "Could not start hosting: " + host.reason : "Could not start hosting. Try again.";
    }
    if (host.status === "stopped") return "Hosting stopped.";
    return "";
  }

  function create(deps) {
    var root = deps.root;
    var state = deps.state;
    var rerender = deps.render;
    var sendAction = deps.sendAction;
    var showJoinOverlay = deps.showJoinOverlay;
    if (!state.steam) {
      state.steam = { name: "", maxPlayers: DEFAULT_MAX_PLAYERS, friendCode: "" };
      state.steamP2pAvailable = false;
      state.steamMaxPlayers = MAX_PLAYERS;
      state.steamFieldIndex = 0;
      state.steamHost = { status: "idle", friendCode: "", reason: "" };
      state.steamJoinError = "";
      state.steamInviteNotice = "";
      state.steamLobbyPeers = [];
      state.steamLobbyError = "";
      state.steamEnterPending = false;
      state.steamUnavailableReason = "";
      state.steamMatchmaking = true;
      state.steamFriendHosts = [];
      state.steamHostsNotice = "";
    }
    var hostsRequestedAt = null;
    var hostsTimer = null;
    var tabIsOpen = false;
    var now = typeof deps.now === "function" ? deps.now : Date.now;

    bind(root, {
      onHostStart: hostStart,
      onHostStop: hostStop,
      onJoin: join,
      onInvite: invite,
      onEnter: enterWorld,
      onScan: function () { sendAction("scanLocalServers", {}); },
      onLan: function (id) { sendAction("joinServer", { serverId: id }); },
      onName: function (value) { state.steam.name = value; },
      onMaxPlayers: function (value) { state.steam.maxPlayers = parseInt(value, 10); },
      onFriendCode: friendCodeChanged,
      onHostsRefresh: function () { requestHosts(true); },
      onHostJoin: joinFriendHost
    });

    function blocked(target) {
      var text = unavailableText(state);
      if (!text) return false;
      state[target] = text;
      rerender();
      return true;
    }

    function requestHosts(manual) {
      if (state.steamP2pAvailable !== true) return false;
      if (state.steamUnavailableReason ? manual !== true : state.steamMatchmaking === false) return false;
      var t = now();
      if (hostsRequestedAt !== null && t >= hostsRequestedAt && t - hostsRequestedAt < HOSTS_REFRESH_MS) {
        if (manual) {
          state.steamHostsNotice = "Friends list was just refreshed. Try again in a few seconds.";
          rerender();
        }
        return false;
      }
      hostsRequestedAt = t;
      state.steamHostsNotice = "";
      sendAction("steamListHosts", {});
      return true;
    }

    function tabOpened() {
      tabIsOpen = true;
      requestHosts(false);
      if (hostsTimer !== null) return;
      var start = typeof deps.setInterval === "function" ? deps.setInterval : (typeof setInterval === "function" ? setInterval : null);
      if (start) hostsTimer = start(function () { requestHosts(false); }, HOSTS_REFRESH_MS);
    }

    function tabClosed() {
      tabIsOpen = false;
      if (hostsTimer === null) return;
      var stop = typeof deps.clearInterval === "function" ? deps.clearInterval : clearInterval;
      stop(hostsTimer);
      hostsTimer = null;
    }

    function hostStart() {
      if (blocked("steamInviteNotice")) return;
      var reason = window.CO_ServerSettings
        ? CO_ServerSettings.hostBlockReason(state)
        : "Set your server name in Server settings, then start hosting again.";
      if (reason) {
        var settings = window.CO_ServerSettings ? CO_ServerSettings.ensureState(state) : null;
        if (settings && !settings.loaded && !settings.loading) {
          settings.loading = true;
          sendAction("serverSettingsGet", {});
        }
        state.steamHost = { status: "failed", friendCode: "", reason: reason };
        rerender();
        return;
      }
      var identity = window.CO_ServerSettings.advertised(state);
      var values = getValues(root);
      state.steam.name = identity.name;
      state.steamInviteNotice = "";
      state.steamHost = { status: "starting", friendCode: "", reason: "" };
      sendAction("steamHostStart", { name: identity.name, password: values.password, maxPlayers: identity.maxPlayers });
      rerender();
    }

    function hostStop() {
      if (blocked("steamInviteNotice")) return;
      state.steamInviteNotice = "";
      sendAction("steamHostStop", {});
    }

    function invite() {
      if (!isHosting(state)) return;
      if (blocked("steamInviteNotice")) return;
      sendAction("steamInvite", {});
    }

    function clearEnterPending() {
      if (state.steamEnterPending !== true) return false;
      state.steamEnterPending = false;
      return true;
    }

    function enterWorld() {
      if (!isHosting(state)) return;
      if (blocked("steamLobbyError")) return;
      if (state.steamEnterPending === true) return;
      state.steamEnterPending = true;
      sendAction("steamHostEnter", {});
      rerender();
    }

    function sendJoin(raw, password) {
      state.steamJoinError = "";
      clearPassword(root, "co-steam-join-password");
      if (typeof showJoinOverlay === "function") {
        showJoinOverlay("Connecting to " + (raw ? "Steam friend " + raw : "a Steam friend") + "...", false);
      }
      sendAction("steamJoin", { address: raw ? "steam:" + raw : "steam:auto", password: password || "" });
      rerender();
    }

    function joinFriendHost(code) {
      if (blocked("steamJoinError")) return;
      var valid = typeof code === "string" ? parseFriendCode(code) : null;
      if (!valid) {
        state.steamJoinError = FRIEND_CODE_ERROR;
        rerender();
        return;
      }
      var listed = (state.steamFriendHosts || []).filter(function (entry) { return entry.friendCode === valid; })[0];
      if (listed && listed.compatible !== true) {
        state.steamJoinError = listed.reason || "Host is not compatible with your game.";
        rerender();
        return;
      }
      sendJoin(valid, getValues(root).joinPassword);
    }

    function join() {
      if (blocked("steamJoinError")) return;
      var values = getValues(root, state.steamMaxPlayers);
      var raw = (values.friendCode || "").trim();
      if (/^[0-9]+$/.test(raw) && !parseFriendCode(raw)) {
        state.steamJoinError = FRIEND_CODE_ERROR;
        rerender();
        return;
      }
      if (raw.length > 64) {
        state.steamJoinError = "Friend Code or name must be 64 characters or fewer.";
        rerender();
        return;
      }
      sendJoin(raw, values.joinPassword);
    }

    function friendCodeChanged(value) {
      state.steam.friendCode = value;
      state.steamJoinError = "";
      var errorEl = root.querySelector("#co-steam-join-error");
      if (errorEl) errorEl.textContent = "";
    }

    function applyHostEvent(event) {
      clearEnterPending();
      if (event.type === "steamHostStarted") {
        clearPassword(root, "co-steam-password");
        state.steamHost = {
          status: "hosted",
          friendCode: event.friendCode ? String(event.friendCode) : "",
          reason: ""
        };
        state.steamLobbyPeers = [];
        state.steamLobbyError = "";
        if (state.focusZone === "steam-form") state.steamFieldIndex = fieldIds(state).indexOf("co-steam-enter");
        return;
      }
      if (event.type === "steamHostFailed" && isHosting(state)) {
        state.steamLobbyError = event.reason ? String(event.reason) : "Could not enter the world.";
        return;
      }
      if (event.type === "steamHostFailed") {
        state.steamHost = {
          status: "failed",
          friendCode: "",
          reason: event.reason ? String(event.reason) : ""
        };
        state.steamInviteNotice = "";
        return;
      }
      state.steamHost = { status: "idle", friendCode: "", reason: "" };
      state.steamInviteNotice = "";
      state.steamLobbyPeers = [];
      state.steamLobbyError = "";
    }

    function handleEvent(event, showJoinOverlay) {
      if (!event || !event.type) return false;
      if (event.type === "capabilities") {
        state.steamP2pAvailable = event.steamP2p === true;
        if (tabIsOpen) requestHosts(false);
        return true;
      }
      if (event.type === "steamStatus") {
        state.steamUnavailableReason = event.available === false ? (cleanText(event.reason, 200) || "Steam is not available.") : "";
        state.steamMatchmaking = event.matchmaking !== false;
        if (event.available !== false && tabIsOpen) requestHosts(false);
        return true;
      }
      if (event.type === "steamFriendHosts") {
        state.steamFriendHosts = normalizeHosts(event.hosts);
        state.steamHostsNotice = "";
        return true;
      }
      if (event.type === "joinSucceeded") {
        clearEnterPending();
        return false;
      }
      if (event.type === "steamHostStarted" || event.type === "steamHostFailed" || event.type === "steamHostStopped") {
        applyHostEvent(event);
        return true;
      }
      if (event.type === "steamLobbyPeers") {
        state.steamLobbyPeers = Array.isArray(event.peers) ? event.peers : [];
        return true;
      }
      if (event.type === "steamInviteOpened") {
        state.steamInviteNotice = "Steam invite opened.";
        return true;
      }
      if (event.type === "steamInviteFailed") {
        state.steamInviteNotice = event.reason ? String(event.reason) : "Could not open the Steam invite.";
        return true;
      }
      if (event.type === "steamJoinFailed") {
        state.steamJoinError = event.reason ? String(event.reason) : "Could not join.";
        if (typeof showJoinOverlay === "function") showJoinOverlay(state.steamJoinError, true);
        return true;
      }
      return false;
    }

    return {
      applyHostEvent: applyHostEvent,
      hostStart: hostStart,
      hostStop: hostStop,
      join: join,
      joinFriendHost: joinFriendHost,
      requestHosts: requestHosts,
      tabOpened: tabOpened,
      tabClosed: tabClosed,
      handleEvent: handleEvent
    };
  }

  function render(root, state) {
    var pane = root.querySelector('[data-pane="steam-friends"]');
    if (!pane) return;

    var container = pane.querySelector(".co-steam");
    if (!container) return;

    var ids = fieldIds(state);
    if (state.steamFieldIndex >= ids.length) state.steamFieldIndex = ids.length - 1;
    var focusedId = state.focusZone === "steam-form" ? ids[state.steamFieldIndex] : null;

    function revealFocused() {
      if (!focusedId) return;
      var focused = container.querySelector("#" + focusedId);
      if (focused && typeof focused.scrollIntoView === "function") focused.scrollIntoView({ block: "nearest" });
    }

    function updateFocusClasses() {
      container.querySelectorAll("[data-steam-field]").forEach(function (el) {
        el.classList.toggle("focused", el.getAttribute("data-steam-field") === focusedId);
      });
      container.querySelectorAll("[data-ss-field]").forEach(function (el) {
        el.classList.toggle("focused", el.getAttribute("data-ss-field") === focusedId);
      });
      var previous = container.getAttribute("data-focused-field") || "";
      if (previous && previous !== focusedId) {
        var stale = container.querySelector("#" + previous);
        if (stale) stale.classList.remove("focused");
      }
      var current = focusedId ? container.querySelector("#" + focusedId) : null;
      if (current) current.classList.add("focused");
      if (window.CO_SteamLan) CO_SteamLan.updateList(container, state, focusedId);
      if (previous !== (focusedId || "")) {
        container.setAttribute("data-focused-field", focusedId || "");
        revealFocused();
      }
    }

    function renderSignature() {
      var formState = state.steam || {};
      var hostState = state.steamHost || {};
      var identityState = window.CO_ServerSettings ? CO_ServerSettings.advertised(state) : { name: "", maxPlayers: DEFAULT_MAX_PLAYERS };
      var settingsState = null;
      if (window.CO_ServerSettings) {
        var settings = CO_ServerSettings.ensureState(state);
        settingsState = {
          loaded: settings.loaded === true,
          loading: settings.loading === true,
          values: settings.values,
          draft: settings.draft,
          folder: settings.folder,
          configPath: settings.configPath,
          folderDraft: settings.folderDraft,
          folderDraftSet: settings.folderDraftSet === true,
          errors: settings.errors,
          folderError: settings.folderError,
          status: settings.status,
          limits: settings.limits,
          passwordClear: settings.passwordClear === true,
          worldModeRefusal: settings.worldModeRefusal,
          readable: settings.readable
        };
      }
      return JSON.stringify({
        available: state.steamP2pAvailable === true,
        hosting: isHosting(state),
        hostStatus: hostState.status || "",
        hostFriendCode: hostState.friendCode || "",
        hostReason: hostState.reason || "",
        lobbyPeers: state.steamLobbyPeers || [],
        lobbyError: state.steamLobbyError || "",
        inviteNotice: state.steamInviteNotice || "",
        joinError: state.steamJoinError || "",
        enterPending: state.steamEnterPending === true,
        formFriendCode: formState.friendCode || "",
        identity: identityState,
        localServers: state.localServers || [],
        scanning: state.isScanningLocal === true,
        friendHosts: state.steamFriendHosts || [],
        hostsNotice: state.steamHostsNotice || "",
        unavailableReason: state.steamUnavailableReason || "",
        matchmaking: state.steamMatchmaking !== false,
        settings: settingsState
      });
    }

    var active = document.activeElement;
    var target = window.CO_Keyboard && CO_Keyboard.getTarget ? CO_Keyboard.getTarget() : null;
    if ((container.contains(active) && CO_TextFocus.isFormEntryTarget(active)) || container.contains(target)) {
      updateFocusClasses();
      return;
    }

    var signature = renderSignature();
    if (container.getAttribute("data-co-signature") === signature) {
      updateFocusClasses();
      return;
    }
    var form = state.steam || {};
    var host = state.steamHost || {};
    var available = state.steamP2pAvailable === true && !state.steamUnavailableReason;
    var hosting = isHosting(state);
    var passwordField = container.querySelector("#co-steam-password");
    var password = passwordField ? passwordField.value : "";
    var serverSettingsPassword = window.CO_ServerSettings ? CO_ServerSettings.capturePassword(container, state) : "";
    var joinPasswordField = container.querySelector("#co-steam-join-password");
    var joinPassword = joinPasswordField ? joinPasswordField.value : "";
    var peers = state.steamLobbyPeers || [];
    var identity = window.CO_ServerSettings ? CO_ServerSettings.advertised(state) : { name: "", maxPlayers: DEFAULT_MAX_PLAYERS };
    var disabled = available ? "" : ' disabled aria-disabled="true"';

    function row(id, label, control) {
      return (
        '<div class="co-steam-row' + (id === focusedId ? " focused" : "") + '" data-steam-field="' + id + '">' +
        '<label for="' + id + '">' + label + "</label>" +
        control +
        "</div>"
      );
    }

    function actionButton(id, label, always, unavailable, extra) {
      var off = always || available ? "" : disabled;
      if (unavailable === true) off = ' disabled aria-disabled="true"';
      return (
        '<button type="button" class="co-btn co-btn-primary' + (id === focusedId ? " focused" : "") + '" id="' + id + '"' +
        ' data-steam-field="' + id + '"' + (extra || "") + off + ">" + label + "</button>"
      );
    }

    function hostRows() {
      var hosts = state.steamFriendHosts || [];
      if (!hosts.length) return '<p class="co-steam-desc">No friends are hosting right now.</p>';
      return hosts.map(function (entry, index) {
        var id = "co-friend-host-" + index;
        return (
          '<div class="co-friend-host-row' + (entry.compatible ? "" : " incompatible") + '">' +
          '<div class="co-friend-host-info">' +
          '<span class="co-lan-name">' + escapeHtml(entry.name) + " · " + escapeHtml(entry.host) + "</span>" +
          '<span class="co-lan-meta">' + escapeHtml(entry.maxPlayers > 0 ? entry.players + "/" + entry.maxPlayers : String(entry.players)) + " players" +
          (entry.build ? " · " + escapeHtml(entry.build) : "") + "</span>" +
          (entry.compatible ? "" : '<span class="co-steam-error">' + escapeHtml(entry.reason) + "</span>") +
          "</div>" +
          actionButton(id, "Join", false, !entry.compatible, ' data-friend-host="' + escapeAttr(entry.friendCode) + '"') +
          "</div>"
        );
      }).join("");
    }

    function hostsSection() {
      var noMatchmaking = state.steamMatchmaking === false && !state.steamUnavailableReason;
      return (
        '<section class="co-steam-card co-steam-hosts">' +
        '<div class="co-steam-lan-head"><h3 class="co-steam-title">Friends playing Commonwealth Online</h3>' +
        '<span class="co-count" id="co-steam-hosts-count">' + (state.steamFriendHosts || []).length + "</span></div>" +
        (noMatchmaking ? '<p class="co-steam-notice" id="co-steam-hosts-unsupported">Steam friend lobbies are not available right now.</p>' : "") +
        (state.steamHostsNotice ? '<p class="co-steam-notice" id="co-steam-hosts-notice">' + escapeHtml(state.steamHostsNotice) + "</p>" : "") +
        '<div class="co-friend-host-list" id="co-friend-host-list">' + hostRows() + "</div>" +
        '<div class="co-steam-actions">' + actionButton("co-steam-hosts-refresh", "Refresh", state.steamP2pAvailable === true, noMatchmaking) + "</div>" +
        "</section>"
      );
    }

    var parts = [];
    if (state.steamUnavailableReason) {
      parts.push('<p class="co-steam-notice" id="co-steam-unavailable">' + escapeHtml(unavailableText(state)) + " Press Refresh to retry. LAN servers below still work.</p>");
    } else if (!available) {
      parts.push('<p class="co-steam-notice">' + NOT_AVAILABLE_TEXT + " LAN servers below still work.</p>");
    }
    parts.push('<div class="co-steam-grid">');
    parts.push('<section class="co-steam-card">');
    if (hosting) {
      parts.push('<h3 class="co-steam-title">Lobby</h3>');
      parts.push('<p class="co-steam-status">' + escapeHtml(hostStatusText(host)) + "</p>");
      if (host.friendCode) {
        parts.push(
          '<p class="co-steam-code-line">Friend Code <span class="co-steam-code" id="co-steam-host-code">' +
          escapeHtml(host.friendCode) +
          "</span></p>"
        );
      }
      parts.push('<div class="co-steam-actions">' + actionButton("co-steam-copy", "Copy") + actionButton("co-steam-invite", "Invite Steam friends") + "</div>");
      parts.push('<p class="co-steam-status">Friends connected: ' + peers.length + "</p>");
      parts.push('<ul class="co-steam-peers">' + peers.map(function (peer) {
        return "<li>" + escapeHtml((peer && peer.name) || "Steam friend") + "</li>";
      }).join("") + "</ul>");
      if (state.steamLobbyError) {
        parts.push('<p class="co-steam-error" id="co-steam-lobby-error">' + escapeHtml(state.steamLobbyError) + "</p>");
      }
      parts.push('<div class="co-steam-actions">' + actionButton("co-steam-enter", "Enter world", false, state.steamEnterPending === true) + actionButton("co-steam-stop", "Stop Hosting") + "</div>");
    } else {
      parts.push('<h3 class="co-steam-title">Host</h3>');
      parts.push('<p class="co-steam-desc">Start hosting runs the server on this PC. Friends join with the code below.</p>');
      parts.push('<p class="co-steam-notice" id="co-steam-advertise">Advertised as ' +
        escapeHtml(identity.name || "no server name yet") + " · " + escapeHtml(String(identity.maxPlayers)) +
        ' players, from Server settings below.</p>');
      parts.push(row("co-steam-password", "Server password for this login",
        '<input class="co-filter-input" id="co-steam-password" type="password" maxlength="128" autocomplete="off">'));
      parts.push('<div class="co-steam-actions">' + actionButton("co-steam-host", "Start Hosting") + "</div>");
      var statusText = hostStatusText(host);
      if (statusText) {
        parts.push('<p class="co-steam-status">' + escapeHtml(statusText) + "</p>");
      }
    }
    if (state.steamInviteNotice) {
      parts.push('<p class="co-steam-notice" id="co-steam-invite-notice">' + escapeHtml(state.steamInviteNotice) + "</p>");
    }
    parts.push("</section>");

    if (window.CO_ServerSettings) parts.push(CO_ServerSettings.section(state, focusedId));

    parts.push('<section class="co-steam-card">');
    parts.push('<h3 class="co-steam-title">Join</h3>');
    parts.push('<p class="co-steam-desc">Leave the code empty to join a friend who is already hosting, or enter their Friend Code or part of their Steam name.</p>');
    parts.push(row("co-steam-friend-code", "Friend Code",
      '<input class="co-filter-input" id="co-steam-friend-code" type="text" maxlength="64" autocomplete="off" value="' + escapeAttr(form.friendCode || "") + '">'));
    parts.push(row("co-steam-join-password", "Password (optional)",
      '<input class="co-filter-input" id="co-steam-join-password" type="password" maxlength="128" autocomplete="off">'));
    parts.push('<p class="co-steam-error" id="co-steam-join-error">' + escapeHtml(state.steamJoinError || "") + "</p>");
    parts.push('<div class="co-steam-actions">' + actionButton("co-steam-join", "Join") + "</div>");
    parts.push("</section></div>");
    parts.push(hostsSection());
    if (window.CO_SteamLan) {
      parts.push(CO_SteamLan.section(state, focusedId, actionButton("co-steam-scan", "Scan LAN", true)));
    }

    container.innerHTML = parts.join("");
    if (window.CO_ServerSettings) CO_ServerSettings.restorePassword(container, serverSettingsPassword);
    var restoredPassword = container.querySelector("#co-steam-password");
    if (restoredPassword) restoredPassword.value = password;
    joinPasswordField = container.querySelector("#co-steam-join-password");
    if (joinPasswordField) joinPasswordField.value = joinPassword;
    container.setAttribute("data-co-signature", signature);
    container.setAttribute("data-focused-field", focusedId || "");
    revealFocused();
  }

  function bind(root, handlers) {
    var pane = root.querySelector('[data-pane="steam-friends"]');
    if (!pane) return;
    handlers = handlers || {};

    function fire(name, value) {
      if (typeof handlers[name] === "function") handlers[name](value);
    }

    pane.addEventListener("click", function (e) {
      if (e.target.closest("#co-steam-host")) return fire("onHostStart");
      if (e.target.closest("#co-steam-stop")) return fire("onHostStop");
      if (e.target.closest("#co-steam-enter")) return fire("onEnter");
      if (e.target.closest("#co-steam-copy")) return copyFriendCode(root);
      if (e.target.closest("#co-steam-join")) return fire("onJoin");
      if (e.target.closest("#co-steam-invite")) return fire("onInvite");
      if (e.target.closest("#co-steam-scan")) return fire("onScan");
      if (e.target.closest("#co-steam-hosts-refresh")) return fire("onHostsRefresh");
      var friendHost = e.target.closest("[data-friend-host]");
      if (friendHost) return fire("onHostJoin", friendHost.getAttribute("data-friend-host"));
      var lan = e.target.closest("[data-lan-id]");
      if (lan) return fire("onLan", lan.getAttribute("data-lan-id"));
    });

    pane.addEventListener("input", function (e) {
      if (!e.target) return;
      if (e.target.id === "co-steam-friend-code") return fire("onFriendCode", e.target.value);
    });
  }

  function getValues(root) {
    var pane = root.querySelector('[data-pane="steam-friends"]');
    if (!pane) return { password: "", joinPassword: "", friendCode: "" };
    var password = pane.querySelector("#co-steam-password");
    var friendCode = pane.querySelector("#co-steam-friend-code");
    var joinPassword = pane.querySelector("#co-steam-join-password");
    return {
      password: password ? password.value : "",
      joinPassword: joinPassword ? joinPassword.value : "",
      friendCode: friendCode ? friendCode.value : ""
    };
  }

  function clearPassword(root, id) {
    var field = root.querySelector("#" + id);
    if (field) field.value = "";
  }

  async function copyFriendCode(root) {
    var el = root.querySelector("#co-steam-host-code");
    if (!el) return false;
    var text = el.textContent || "";
    try {
      if (typeof navigator !== "undefined" && navigator.clipboard && navigator.clipboard.writeText) {
        await navigator.clipboard.writeText(text);
        return true;
      }
    } catch (err) {
      console.warn("[CO] Steam friend code clipboard write failed:", err);
    }
    try {
      var range = document.createRange();
      range.selectNodeContents(el);
      var selection = window.getSelection();
      selection.removeAllRanges();
      selection.addRange(range);
      return true;
    } catch (err2) {
      console.warn("[CO] Steam friend code selection failed:", err2);
      return false;
    }
  }

  window.CO_SteamFriends = {
    MAX_PLAYERS: MAX_PLAYERS,
    DEFAULT_MAX_PLAYERS: DEFAULT_MAX_PLAYERS,
    FRIEND_CODE_ERROR: FRIEND_CODE_ERROR,
    create: create,
    render: render,
    bind: bind,
    getValues: getValues,
    clearPassword: clearPassword,
    parseFriendCode: parseFriendCode,
    normalizeHosts: normalizeHosts,
    unavailableText: unavailableText,
    fieldIds: fieldIds,
    copyFriendCode: copyFriendCode
  };

  function escapeHtml(value) {
    return String(value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function escapeAttr(value) {
    return String(value).replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;");
  }
})();
