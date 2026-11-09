(function () {
  "use strict";

  var MENU_LABELS = { players: "Players", moderation: "Bans & admins", world: "World", factions: "Factions", resources: "Scripts", reports: "Reports" };
  var TAB_REQUESTS = {
    players: ["players"],
    moderation: ["bans", "admin_list", "clients"],
    world: ["status", "map_public"],
    factions: ["factions"],
    resources: ["resources"],
    reports: ["reports"]
  };
  var REPORT_LABELS = { harassment: "Harassment", cheating: "Cheating", griefing: "Griefing", other: "Other", support: "Support request" };
  var CONFIRM = { kick: true, ban: true, kill: true, faction_kick: true, faction_disband: true, faction_promote: true, approve: true, stop: true, admin_grant: true, admin_revoke: true, world_mode: true };
  var QUERY_ACTIONS = { players: true, status: true, clients: true, bans: true, admin_list: true, factions: true, resources: true, map_public: true, reports: true, refresh: true };
  var ACTION_LABELS = { kick: "Kick", ban: "Ban IP", unban: "Unban", profile_unban: "Unban", revive: "Revive", kill: "Kill", admin_grant: "Make admin", admin_revoke: "Remove admin", voice_mute: "Mute", voice_unmute: "Unmute", world_time: "Set time", world_weather: "Set weather", world_mode: "World mode", quest_mode: "Quest mode", map_public: "Public map", faction_disband: "Disband", faction_promote: "Make leader", faction_kick: "Remove", ensure: "Start", stop: "Stop", approve: "Approve", refresh: "Rescan" };
  var ADMIN_SOURCES = { host: "Hosting this session", config: "Server config", granted: "Granted" };
  var MUTE_PRESETS = [["5 min", 300], ["30 min", 1800], ["1 hour", 3600]];
  var TIME_PRESETS = [["0600", "06:00"], ["1200", "12:00"], ["1800", "18:00"], ["0000", "00:00"]];
  var WORLD_MODES = [["coop-story", "Co-op story"], ["free-roam", "Free roam"]];
  var QUEST_MODES = [["coop-story", "Co-op story"], ["faction-war", "Faction war"], ["instanced-story", "Instanced story"], ["free-roam", "Free roam"]];
  var LOCKOUT_MS = 600;
  var RESULT_LIMIT = 8;

  var state = null;
  var lastStateKey = null;
  var ui = { tab: "", armed: "", mutePick: "", drafts: {}, notice: "" };
  var root = null;
  var registry = {};
  var lastSent = {};

  function text(value) {
    if (typeof value === "string") return value;
    return typeof value === "number" ? String(value) : "";
  }

  function trimmed(value) {
    return typeof value === "string" ? value.trim() : "";
  }

  function allowed(action) {
    return !!state && Array.isArray(state.actions) && state.actions.indexOf(action) >= 0;
  }

  function visibleMenus() {
    if (!state || !Array.isArray(state.menus)) return [];
    return state.menus.filter(function (name) {
      return Object.prototype.hasOwnProperty.call(MENU_LABELS, name);
    });
  }

  function dataOf(action) {
    return state && state.data && state.data[action] !== undefined ? state.data[action] : null;
  }

  function listOf(value, key) {
    if (Array.isArray(value)) return value;
    return value && Array.isArray(value[key]) ? value[key] : [];
  }

  function labelOf(pairs, value) {
    for (var i = 0; i < pairs.length; i += 1) {
      if (pairs[i][0] === value) return pairs[i][1];
    }
    return value;
  }

  function onOff(value) {
    if (value === true) return "On";
    return value === false ? "Off" : "";
  }

  function truncatedNote(raw) {
    return raw && raw.truncated === true ? note("Too large to display (" + text(raw.bytes) + " bytes).") : null;
  }

  function rosterKey(value) {
    var players = value && Array.isArray(value.players) ? value.players : [];
    return JSON.stringify(players.map(function (player) { return [player && player.playerId, player && player.profileId]; }));
  }

  function keyboardOpen() {
    return !!(window.CO_Keyboard && window.CO_Keyboard.isOpen());
  }

  function post(payload) {
    if (typeof window.coAdminAction === "function") {
      window.coAdminAction(JSON.stringify(payload));
    }
  }

  function sendAction(action, params) {
    var payload = params || {};
    if (!QUERY_ACTIONS[action]) {
      var key = action + JSON.stringify(payload);
      var now = Date.now();
      if (lastSent[key] !== undefined && now - lastSent[key] < LOCKOUT_MS) return;
      lastSent[key] = now;
    }
    ui.notice = "";
    post({ op: "action", action: action, params: payload });
  }

  function close() {
    ui.armed = "";
    post({ op: "close" });
  }

  function el(tag, attrs, children) {
    var node = document.createElement(tag);
    Object.keys(attrs || {}).forEach(function (name) {
      var value = attrs[name];
      if (name === "text") {
        node.textContent = value;
      } else if (name === "onClick") {
        node.addEventListener("click", value);
      } else if (name === "value") {
        node.value = value;
      } else if (name === "key") {
        node.setAttribute("data-key", value);
        registry[value] = node;
      } else {
        node.setAttribute(name, value);
      }
    });
    (children || []).forEach(function (child) {
      if (child) node.appendChild(child);
    });
    return node;
  }

  function note(message) {
    return el("p", { "class": "admin-muted", text: message });
  }

  function section(title, children) {
    return el("section", { "class": "admin-block" }, [el("h2", { text: title })].concat(children));
  }

  function infoRow(name, value) {
    return el("li", { "class": "admin-row" }, [
      el("span", { "class": "admin-name", text: name }),
      el("span", { "class": "admin-muted", text: value })
    ]);
  }

  function button(label, key, onClick, confirmText) {
    var armed = ui.armed === key;
    return el("button", {
      type: "button",
      "class": "admin-btn" + (armed ? " is-armed" : ""),
      key: key,
      text: armed ? (confirmText || "Confirm " + label) : label,
      onClick: onClick
    });
  }

  function control(action, label, key, params, confirmText) {
    if (!allowed(action)) return null;
    return button(label, key, function () {
      if (CONFIRM[action] && ui.armed !== key) {
        ui.armed = key;
        paint();
        return;
      }
      var payload = params();
      ui.armed = "";
      if (payload) sendAction(action, payload);
      paint();
    }, confirmText);
  }

  function withReason(params) {
    var reason = trimmed(ui.drafts.reason);
    if (reason) params.reason = reason;
    ui.drafts.reason = "";
    return params;
  }

  function draftParams(param, message) {
    var value = trimmed(ui.drafts[param]);
    if (!value) {
      ui.notice = message;
      return null;
    }
    var params = {};
    params[param] = value;
    return params;
  }

  function openKeyboard(event) {
    if (window.CO_Keyboard && event && event.target) {
      window.CO_Keyboard.open(event.target);
      window.CO_Keyboard.render(root);
    }
  }

  function field(key, label, placeholder) {
    return el("input", {
      type: "text",
      "class": "admin-input",
      key: key,
      "aria-label": label,
      placeholder: placeholder,
      maxlength: "120",
      value: ui.drafts[key] || "",
      onClick: openKeyboard
    });
  }

  function mutePresetButton(id, rowKey, label, seconds) {
    return button(label, "mute-" + rowKey + "-" + seconds, function () {
      ui.mutePick = "";
      var payload = { playerId: id };
      if (seconds > 0) payload.seconds = seconds;
      sendAction("voice_mute", payload);
      paint();
    });
  }

  function muteCells(id, rowKey) {
    if (!allowed("voice_mute")) return [];
    if (ui.mutePick !== rowKey) {
      return [button("Mute", "mute-" + rowKey, function () {
        ui.mutePick = rowKey;
        paint();
      })];
    }
    return MUTE_PRESETS.map(function (preset) {
      return mutePresetButton(id, rowKey, preset[0], preset[1]);
    }).concat([
      mutePresetButton(id, rowKey, "No limit", 0),
      button("Cancel", "mute-cancel-" + rowKey, function () {
        ui.mutePick = "";
        paint();
      })
    ]);
  }

  function playerRow(player) {
    if (!player || typeof player.playerId !== "number") return null;
    var id = player.playerId;
    var name = text(player.name) || "Player " + id;
    var rowKey = text(player.profileId) || String(id);
    var target = name + " (ID " + id + ")";
    return el("li", { "class": "admin-row" }, [
      el("span", { "class": "admin-name", text: name }),
      el("span", { "class": "admin-muted", text: "ID " + id }),
      control("kick", "Kick", "kick-" + rowKey, function () { return withReason({ playerId: id }); }, "Confirm kick " + target),
      control("ban", "Ban IP", "ban-" + rowKey, function () { return withReason({ playerId: id }); }, "Confirm IP ban for " + target),
      control("revive", "Revive", "revive-" + rowKey, function () { return { playerId: id }; }),
      control("kill", "Kill", "kill-" + rowKey, function () { return { playerId: id }; }, "Confirm kill " + target),
      control("admin_grant", "Make admin", "admin-" + rowKey, function () { return { playerId: id }; }, "Confirm make " + target + " an admin"),
      control("voice_unmute", "Unmute", "unmute-" + rowKey, function () { return { playerId: id }; })
    ].concat(muteCells(id, rowKey)));
  }

  function playersBody() {
    var players = Array.isArray(state.players) ? state.players : [];
    var rows = players.map(playerRow).filter(Boolean);
    return [
      el("div", { "class": "admin-row" }, [field("reason", "Reason for kick or ban (optional)", "Reason (optional)")]),
      rows.length ? el("ul", { "class": "admin-list" }, rows) : note("No players are connected.")
    ];
  }

  function banRow(ban) {
    if (!ban || typeof ban !== "object") return null;
    var ip = text(ban.ip);
    var profileId = text(ban.profileId);
    var reason = text(ban.reason);
    var details = [ip && profileId ? "Profile " + profileId : "", reason ? "Reason: " + reason : ""].filter(Boolean).join(" · ");
    var cells = [
      el("span", { "class": "admin-name", text: ip || (profileId ? "Profile " + profileId : "Ban") }),
      el("span", { "class": "admin-muted", text: details })
    ];
    if (ip) {
      cells.push(control("unban", "Unban", "unban:" + ip, function () { return { ip: ip }; }));
    } else if (profileId) {
      cells.push(control("profile_unban", "Unban", "unban-profile:" + profileId, function () { return { profileId: profileId }; }));
    }
    return el("li", { "class": "admin-row" }, cells);
  }

  function steamBanRow(ban) {
    if (!ban || typeof ban !== "object") return null;
    var steamId = text(ban.steamId);
    if (!steamId) return null;
    var reason = text(ban.reason);
    return el("li", { "class": "admin-row" }, [
      el("span", { "class": "admin-name", text: "Steam ID " + steamId }),
      el("span", { "class": "admin-muted", text: reason ? "Reason: " + reason : "" })
    ]);
  }

  function bansBlock() {
    if (!allowed("bans")) return null;
    var raw = dataOf("bans");
    var rows = listOf(raw, "bans").map(banRow).filter(Boolean);
    var steam = listOf(raw, "steamBans").map(steamBanRow).filter(Boolean);
    return section("Bans", [
      truncatedNote(raw) || (rows.length ? el("ul", { "class": "admin-list" }, rows) : note(raw === null ? "Loading..." : "No bans.")),
      steam.length ? el("h2", { text: "Steam bans" }) : null,
      steam.length ? note("Steam bans can only be lifted from the server console.") : null,
      steam.length ? el("ul", { "class": "admin-list" }, steam) : null
    ]);
  }

  function adminRow(entry) {
    var profileId = entry && text(entry.profileId);
    if (!profileId) return null;
    var cells = [
      el("span", { "class": "admin-name", text: profileId }),
      el("span", { "class": "admin-muted", text: ADMIN_SOURCES[text(entry.source)] || "" })
    ];
    if (entry.source === "granted") cells.push(control("admin_revoke", "Revoke", "revoke:" + profileId, function () { return { profileId: profileId }; }, "Confirm revoke admin " + profileId));
    return el("li", { "class": "admin-row" }, cells);
  }

  function adminsBlock() {
    if (!allowed("admin_list")) return null;
    var raw = dataOf("admin_list");
    var rows = listOf(raw, "admins").map(adminRow).filter(Boolean);
    return section("Admins", [
      truncatedNote(raw) || (rows.length ? el("ul", { "class": "admin-list" }, rows) : note(raw === null ? "Loading..." : "No admins."))
    ]);
  }

  function clientRow(client) {
    if (!client || typeof client !== "object") return null;
    var address = text(client.address);
    var port = text(client.port);
    var steamId = text(client.steam_id);
    var details = [address ? (port ? address + ":" + port : address) : "", steamId ? "Steam ID " + steamId : ""].filter(Boolean).join(" · ");
    return el("li", { "class": "admin-row" }, [
      el("span", { "class": "admin-name", text: "Player " + text(client.player_id) }),
      el("span", { "class": "admin-muted", text: details })
    ]);
  }

  function clientsBlock() {
    if (!allowed("clients")) return null;
    var raw = dataOf("clients");
    var rows = listOf(raw, "clients").map(clientRow).filter(Boolean);
    return section("Connected clients", [
      truncatedNote(raw) || (rows.length ? el("ul", { "class": "admin-list" }, rows) : note(raw === null ? "Loading..." : "No clients connected."))
    ]);
  }

  function moderationBody() {
    return [bansBlock(), adminsBlock(), clientsBlock()];
  }

  function statusBlock() {
    if (!allowed("status")) return null;
    var raw = dataOf("status");
    if (raw === null) return section("Server", [note("Loading...")]);
    if (raw.truncated === true) return section("Server", [truncatedNote(raw)]);
    return section("Server", [el("ul", { "class": "admin-list" }, [
      infoRow("Name", text(raw.server_name)),
      infoRow("Players online", text(raw.connected_clients) + " of " + text(raw.max_players)),
      infoRow("Waiting to join", text(raw.pending_connections)),
      infoRow("World mode", labelOf(WORLD_MODES, text(raw.world_mode))),
      infoRow("Friendly fire", onOff(raw.friendly_fire)),
      infoRow("Faction friendly fire", onOff(raw.faction_friendly_fire)),
      infoRow("Progress saved", onOff(raw.persistence_enabled))
    ])]);
  }

  function timeBlock() {
    if (!allowed("world_time")) return null;
    return section("Time of day", [el("div", { "class": "admin-row" }, TIME_PRESETS.map(function (preset) {
      return control("world_time", preset[1], "time-" + preset[0], function () { return { hhmm: preset[0] }; });
    }))]);
  }

  function weatherBlock() {
    if (!allowed("world_weather")) return null;
    return section("Weather", [el("div", { "class": "admin-row" }, [
      field("weather", "Weather form ID", "8 hex digits or auto"),
      control("world_weather", "Set weather", "set-weather", function () {
        return draftParams("weather", "Enter an 8-digit hex weather form ID, or auto to clear the lock.");
      })
    ])]);
  }

  function worldModeBlock() {
    if (!allowed("world_mode")) return null;
    var status = dataOf("status");
    var current = status && typeof status === "object" ? labelOf(WORLD_MODES, text(status.world_mode)) : "";
    return section("World mode", [
      note(current ? "Current: " + current : "Current mode is not known yet."),
      el("div", { "class": "admin-row" }, WORLD_MODES.map(function (mode) {
        return control("world_mode", mode[1], "mode-" + mode[0], function () { return { mode: mode[0] }; }, "Confirm set world mode to " + mode[1]);
      }))
    ]);
  }

  function questModeBlock() {
    if (!allowed("quest_mode")) return null;
    return section("Quest mode", [el("div", { "class": "admin-row" }, QUEST_MODES.map(function (mode) {
      return control("quest_mode", mode[1], "quest-" + mode[0], function () { return { mode: mode[0] }; });
    }).concat([control("quest_mode", "Show current", "quest-current", function () { return {}; })]))]);
  }

  function publicMapBlock() {
    if (!allowed("map_public")) return null;
    var raw = dataOf("map_public");
    if (raw === null) return section("Public map", [note("Loading...")]);
    var shown = raw.enabled === true;
    return section("Public map", [el("div", { "class": "admin-row" }, [
      note(shown ? "Everyone can see every player on the map." : "Players see only themselves and their faction on the map."),
      button(shown ? "Limit to own faction" : "Show everyone", "map-public", function () {
        sendAction("map_public", { enabled: !shown });
        sendAction("map_public", {});
      })
    ])]);
  }

  function worldBody() {
    return [statusBlock(), timeBlock(), weatherBlock(), worldModeBlock(), questModeBlock(), publicMapBlock()];
  }

  function memberRow(faction, member) {
    var profileId = text(member);
    if (!profileId) return null;
    var factionId = text(faction.factionId);
    var factionName = text(faction.name) || factionId;
    var names = faction.memberNames && typeof faction.memberNames === "object" ? faction.memberNames : {};
    var name = Object.prototype.hasOwnProperty.call(names, profileId) ? text(names[profileId]) : "";
    var display = name || profileId;
    var role = profileId === faction.leaderProfileId ? "Leader" : (listOf(faction.officers).indexOf(profileId) >= 0 ? "Officer" : "");
    var key = factionId + ":" + profileId;
    var params = function () { return { factionId: factionId, profileId: profileId }; };
    return el("li", { "class": "admin-row" }, [
      el("span", { "class": "admin-name", text: display }),
      el("span", { "class": "admin-muted", text: role }),
      role !== "Leader" ? control("faction_promote", "Make leader", "promote:" + key, params, "Confirm make " + display + " leader of " + factionName) : null,
      control("faction_kick", "Remove", "faction-kick:" + key, params, "Confirm remove " + display + " from " + factionName)
    ]);
  }

  function factionCard(faction) {
    if (!faction || typeof faction !== "object") return null;
    var id = text(faction.factionId);
    if (!id) return null;
    var name = text(faction.name) || id;
    var tag = text(faction.tag);
    var title = name + (tag ? " [" + tag + "]" : "");
    var members = Array.isArray(faction.members) ? faction.members : [];
    return el("section", { "class": "admin-block" }, [
      el("div", { "class": "admin-row" }, [
        el("span", { "class": "admin-name", text: title }),
        control("faction_disband", "Disband", "disband:" + id, function () { return { factionId: id }; }, "Confirm disband " + name)
      ]),
      el("ul", { "class": "admin-list" }, members.map(function (member) { return memberRow(faction, member); }).filter(Boolean))
    ]);
  }

  function factionsBody() {
    var raw = dataOf("factions");
    var tooLarge = truncatedNote(raw);
    if (tooLarge) return [tooLarge];
    var factions = listOf(raw, "factions").map(factionCard).filter(Boolean);
    if (!factions.length) return [note(raw === null ? "Loading..." : "No factions.")];
    return factions;
  }

  function resourceRow(resource) {
    var name = typeof resource === "string" ? resource : text(resource && resource.name);
    if (!name) return null;
    var info = resource && typeof resource === "object" ? [text(resource.state), text(resource.version)].filter(Boolean).join(" ") : "";
    var approval = resource && typeof resource.approved === "boolean" ? (resource.approved ? "approved" : "not approved") : "";
    var permissions = resource && Array.isArray(resource.permissions) ? resource.permissions.map(text).filter(Boolean).join(", ") : "";
    var params = function () { return { name: name }; };
    return el("li", { "class": "admin-row" }, [
      el("span", { "class": "admin-name", text: name }),
      el("span", { "class": "admin-muted", text: [info, approval, text(resource && resource.detail)].filter(Boolean).join(" · ") }),
      el("span", { "class": "admin-muted", text: "Permissions: " + (permissions || "none") }),
      control("ensure", "Start", "ensure:" + name, params),
      control("stop", "Stop", "stop:" + name, params, "Confirm stop " + name),
      control("approve", "Approve", "approve:" + name, params, "Confirm approve " + name)
    ]);
  }

  function resourcesBody() {
    var raw = dataOf("resources");
    var rows = listOf(raw, "resources").map(resourceRow).filter(Boolean);
    return [
      allowed("refresh") ? button("Rescan scripts", "rescan", function () {
        sendAction("refresh", {});
        sendAction("resources", {});
      }) : null,
      truncatedNote(raw) || (rows.length ? el("ul", { "class": "admin-list" }, rows) : note(raw === null ? "Loading..." : "No scripts."))
    ];
  }

  function agoText(at) {
    var now = state && typeof state.serverTime === "number" ? state.serverTime : null;
    if (now === null || typeof at !== "number" || at > now) return "";
    var seconds = now - at;
    if (seconds < 60) return "under a minute ago";
    var minutes = Math.floor(seconds / 60);
    if (minutes < 60) return minutes + " min ago";
    var hours = Math.floor(minutes / 60);
    return hours < 48 ? hours + " h ago" : Math.floor(hours / 24) + " days ago";
  }

  function reportRow(report) {
    if (!report || typeof report !== "object") return null;
    var category = REPORT_LABELS[text(report.reason)] || REPORT_LABELS[text(report.kind)] || "Report";
    var cells = [
      el("span", { "class": "admin-name", text: category }),
      el("span", { "class": "admin-muted", text: text(report.reportId) }),
      el("span", { "class": "admin-muted", text: agoText(report.serverTime) }),
      el("span", { "class": "admin-muted", text: ("From #" + text(report.reporterPlayerId) + " " + text(report.reporterName)).trim() })
    ];
    if (typeof report.targetPlayerId === "number") {
      cells.push(el("span", { "class": "admin-muted", text: ("About #" + report.targetPlayerId + " " + text(report.targetName)).trim() }));
    }
    cells.push(el("span", { "class": "admin-muted", text: text(report.note) || "(no message)" }));
    return el("li", { "class": "admin-row" }, cells);
  }

  function reportsBody() {
    var raw = dataOf("reports");
    if (raw === null) return [note("Loading...")];
    var tooLarge = truncatedNote(raw);
    if (tooLarge) return [tooLarge];
    var rows = listOf(raw, "reports").slice().reverse().map(reportRow).filter(Boolean);
    if (!rows.length) return [note("No player reports yet.")];
    var total = raw && typeof raw.total === "number" ? raw.total : rows.length;
    return [note("Showing the latest " + rows.length + " of " + total + " reports. Read-only."), el("ul", { "class": "admin-list" }, rows)];
  }

  var TAB_BODY = {
    players: playersBody,
    moderation: moderationBody,
    world: worldBody,
    factions: factionsBody,
    resources: resourcesBody,
    reports: reportsBody
  };

  function resultRow(result) {
    if (!result || typeof result !== "object") return null;
    var ok = result.ok === true;
    var action = text(result.action);
    var label = Object.prototype.hasOwnProperty.call(ACTION_LABELS, action) ? ACTION_LABELS[action] : "";
    var message = text(result.message) || (ok ? "Done." : "That did not work.");
    return el("li", {
      "class": "admin-result " + (ok ? "is-ok" : "is-fail"),
      text: label && message.indexOf(label) !== 0 ? label + ": " + message : message
    });
  }

  function resultsBody() {
    var results = Array.isArray(state.results) ? state.results.slice(-RESULT_LIMIT).reverse() : [];
    var items = results.map(resultRow).filter(Boolean);
    if (ui.notice) items.unshift(el("li", { "class": "admin-result is-fail", text: ui.notice }));
    return el("section", { "class": "admin-block" }, [
      el("h2", { text: "Results" }),
      el("ul", { "class": "admin-list" }, items.length ? items : [note("No results yet.")])
    ]);
  }

  function tabBar(menus) {
    return el("nav", { "class": "admin-tabs", role: "tablist", "aria-label": "Admin" }, menus.map(function (name) {
      return el("button", {
        type: "button",
        "class": "admin-tab",
        role: "tab",
        "aria-selected": name === ui.tab ? "true" : "false",
        key: "tab-" + name,
        text: MENU_LABELS[name],
        onClick: function () { selectTab(name); }
      });
    }));
  }

  function panel(children) {
    return el("section", { "class": "admin-panel" }, [
      el("header", { "class": "admin-head" }, [
        el("h1", { text: "ADMIN" }),
        button("Close", "close", close)
      ]),
      el("div", { "class": "admin-body", key: "body" }, children)
    ]);
  }

  function renderPage() {
    if (!state) return panel([note("Loading admin status...")]);
    if (state.error) return panel([note(text(state.error))]);
    if (state.isAdmin !== true) {
      return panel([note(state.connected === true ? "Admin access is not available." : "Not connected to a server.")]);
    }
    var menus = visibleMenus();
    if (!menus.length) return panel([note("No admin tabs are available.")]);
    if (menus.indexOf(ui.tab) < 0) ui.tab = menus[0];
    return panel([
      tabBar(menus),
      el("div", { "class": "admin-tabpage" }, TAB_BODY[ui.tab]()),
      resultsBody(),
      el("div", { "class": "admin-foot" }, [button("Refresh", "refresh-tab", function () {
        ui.armed = "";
        requestTab(ui.tab);
        paint();
      })])
    ]);
  }

  function paint() {
    if (!root || keyboardOpen()) return;
    var active = document.activeElement;
    var focusKey = active && active.getAttribute ? active.getAttribute("data-key") : null;
    var scrollTop = registry.body ? registry.body.scrollTop : 0;
    registry = {};
    root.textContent = "";
    root.appendChild(renderPage());
    root.appendChild(el("div", { "class": "co-keyboard hidden" }));
    if (focusKey && registry[focusKey]) registry[focusKey].focus();
    if (registry.body) registry.body.scrollTop = scrollTop;
    if (window.CO_Keyboard) window.CO_Keyboard.render(root);
  }

  function requestTab(name) {
    (TAB_REQUESTS[name] || []).forEach(function (action) {
      if (allowed(action)) sendAction(action, {});
    });
  }

  function selectTab(name) {
    if (!Object.prototype.hasOwnProperty.call(MENU_LABELS, name)) return;
    ui.tab = name;
    ui.armed = "";
    ui.mutePick = "";
    requestTab(name);
    paint();
  }

  function cycleTab(delta) {
    var menus = visibleMenus();
    if (!menus.length) return;
    selectTab(menus[(menus.indexOf(ui.tab) + delta + menus.length) % menus.length]);
  }

  function lastResult(value) {
    var results = value && Array.isArray(value.results) ? value.results : [];
    return results[results.length - 1] || null;
  }

  function changeFinished(previous) {
    var last = lastResult(state);
    var before = lastResult(previous);
    if (!previous || !last || last.ok !== true || QUERY_ACTIONS[last.action]) return false;
    return !before || before.requestId !== last.requestId;
  }

  function setState(raw) {
    var next;
    try {
      next = typeof raw === "string" ? JSON.parse(raw) : raw;
    } catch (err) {
      return;
    }
    if (!next || typeof next !== "object") return;
    var key = JSON.stringify(next);
    if (key === lastStateKey) return;
    lastStateKey = key;
    var roster = rosterKey(state);
    var previous = state;
    state = next;
    if (rosterKey(state) !== roster) {
      ui.armed = "";
      ui.mutePick = "";
    }
    var menus = visibleMenus();
    if (state.isAdmin === true && menus.length && menus.indexOf(ui.tab) < 0) {
      ui.tab = menus[0];
    }
    if (state.isAdmin === true && changeFinished(previous)) requestTab(ui.tab);
    paint();
  }

  function focusables() {
    if (!root) return [];
    return Array.prototype.slice.call(root.querySelectorAll("button:not([disabled]), input"));
  }

  function moveFocus(delta) {
    var items = focusables();
    if (!items.length) return;
    var index = items.indexOf(document.activeElement);
    if (index < 0) {
      index = delta > 0 ? 0 : items.length - 1;
    } else {
      index = (index + delta + items.length) % items.length;
    }
    items[index].focus();
  }

  function keyboardButton(button) {
    window.CO_Keyboard.handleButton(button);
    window.CO_Keyboard.render(root);
  }

  function activateFocused() {
    var active = document.activeElement;
    if (!active) return;
    if (active.tagName === "INPUT" && window.CO_Keyboard) {
      openKeyboard({ target: active });
    } else if (active.tagName === "BUTTON") {
      active.click();
    }
  }

  function onRootInput(event) {
    var target = event.target;
    var key = target && target.tagName === "INPUT" ? target.getAttribute("data-key") : null;
    if (key) ui.drafts[key] = target.value;
  }

  function onKeydown(event) {
    if (event.key === "Escape") {
      event.preventDefault();
      if (keyboardOpen()) {
        keyboardButton("b");
      } else {
        close();
      }
      return;
    }
    if (keyboardOpen()) return;
    if (event.key === "ArrowDown") {
      event.preventDefault();
      moveFocus(1);
    } else if (event.key === "ArrowUp") {
      event.preventDefault();
      moveFocus(-1);
    } else if (event.key === "Enter") {
      event.preventDefault();
      activateFocused();
    }
  }

  function handleControllerInput(button, phase) {
    if (phase !== "down") return;
    if (keyboardOpen()) {
      keyboardButton(button);
      return;
    }
    if (button === "lb") {
      cycleTab(-1);
    } else if (button === "rb") {
      cycleTab(1);
    } else if (button === "b") {
      close();
    } else if (button === "dpad_down") {
      moveFocus(1);
    } else if (button === "dpad_up") {
      moveFocus(-1);
    } else if (button === "a") {
      activateFocused();
    }
  }

  function updateUiScale() {
    if (!window.innerWidth) return;
    var scale = Math.max(0.75, Math.min(1.35, window.innerWidth / 1920));
    document.documentElement.style.setProperty("--co-ui-scale", String(scale));
  }

  function init() {
    root = document.getElementById("coAdmin");
    if (!root) return;
    root.addEventListener("input", onRootInput);
    document.addEventListener("keydown", onKeydown);
    updateUiScale();
    if (window.CO_Keyboard) {
      window.CO_Keyboard.onClosed = paint;
    }
    paint();
  }

  window.coAdminSetState = setState;
  window.coAdminReset = function () {
    ui.armed = "";
    ui.mutePick = "";
    ui.notice = "";
    ui.tab = "";
    lastSent = {};
    lastStateKey = null;
    if (document.activeElement) document.activeElement.blur();
    if (registry.body) registry.body.scrollTop = 0;
    paint();
  };
  window.handleControllerInput = handleControllerInput;

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
