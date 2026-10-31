(function () {
  "use strict";

  var FOLDER_ID = "co-ss-folder";
  var PASSWORD_ID = "co-ss-password";
  var CLEAR_ID = "co-ss-password-clear";
  var RELOAD_ID = "co-ss-reload";
  var SAVE_ID = "co-ss-save";

  var FIELDS = [
    { id: FOLDER_ID, type: "text", label: "Server folder", numeric: false, folder: true, wide: true,
      hint: "Folder holding CommonwealthOnline.Server.exe and commonwealth-server.json." },
    { id: "co-ss-name", key: "serverName", type: "text", maxlength: 64, label: "Listed server name", wide: true,
      hint: "The name LAN discovery and the server directory report." },
    { id: "co-ss-description", key: "serverDescription", type: "text", maxlength: 256, label: "Description", wide: true },
    { id: PASSWORD_ID, type: "password", maxlength: 64, label: "Server password", wide: true,
      hint: "4-64 printable ASCII characters. Leaving it blank keeps the saved password." },
    { id: CLEAR_ID, key: "passwordClear", type: "toggle", label: "Remove saved password" },
    { id: "co-ss-max-players", key: "maxPlayers", type: "select", numbers: true, label: "Max players" },
    { id: "co-ss-world-mode", key: "worldMode", type: "select", modes: true, label: "World mode" },
    { id: "co-ss-vats", key: "vats", type: "select", vatsModes: true, label: "VATS",
      hint: "How VATS behaves in multiplayer: disabled, real-time without slow-motion, or vanilla (not recommended, it desyncs)." },
    { id: "co-ss-downed-seconds", key: "downedSeconds", type: "text", numeric: true, maxlength: 3, label: "Downed seconds" },
    { id: "co-ss-resume-hours", key: "resumeTokenLifetimeHours", type: "text", numeric: true, maxlength: 5, label: "Resume token hours" },
    { id: "co-ss-doors-shared", key: "doorsShared", type: "toggle", label: "Doors shared" },
    { id: "co-ss-friendly-fire", key: "friendlyFire", type: "toggle", label: "Friendly fire" },
    { id: "co-ss-faction-friendly-fire", key: "factionFriendlyFire", type: "toggle", label: "Faction friendly fire" },
    { id: "co-ss-chat", key: "enableChat", type: "toggle", label: "Chat" },
    { id: "co-ss-factions", key: "enableFactions", type: "toggle", label: "Factions" },
    { id: "co-ss-persistence", key: "enablePersistence", type: "toggle", label: "Persistence" },
    { id: "co-ss-object-state", key: "enableObjectState", type: "toggle", label: "Object state" },
    { id: "co-ss-lan", key: "lanDiscoveryEnabled", type: "toggle", label: "LAN discovery" },
    { id: "co-ss-listing", key: "publicListing", type: "toggle", label: "Public listing",
      lockHint: "Steam P2P hosts are never listed." }
  ];

  var LISTING_KEY = "publicListing";

  var FIELD_IDS = FIELDS.map(function (field) { return field.id; });
  var ALL_IDS = [FOLDER_ID].concat(FIELD_IDS.slice(1), [RELOAD_ID, SAVE_ID]);

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

  function sameFolder(a, b) {
    function normalize(value) {
      return String(value || "").trim().replace(/[\/]+$/, "").toLowerCase();
    }
    return normalize(a) === normalize(b);
  }

  function fieldById(id) {
    for (var i = 0; i < FIELDS.length; i++) {
      if (FIELDS[i].id === id) return FIELDS[i];
    }
    return null;
  }

  function firstReplyError(settings, fallback) {
    var errors = settings.errors || [];
    return errors.length ? String(errors[0]) : fallback;
  }

  function ensureState(state) {
    if (!state.serverSettings) {
      state.serverSettings = {
        loaded: false,
        loading: false,
        folder: "",
        configPath: "",
        values: null,
        draft: {},
        folderDraft: "",
        folderDraftSet: false,
        errors: [],
        folderError: "",
        status: "",
        limits: { maximumPlayers: 16, worldModes: ["coop-story", "free-roam"], vatsModes: ["disabled", "real-time", "vanilla"] },
        passwordClear: false,
        passwordSaved: false,
        saving: false,
        worldModeRefusal: "",
        pendingSave: null,
        pendingBefore: null,
        lastPasswordIntent: "",
        readable: true,
        listingLocked: false
      };
    }
    return state.serverSettings;
  }

  function maxPlayerOptions(settings) {
    var limit = settings.limits && settings.limits.maximumPlayers > 0 ? settings.limits.maximumPlayers : 16;
    var values = [];
    for (var index = 1; index <= limit; index++) values.push(index);
    return values;
  }

  function worldModeOptions(settings) {
    var modes = settings.limits && settings.limits.worldModes;
    return modes && modes.length ? modes : ["coop-story", "free-roam"];
  }

  function vatsModeOptions(settings) {
    var modes = settings.limits && settings.limits.vatsModes;
    return modes && modes.length ? modes : ["disabled", "real-time", "vanilla"];
  }

  function advertised(state) {
    var settings = ensureState(state);
    var values = settings.values || {};
    var name = typeof values.serverName === "string" ? values.serverName.trim() : "";
    var maxPlayers = parseInt(values.maxPlayers, 10);
    if (isNaN(maxPlayers) || maxPlayers < 1) maxPlayers = 1;
    var limit = settings.limits && settings.limits.maximumPlayers > 0 ? settings.limits.maximumPlayers : maxPlayers;
    return { name: name, maxPlayers: Math.min(maxPlayers, limit) };
  }

  function hostBlockReason(state) {
    var settings = ensureState(state);
    if (!settings.loaded) {
      return "Reading the server settings; start hosting again once they load.";
    }
    var errors = settings.errors || [];
    var detail = errors.length ? String(errors[0]) : "";
    if (settings.readable === false) {
      return "The server configuration in " + (settings.folder || "the server folder") +
        " could not be read, so the server cannot start and there is no name to advertise." +
        (detail ? " " + detail : "") +
        " Fix the file and press Reload in Server settings, or point the panel at a folder that holds a good one.";
    }
    if (settings.worldModeRefusal) {
      return settings.worldModeRefusal + " Set World mode in Server settings below and press Save.";
    }
    if (detail) {
      return "The saved configuration would not start the server: " + detail +
        " Fix it in Server settings below, or press Reload once the file is repaired.";
    }
    if (!advertised(state).name) {
      return "Set your server name in Server settings, then start hosting again.";
    }
    return "";
  }

  function valueOf(state, field) {
    var settings = ensureState(state);
    if (field.folder) return settings.folderDraftSet ? settings.folderDraft : settings.folder;
    if (field.key === "passwordClear") return settings.passwordClear === true;
    if (field.key === LISTING_KEY && settings.listingLocked === true) return false;
    if (Object.prototype.hasOwnProperty.call(settings.draft, field.key)) return settings.draft[field.key];
    if (settings.values && Object.prototype.hasOwnProperty.call(settings.values, field.key)) return settings.values[field.key];
    return field.type === "toggle" ? false : "";
  }

  function optionsFor(settings, field) {
    if (field.numbers) return maxPlayerOptions(settings);
    if (field.vatsModes) return vatsModeOptions(settings);
    return worldModeOptions(settings);
  }

  function selectMarkup(state, field, focusedId) {
    var settings = ensureState(state);
    var current = valueOf(state, field);
    var options = optionsFor(settings, field).map(function (option) {
      return '<option value="' + escapeAttr(option) + '"' + (String(option) === String(current) ? " selected" : "") + ">" +
        escapeHtml(option) + "</option>";
    }).join("");
    var hint = field.hint ? '<p class="co-ss-hint">' + escapeHtml(field.hint) + "</p>" : "";
    return (
      '<div class="co-steam-row co-ss-row' + (field.wide ? " co-ss-row--wide" : "") + (field.id === focusedId ? " focused" : "") + '" data-ss-field="' + field.id + '">' +
      '<label for="' + field.id + '">' + escapeHtml(field.label) + "</label>" +
      '<select class="co-filter-select" id="' + field.id + '" data-ss-select="1">' + options + "</select>" +
      hint +
      "</div>"
    );
  }

  function textMarkup(state, field, focusedId) {
    var settings = ensureState(state);
    var type = field.type === "password" ? "password" : "text";
    var maxlength = field.maxlength ? ' maxlength="' + field.maxlength + '"' : "";
    var hint = field.hint ? '<p class="co-ss-hint">' + escapeHtml(field.hint) + "</p>" : "";
    var readError = field.folder && settings.folderError
      ? '<p class="co-ss-hint co-ss-refusal" id="co-ss-folder-error">' + escapeHtml(settings.folderError) + "</p>"
      : "";
    return (
      '<div class="co-steam-row co-ss-row' + (field.wide ? " co-ss-row--wide" : "") + (field.id === focusedId ? " focused" : "") + '" data-ss-field="' + field.id + '">' +
      '<label for="' + field.id + '">' + escapeHtml(field.label) + "</label>" +
      '<input class="co-filter-input" id="' + field.id + '" type="' + type + '"' + maxlength +
      ' data-ss-text="1"' + (field.numeric ? ' inputmode="numeric"' : "") +
      ' autocomplete="off" value="' + escapeAttr(valueOf(state, field)) + '">' +
      hint +
      readError +
      "</div>"
    );
  }

  function toggleMarkup(state, field, focusedId) {
    var settings = ensureState(state);
    var locked = field.key === LISTING_KEY && settings.listingLocked === true;
    var on = !locked && valueOf(state, field) === true;
    var hint = field.hint ? '<p class="co-ss-hint">' + escapeHtml(field.hint) + "</p>" : "";
    var lockHint = locked && field.lockHint
      ? '<p class="co-ss-hint co-ss-refusal" id="co-ss-listing-lock">' + escapeHtml(field.lockHint) + "</p>"
      : "";
    return (
      '<div class="co-steam-row co-ss-row' + (field.wide ? " co-ss-row--wide" : "") + (field.id === focusedId ? " focused" : "") + '" data-ss-field="' + field.id + '">' +
      '<span class="co-ss-label">' + escapeHtml(field.label) + "</span>" +
      '<button type="button" class="co-btn co-ss-toggle" id="' + field.id + '" data-ss-toggle="1" aria-pressed="' +
      (on ? "true" : "false") + '"' + (locked ? ' disabled aria-disabled="true"' : "") + ">" + (on ? "On" : "Off") + "</button>" +
      hint +
      lockHint +
      "</div>"
    );
  }

  function section(state, focusedId) {
    var settings = ensureState(state);
    var parts = [];
    parts.push('<section class="co-steam-card co-ss-card">');
    parts.push('<h3 class="co-steam-title">Server settings</h3>');
    parts.push(
      '<p class="co-steam-desc">Saved to <code>commonwealth-server.json</code> with the server\'s own validation. ' +
      "Every setting is applied to the running server straight away, except the resume token lifetime and the directory URL, " +
      "which the running server keeps until its next start; when no server is running the saved file is used at the next Host.</p>"
    );
    var path = settings.configPath ? settings.configPath : "no server folder yet";
    parts.push('<p class="co-ss-path" id="co-ss-path">File: ' + escapeHtml(path) + "</p>");

    if (settings.readable === false) {
      parts.push(
        '<p class="co-ss-hint" id="co-ss-unreadable">The file could not be read, so these fields are blank. Fix the file in the folder ' +
        "above, or point the panel at another folder and press Reload.</p>"
      );
    }

    if (settings.worldModeRefusal) {
      parts.push('<p class="co-ss-refusal" id="co-ss-world-mode-refusal">' + escapeHtml(settings.worldModeRefusal) + "</p>");
    }

    var listed = (settings.errors || []).filter(function (error) {
      return error !== settings.folderError;
    });
    if (listed.length) {
      parts.push('<ul class="co-ss-errors" id="co-ss-errors">');
      listed.forEach(function (error) {
        parts.push("<li>" + escapeHtml(error) + "</li>");
      });
      parts.push("</ul>");
    }

    parts.push('<div class="co-ss-fields">');
    FIELDS.forEach(function (field) {
      if (field.type === "select") parts.push(selectMarkup(state, field, focusedId));
      else if (field.type === "toggle") parts.push(toggleMarkup(state, field, focusedId));
      else parts.push(textMarkup(state, field, focusedId));
    });
    parts.push("</div>");

    parts.push(
      '<p class="co-ss-hint">Starting inventory is not editable here: the server keeps <code>starting_inventory</code> ' +
      "exactly as it is in the file.</p>"
    );
    parts.push(
      '<p class="co-ss-hint">The modpack gate is set with <code>required_modpack_path</code> and the optional ' +
      "<code>required_modpack_url</code> in the same file. It is a compatibility check, not anti-cheat: it catches a " +
      "different plugin set or load order, and a deliberately faked fingerprint is accepted by design.</p>"
    );
    parts.push(
      '<div class="co-steam-actions">' +
      '<button type="button" class="co-btn' + (RELOAD_ID === focusedId ? " focused" : "") + '" id="' + RELOAD_ID + '">Reload</button>' +
      '<button type="button" class="co-btn co-btn-primary' + (SAVE_ID === focusedId ? " focused" : "") + '" id="' + SAVE_ID + '">Save</button>' +
      "</div>"
    );
    if (settings.status) {
      parts.push('<p class="co-steam-status" id="co-ss-status">' + escapeHtml(settings.status) + "</p>");
    }
    parts.push("</section>");
    return parts.join("");
  }

  function isNumeric(field) {
    return field.numbers === true || field.numeric === true;
  }

  function valuesFromState(state) {
    var values = {};
    FIELDS.forEach(function (field) {
      if (!field.key || field.key === "passwordClear") return;
      if (field.type === "toggle") values[field.key] = valueOf(state, field) === true;
      else if (isNumeric(field)) values[field.key] = parseInt(valueOf(state, field), 10);
      else values[field.key] = String(valueOf(state, field) || "");
    });
    return values;
  }

  function readPayload(root, state) {
    var settings = ensureState(state);
    var passwordField = root.querySelector("#" + PASSWORD_ID);
    var password = passwordField && passwordField.value ? passwordField.value : "";
    var mode = "keep";
    if (settings.passwordClear === true) mode = "clear";
    else if (password) mode = "set";
    return {
      serverDirectory: String(valueOf(state, fieldById(FOLDER_ID)) || "").trim(),
      values: valuesFromState(state),
      passwordMode: mode,
      password: password
    };
  }

  function fieldLabel(key) {
    for (var index = 0; index < FIELDS.length; index += 1) {
      if (FIELDS[index].key === key) return FIELDS[index].label;
    }
    return key;
  }

  function applyResult(state, event) {
    var settings = ensureState(state);
    settings.loading = false;
    if (event && event.action === "serverSettingsSave") settings.saving = false;
    settings.folderError = "";
    if (!settings.pendingSave && event && event.action === "serverSettingsGet" && event.data &&
        !event.data.serverDirectory && settings.folder) {
      settings.folderError = firstReplyError({ errors: event.data.errors || [] }, "That server folder could not be read.");
      settings.status = "";
      return settings;
    }
    if (event && event.data && event.data.readable === false) {
      settings.loaded = true;
      settings.readable = false;
      settings.values = null;
      settings.limits = event.data.limits || settings.limits;
      settings.folder = event.data.serverDirectory || "";
      settings.configPath = event.data.configPath || "";
      settings.draft = {};
      settings.folderDraft = "";
      settings.folderDraftSet = false;
      settings.passwordClear = false;
      settings.worldModeRefusal = "";
      settings.listingLocked = event.data.publicListingLocked === true;
      settings.errors = event.data.errors || [];
      settings.status = "";
    } else if (event && event.data && event.data.values) {
      settings.readable = true;
      settings.loaded = true;
      settings.values = event.data.values;
      settings.limits = event.data.limits || settings.limits;
      settings.folder = event.data.serverDirectory || "";
      settings.configPath = event.data.configPath || "";
      settings.draft = {};
      settings.folderDraft = "";
      settings.folderDraftSet = false;
      settings.passwordClear = false;
      settings.worldModeRefusal = event.data.worldModeRefusal || "";
      settings.listingLocked = event.data.publicListingLocked === true;
      settings.errors = event.data.errors || [];
      if (event.action === "serverSettingsSave" && event.success === true) {
        settings.passwordSaved = true;
        settings.lastPasswordIntent = "";
        if (event.data.liveApplied === true) {
          var applied = event.data.liveResult && event.data.liveResult.applied ? event.data.liveResult.applied : [];
          var refused = event.data.liveResult && event.data.liveResult.errors ? event.data.liveResult.errors : [];
          settings.status = applied.length
            ? "Applied to the running server: " + applied.map(fieldLabel).join(", ") + "."
            : "Applied to the running server.";
          if (refused.length) {
            settings.status += " Saved, but not applied live: " + refused.map(function (entry) {
              return fieldLabel(entry && entry.field) + " (" + (entry && entry.message ? entry.message : "refused") + ")";
            }).join("; ") + ". It takes effect at the next Host.";
          }
        } else {
          settings.status = "Saved. No server is running, so the file is used when you host.";
        }
      } else if (event.action === "serverSettingsSave") {
        settings.status = "";
        settings.lastPasswordIntent = "";
      } else {
        settings.status = settings.errors.length ? "" : "Loaded from the server folder.";
      }
    }
    var replyErrors = event && event.errors ? event.errors : [];
    if (replyErrors.length) {
      settings.errors = replyErrors;
      settings.status = "";
      if (event.action === "serverSettingsSave" && settings.lastPasswordIntent) {
        var hadPassword = settings.values && settings.values.passwordConfigured === true;
        settings.status = settings.lastPasswordIntent === "set"
          ? "Nothing was saved, so " + (hadPassword ? "the file still has the password it had" : "the file still has no password") +
            ". The new password is still in the field, and Save will send it again."
          : "Nothing was saved, so the file still has the saved password. " +
            "The remove-password request is still on, and Save will send it again.";
      }
    }
    return settings;
  }

  function capturePassword(container, state) {
    var field = container.querySelector("#" + PASSWORD_ID);
    var value = field && field.value ? field.value : "";
    if (!value || !state) return value;
    var settings = ensureState(state);
    if (settings.passwordSaved !== true) return value;
    settings.passwordSaved = false;
    return "";
  }

  function restorePassword(container, value) {
    if (!value) return;
    var field = container.querySelector("#" + PASSWORD_ID);
    if (field) field.value = value;
  }

  function create(deps) {
    var root = deps.root;
    var state = deps.state;
    var rerender = deps.render;
    var sendAction = deps.sendAction;
    ensureState(state);

    function ensureLoaded() {
      var settings = ensureState(state);
      if (settings.loaded || settings.loading) return settings;
      settings.loading = true;
      sendAction("serverSettingsGet", {});
      return settings;
    }

    function reload() {
      var settings = ensureState(state);
      if (settings.saving) return;
      settings.loading = true;
      settings.status = "";
      settings.folderError = "";
      settings.pendingSave = null;
      settings.pendingBefore = null;
      sendAction("serverSettingsGet", { serverDirectory: settings.folderDraftSet ? settings.folderDraft : settings.folder });
      rerender();
    }

    function save() {
      var settings = ensureState(state);
      if (settings.pendingSave || settings.saving) return;
      var payload = readPayload(root, state);
      if (!payload.serverDirectory) {
        settings.errors = ["Set the server folder that holds CommonwealthOnline.Server.exe."];
        rerender();
        return;
      }
      if (!sameFolder(payload.serverDirectory, settings.folder)) {
        settings.errors = [];
        settings.status = "Reading " + payload.serverDirectory + " before saving...";
        settings.pendingSave = {
          folder: payload.serverDirectory,
          draft: Object.assign({}, settings.draft),
          passwordClear: settings.passwordClear === true,
          passwordMode: payload.passwordMode,
          password: payload.password
        };
        settings.pendingBefore = {
          folder: settings.folder,
          configPath: settings.configPath,
          values: settings.values,
          readable: settings.readable,
          limits: settings.limits
        };
        sendAction("serverSettingsGet", { serverDirectory: payload.serverDirectory });
        rerender();
        return;
      }
      if (settings.readable === false) {
        settings.errors = ["commonwealth-server.json in this folder could not be read, so nothing was saved. " +
          "Fix the file and press Reload, or type another folder and press Save to read and write it."];
        rerender();
        return;
      }
      settings.lastPasswordIntent = payload.passwordMode === "keep" ? "" : payload.passwordMode;
      settings.saving = true;
      sendAction("serverSettingsSave", payload);
      settings.status = "Saving...";
      rerender();
    }

    function applyResultAndSave(event) {
      var settings = applyResult(state, event);
      if (!settings.pendingSave || !event || event.action !== "serverSettingsGet") return settings;
      var pending = settings.pendingSave;
      var before = settings.pendingBefore;
      settings.pendingSave = null;
      settings.pendingBefore = null;
      if (!sameFolder(settings.folder, pending.folder)) {
        settings.folderError = firstReplyError(settings, "That server folder could not be read.");
        if (before) {
          settings.folder = before.folder;
          settings.configPath = before.configPath;
          settings.values = before.values;
          settings.readable = before.readable;
          settings.limits = before.limits;
        }
        return settings;
      }
      if (settings.loaded !== true || settings.readable === false) {
        settings.folderError = firstReplyError(settings, "The server folder could not be read.");
        return settings;
      }
      settings.draft = Object.assign({}, pending.draft || {});
      settings.passwordClear = pending.passwordClear === true;
      settings.lastPasswordIntent = pending.passwordMode === "keep" ? "" : pending.passwordMode;
      settings.saving = true;
      sendAction("serverSettingsSave", {
        serverDirectory: settings.folder,
        values: valuesFromState(state),
        passwordMode: pending.passwordMode,
        password: pending.password
      });
      settings.status = "Saving...";
      return settings;
    }

    function clearStatus(state) {
      var settings = ensureState(state);
      if (!settings.status) return;
      settings.status = "";
      var node = root.querySelector("#co-ss-status");
      if (node && node.parentNode) node.parentNode.removeChild(node);
    }

    function clearFolderError(state) {
      ensureState(state).folderError = "";
      var node = root.querySelector("#co-ss-folder-error");
      if (node && node.parentNode) node.parentNode.removeChild(node);
    }

    function toggleField(id, force) {
      var field = fieldById(id);
      if (!field || field.type !== "toggle") return false;
      if (field.key === LISTING_KEY && ensureState(state).listingLocked === true) return false;
      var current = valueOf(state, field) === true;
      var next = typeof force === "boolean" ? force : !current;
      clearStatus(state);
      if (field.key === "passwordClear") ensureState(state).passwordClear = next;
      else ensureState(state).draft[field.key] = next;
      rerender();
      return true;
    }

    function selectField(id, value) {
      var field = fieldById(id);
      if (!field || field.type !== "select" || value === undefined || value === null) return false;
      clearStatus(state);
      ensureState(state).draft[field.key] = field.numbers ? parseInt(value, 10) : String(value);
      rerender();
      return true;
    }

    function textField(id, value) {
      var field = fieldById(id);
      if (!field || (field.type !== "text" && field.type !== "password")) return false;
      clearStatus(state);
      if (field.folder) {
        ensureState(state).folderDraft = String(value);
        ensureState(state).folderDraftSet = true;
        clearFolderError(state);
      }
      else if (field.type === "password") return true;
      else ensureState(state).draft[field.key] = String(value);
      return true;
    }

    return {
      ensureLoaded: ensureLoaded,
      reload: reload,
      save: save,
      toggleField: toggleField,
      selectField: selectField,
      textField: textField,
      applyResult: applyResultAndSave
    };
  }

  function bind(root, handlers) {
    var pane = root.querySelector('[data-pane="steam-friends"]');
    if (!pane) return;
    handlers = handlers || {};

    pane.addEventListener("click", function (e) {
      if (!e.target || !e.target.closest) return;
      if (e.target.closest("#" + SAVE_ID)) {
        if (typeof handlers.onSave === "function") handlers.onSave();
        return;
      }
      if (e.target.closest("#" + RELOAD_ID)) {
        if (typeof handlers.onReload === "function") handlers.onReload();
        return;
      }
      var toggle = e.target.closest("[data-ss-toggle]");
      if (toggle && typeof handlers.onToggle === "function") {
        handlers.onToggle(toggle.id);
      }
    });

    pane.addEventListener("input", function (e) {
      if (!e.target || !e.target.getAttribute || !e.target.getAttribute("data-ss-text")) return;
      if (typeof handlers.onText === "function") handlers.onText(e.target.id, e.target.value);
    });

    pane.addEventListener("change", function (e) {
      if (!e.target || !e.target.getAttribute || !e.target.getAttribute("data-ss-select")) return;
      if (typeof handlers.onSelect === "function") handlers.onSelect(e.target.id, e.target.value);
    });
  }

  function ownsField(id) {
    return ALL_IDS.indexOf(id) >= 0;
  }

  function activate(root, id, state) {
    var field = fieldById(id);
    if (id === SAVE_ID || id === RELOAD_ID) {
      var button = root.querySelector("#" + id);
      if (button) button.dispatchEvent({ type: "click", bubbles: true });
      return true;
    }
    if (!field) return false;
    var element = root.querySelector("#" + id);
    if (!element) return false;
    if (field.type === "select") {
      var options = optionsFor(ensureState(state || {}), field).map(function (option) { return String(option); });
      if (!options.length) return false;
      var index = options.indexOf(String(element.value));
      element.value = options[(index + 1) % options.length];
      element.dispatchEvent({ type: "change", bubbles: true });
      return true;
    }
    if (field.type === "toggle") {
      if (field.key === LISTING_KEY && ensureState(state || {}).listingLocked === true) return false;
      element.dispatchEvent({ type: "click", bubbles: true });
      return true;
    }
    if (window.CO_Keyboard) window.CO_Keyboard.open(element);
    return true;
  }

  function promptLabel(id) {
    if (id === SAVE_ID) return "Save";
    if (id === RELOAD_ID) return "Reload";
    var field = fieldById(id);
    if (!field) return "Select";
    return field.type === "select" || field.type === "toggle" ? "Change" : "Edit";
  }

  window.CO_ServerSettings = {
    FOLDER_ID: FOLDER_ID,
    SAVE_ID: SAVE_ID,
    RELOAD_ID: RELOAD_ID,
    PASSWORD_ID: PASSWORD_ID,
    CLEAR_ID: CLEAR_ID,
    FIELDS: FIELDS,
    create: create,
    section: section,
    bind: bind,
    fieldIds: function () { return ALL_IDS.slice(); },
    ownsField: ownsField,
    activate: activate,
    promptLabel: promptLabel,
    applyResult: applyResult,
    capturePassword: capturePassword,
    restorePassword: restorePassword,
    readPayload: readPayload,
    valueOf: valueOf,
    advertised: advertised,
    hostBlockReason: hostBlockReason,
    ensureState: ensureState
  };
})();
