(function () {
  "use strict";

  var NAME_PATTERN = /^[A-Za-z0-9 '-]{3,24}$/;
  var TAG_PATTERN = /^[A-Z0-9]{2,4}$/;
  var DESCRIPTION_PATTERN = /^[\x20\x21\x23-\x5B\x5D-\x7E]*$/;
  var COLOR_PATTERN = /^#[0-9A-F]{6}$/;
  var PALETTE = ["#C0392B", "#E67E22", "#F1C40F", "#27AE60", "#16A085", "#2980B9", "#8E44AD", "#7F8C8D"];
  var TABS = [["factions", "Factions"], ["mine", "My faction"], ["invite", "Invite"], ["invites", "Invites"]];
  var MINE_TABS = [["members", "Members"], ["profile", "Profile"], ["build", "Build access"], ["ownership", "Ownership"], ["manage", "Manage"]];

  var REASON_TEXT = {
    alreadyInFaction: "You are already in a faction.",
    nameTaken: "That faction name is already taken.",
    invalidName: "That faction name is not allowed.",
    invalidTag: "That tag is not allowed. Use 2 to 4 capital letters or digits.",
    invalidDescription: "That description is not allowed.",
    invalidMotd: "That message of the day is not allowed.",
    tagTaken: "That faction tag is already taken.",
    identityUnavailable: "Your player identity is not available yet. Try again shortly.",
    factionCapacity: "The server has reached its faction limit.",
    factionIdConflict: "That faction id is already in use. Try again.",
    factionNotFound: "That faction no longer exists.",
    factionFull: "The faction is full.",
    persistenceUnavailable: "Faction data is unavailable right now. Try again later.",
    notLeader: "Only the faction leader can do that.",
    notMember: "You are not in a faction.",
    targetUnavailable: "That player is no longer available.",
    targetInFaction: "That player is already in a faction.",
    invalidTarget: "You cannot target that player.",
    inviteExists: "That player already has an invite pending.",
    inviteCapacity: "Too many invites are pending. Try again later.",
    inviteNotFound: "That invite no longer exists.",
    notInvited: "That invite was not sent to you.",
    rateLimited: "Too many requests. Try again in a moment.",
    declined: "The invite was declined.",
    kicked: "You were removed from the faction.",
    inviteExpired: "The invite expired before it was answered."
  };

  var DONE_TEXT = {
    create: "Faction created.",
    invite: "Invite sent.",
    inviteResponse: "Invite answered.",
    leave: "You left the faction.",
    kick: "Member removed.",
    promote: "Leadership passed on.",
    disband: "Faction disbanded.",
    setRank: "Rank updated.",
    setProfile: "Faction profile updated."
  };

  var state = null;
  var lastStateKey = null;
  var ui = {
    tab: "mine",
    mineTab: "members",
    factionSearch: "",
    name: "",
    tag: "",
    nameError: "",
    tagError: "",
    leaveArmed: false,
    disbandArmed: false,
    promoteArmed: "",
    profileDescription: null,
    profileMotd: null,
    profileColor: null,
    profileError: "",
    buildAccessTargets: {},
    buildAccessError: "",
    buildAccessErrorZone: ""
  };
  var root = null;

  function hasOwn(object, key) {
    return Object.prototype.hasOwnProperty.call(object, key);
  }

  function esc(text) {
    return String(text == null ? "" : text)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function trimmed(value) {
    return String(value == null ? "" : value).trim();
  }

  function list(value) {
    if (!Array.isArray(value)) {
      return [];
    }
    return value.filter(function (item) {
      return item && typeof item === "object";
    });
  }

  function validateName(name) {
    if (!NAME_PATTERN.test(trimmed(name))) {
      return "Use 3 to 24 letters, numbers, spaces, apostrophes or hyphens.";
    }
    return "";
  }

  function validateTag(tag) {
    var value = trimmed(tag).toUpperCase();
    if (value === "" || TAG_PATTERN.test(value)) {
      return "";
    }
    return "Tag must be 2 to 4 capital letters or digits.";
  }

  function createRequest(name, tag) {
    var nameError = validateName(name);
    var tagError = validateTag(tag);
    var action = null;
    if (!nameError && !tagError) {
      action = actionFor({ op: "create", name: trimmed(name), tag: trimmed(tag).toUpperCase() });
    }
    return { nameError: nameError, tagError: tagError, action: action };
  }

  function reasonText(result) {
    if (!result) {
      return "";
    }
    if (result.ok) {
      return hasOwn(DONE_TEXT, result.op) ? DONE_TEXT[result.op] : "Done.";
    }
    return hasOwn(REASON_TEXT, result.reason) ? REASON_TEXT[result.reason] : "Something went wrong.";
  }

  function actionFor(data) {
    switch (data.op) {
      case "create":
        return { op: "create", name: String(data.name || ""), tag: String(data.tag || "") };
      case "invite":
        return { op: "invite", targetPlayerId: Number(data.targetPlayerId) };
      case "inviteResponse":
        return { op: "inviteResponse", inviteId: String(data.inviteId), accept: data.accept === "true" };
      case "kick":
        return { op: "kick", targetProfileId: String(data.targetProfileId) };
      case "promote":
        return { op: "promote", targetProfileId: String(data.targetProfileId) };
      case "setRank":
        return { op: "setRank", targetProfileId: String(data.targetProfileId), rank: String(data.rank) };
      case "setProfile":
        return { op: "setProfile", description: String(data.description || ""), motd: String(data.motd || ""), color: String(data.color || "") };
      case "buildAccessGet":
        return { op: "buildAccessGet", buildZoneId: Number(data.buildZone) };
      case "buildAccessGrant":
      case "buildAccessRevoke":
        return { op: data.op, buildZoneId: Number(data.buildZone), targetProfileId: String(data.targetProfileId) };
      case "disband":
        return { op: "disband" };
      case "leave":
        return { op: "leave" };
      case "close":
      case "refresh":
      case "cancelCapture":
      case "refreshCapture":
      case "listFactions":
        return { op: data.op };
      case "capture":
        return { op: "capture", buildZoneId: Number(data.buildZone) };
      default:
        return null;
    }
  }

  function isLeader(s) {
    return !!(s.faction && typeof s.localPlayerId === "number" && s.faction.leaderPlayerId === s.localPlayerId);
  }

  function isOfficer(s) {
    return list(s.faction && s.faction.members).some(function (member) {
      return typeof s.localPlayerId === "number" && member.playerId === s.localPlayerId && member.officer === true;
    });
  }

  function validColor(value) {
    return COLOR_PATTERN.test(value) ? value : "";
  }

  function profileDraft(faction, view) {
    return {
      description: typeof view.profileDescription === "string" ? view.profileDescription : trimmed(faction.description),
      motd: typeof view.profileMotd === "string" ? view.profileMotd : trimmed(faction.motd),
      color: typeof view.profileColor === "string" ? view.profileColor : validColor(faction.color)
    };
  }

  function profileKey(s) {
    return s && s.faction ? JSON.stringify([s.faction.factionId, s.faction.description, s.faction.motd, s.faction.color]) : "";
  }

  function profileRequest(draft) {
    var description = trimmed(draft.description);
    var motd = trimmed(draft.motd);
    var error = "";
    if (description.length > 200 || !DESCRIPTION_PATTERN.test(description)) {
      error = "Use up to 200 plain characters, no quotes or backslashes.";
    } else if (motd.length > 1000 || !DESCRIPTION_PATTERN.test(motd)) {
      error = "The message of the day takes up to 1000 plain characters, no quotes or backslashes.";
    }
    var action = null;
    if (!error) {
      action = actionFor({ op: "setProfile", description: description, motd: motd, color: trimmed(draft.color).toUpperCase() });
    }
    return { error: error, action: action };
  }

  function panel(title, body, extra) {
    return (
      '<section class="faction-panel" aria-label="Factions">' +
      '<header class="faction-head"><h1 class="faction-title">' + title + "</h1></header>" +
      body +
      '<div class="faction-actions faction-footer">' + (extra || "") +
      '<button type="button" class="faction-btn" data-op="close">Close</button></div>' +
      "</section>"
    );
  }

  function pane(body) {
    return '<div class="faction-pane">' + body + "</div>";
  }

  function statusLine(result) {
    var text = reasonText(result);
    if (!text) {
      return "";
    }
    var cls = result.ok ? "faction-status" : "faction-status is-error";
    return '<p class="' + cls + '" role="status">' + esc(text) + "</p>";
  }

  function createSection(view) {
    return (
      '<div class="faction-section">' +
      "<h2>CREATE FACTION</h2>" +
      '<label class="faction-field"><span>Name</span>' +
      '<input id="coFactionName" type="text" maxlength="24" autocomplete="off" spellcheck="false" value="' +
      esc(view.name) + '"></label>' +
      '<p class="faction-error">' + esc(view.nameError) + "</p>" +
      '<label class="faction-field"><span>Tag (optional)</span>' +
      '<input id="coFactionTag" type="text" maxlength="4" autocomplete="off" spellcheck="false" value="' +
      esc(view.tag) + '"></label>' +
      '<p class="faction-error">' + esc(view.tagError) + "</p>" +
      '<div class="faction-actions"><button type="button" class="faction-btn is-primary" data-op="create-submit">Create</button></div>' +
      "</div>"
    );
  }

  function profileLine(faction) {
    var description = trimmed(faction.description);
    var color = validColor(faction.color);
    var swatch = color ? '<span class="faction-swatch" style="background:' + color + '"></span>' : "";
    var about = description ? '<p class="faction-label">ABOUT</p><p class="faction-desc">' + esc(description) + "</p>" : "";
    if (!about && !swatch) {
      return "";
    }
    return '<div class="faction-profile">' + swatch + about + "</div>";
  }

  function motdBlock(faction) {
    var motd = trimmed(faction.motd);
    return motd ? '<section class="faction-motd-block" aria-label="Message of the day"><p class="faction-label">MESSAGE OF THE DAY</p><p class="faction-motd">' + esc(motd) + "</p></section>" : "";
  }

  function profileSection(faction, view) {
    var draft = profileDraft(faction, view);
    var palette = PALETTE.map(function (color) {
      var picked = color === draft.color;
      return '<button type="button" class="faction-btn faction-swatch-pick' + (picked ? " is-selected" : "") +
        '" data-op="pick-color" data-color="' + color + '" aria-label="Colour ' + color + '" aria-pressed="' +
        (picked ? "true" : "false") + '" style="background:' + color + '"></button>';
    }).join("");
    return (
      '<div class="faction-section"><h2>FACTION PROFILE</h2>' +
      '<label class="faction-field"><span>Description</span>' +
      '<input id="coFactionDescription" type="text" maxlength="200" autocomplete="off" spellcheck="false" value="' +
      esc(draft.description) + '"></label>' +
      '<label class="faction-field"><span>Message of the day</span>' +
      '<textarea id="coFactionMotd" rows="7" maxlength="1000" autocomplete="off" spellcheck="false">' +
      esc(draft.motd) + '</textarea></label>' +
      '<div class="faction-field"><span>Colour</span></div>' +
      '<div class="faction-palette">' + palette + "</div>" +
      '<p class="faction-error">' + esc(view.profileError) + "</p>" +
      '<div class="faction-actions"><button type="button" class="faction-btn" data-op="clear-color">Clear colour</button>' +
      '<button type="button" class="faction-btn is-primary" data-op="setProfile">Save</button></div>' +
      "</div>"
    );
  }

  function leaveSection(view) {
    if (!view.leaveArmed) {
      return '<div class="faction-actions"><button type="button" class="faction-btn is-danger" data-op="arm-leave">Leave faction</button></div>';
    }
    return (
      '<div class="faction-actions"><span class="faction-muted">Leave this faction?</span>' +
      '<button type="button" class="faction-btn is-danger" data-op="leave">Confirm leave</button>' +
      '<button type="button" class="faction-btn" data-op="cancel-leave">Cancel</button></div>'
    );
  }

  function disbandSection(view) {
    if (!view.disbandArmed) {
      return '<div class="faction-actions"><button type="button" class="faction-btn is-danger" data-op="arm-disband">Disband faction</button></div>';
    }
    return (
      '<div class="faction-actions"><span class="faction-muted">Disband this faction? Every member leaves.</span>' +
      '<button type="button" class="faction-btn is-danger" data-op="disband">Confirm disband</button>' +
      '<button type="button" class="faction-btn" data-op="cancel-disband">Cancel</button></div>'
    );
  }

  function refreshRow() {
    return '<button type="button" class="faction-btn" data-op="refresh">Refresh</button>';
  }

  function tabBar(s, view) {
    var invites = list(s.pendingInvites).length;
    var tabs = TABS.map(function (tab) {
      var label = tab[0] === "invites" && invites ? tab[1] + " (" + invites + ")" : tab[1];
      return '<button type="button" class="faction-subtab" role="tab" aria-selected="' +
        (tab[0] === view.tab ? "true" : "false") + '" data-op="tab" data-tab="' + tab[0] + '">' + label + "</button>";
    });
    return '<nav class="faction-subtabs" role="tablist" aria-label="Faction sections">' + tabs.join("") + "</nav>";
  }

  function mineSection(s, view) {
    var build = window.coFactionBuild ? window.coFactionBuild.buildSection(s, view, esc, list) : "";
    var ownership = window.coFactionOwnership ? window.coFactionOwnership.ownershipSection(s, esc) : "";
    if (!s.faction) {
      return pane(createSection(view) + build + ownership);
    }
    var leader = isLeader(s);
    var tabs = MINE_TABS.filter(function (tab) { return tab[0] !== "profile" || leader; });
    var active = tabs.some(function (tab) { return tab[0] === view.mineTab; }) ? view.mineTab : "members";
    var bodies = {
      members: window.coFactionMembers.memberSection(s, leader, isOfficer(s), view, esc, list),
      profile: leader ? profileSection(s.faction, view) : "",
      build: build,
      ownership: ownership,
      manage: '<div class="faction-section"><h2>MANAGE</h2>' +
        '<div class="faction-card"><h3>LEAVE FACTION</h3><p class="faction-muted">Walk away from ' + esc(s.faction.name) +
        ". You can join or create another faction afterwards.</p>" + leaveSection(view) + "</div>" +
        (leader ? '<div class="faction-card"><h3>DISBAND FACTION</h3><p class="faction-muted">Ends the faction for every member. This cannot be undone.</p>' +
          disbandSection(view) + "</div>" : "") + "</div>"
    };
    var nav = tabs.map(function (tab) {
      return '<button type="button" class="faction-mtab" role="tab" aria-selected="' +
        (tab[0] === active ? "true" : "false") + '" data-op="mine-tab" data-tab="' + tab[0] + '">' + tab[1] + "</button>";
    }).join("");
    return '<div class="faction-mine"><aside class="faction-rail">' + profileLine(s.faction) +
      '<nav class="faction-mtabs" role="tablist" aria-label="My faction sections">' + nav + "</nav></aside>" +
      '<div class="faction-main">' + (active === "members" ? motdBlock(s.faction) : "") + pane(bodies[active]) + "</div></div>";
  }

  function tabBody(s, view) {
    if (view.tab === "factions") {
      return pane(window.coFactionBrowse.factionsSection(s, view, esc, list));
    }
    if (view.tab === "invite") {
      return pane(window.coFactionBrowse.inviteSection(s, isLeader(s) || isOfficer(s), esc, list));
    }
    if (view.tab === "invites") {
      return pane(window.coFactionBrowse.invitesSection(s.pendingInvites, esc, list));
    }
    return mineSection(s, view);
  }

  function renderState(s, view) {
    if (!s) {
      return panel("FACTIONS", '<p class="faction-note">Loading faction status...</p>');
    }
    if (s.enabled !== true) {
      return panel("FACTIONS", '<p class="faction-note">Factions are not available on this server</p>');
    }
    var tag = s.faction ? trimmed(s.faction.tag) : "";
    var title = s.faction ? esc(s.faction.name) + (tag ? ' <span class="faction-tag">[' + esc(tag) + "]</span>" : "") : "FACTIONS";
    return panel(title, tabBar(s, view) + statusLine(s.lastResult) + tabBody(s, view), refreshRow());
  }

  function paint() {
    if (!root || window.coFactionFocus.isKeyboardOpen()) {
      return;
    }
    var active = document.activeElement;
    var key = window.coFactionFocus.focusKey(active);
    var caret = active && typeof active.selectionStart === "number"
      ? [active.selectionStart, active.selectionEnd]
      : null;
    var motd = document.getElementById("coFactionMotd");
    var motdTop = motd ? motd.scrollTop : 0;

    var html;
    try {
      html = renderState(state, ui);
    } catch (err) {
      html = renderState({ enabled: false }, ui);
    }
    root.innerHTML = html + '<div class="co-keyboard hidden"></div>';

    motd = document.getElementById("coFactionMotd");
    if (motd) {
      motd.scrollTop = motdTop;
    }
    window.coFactionFocus.restoreFocus(root, key, caret);
    if (window.CO_Keyboard) {
      window.CO_Keyboard.render(root);
    }
  }

  function send(action) {
    if (typeof window.coFactionAction === "function") {
      window.coFactionAction(JSON.stringify(action));
    }
  }

  function close() {
    send({ op: "close" });
  }

  function submitCreate() {
    var result = createRequest(ui.name, ui.tag);
    ui.nameError = result.nameError;
    ui.tagError = result.tagError;
    paint();
    if (result.action) {
      send(result.action);
    }
  }

  function submitBuildAccess(zone) {
    if (!window.coFactionBuild) {
      return;
    }
    var result = window.coFactionBuild.approveRequest(zone, ui.buildAccessTargets[String(zone)]);
    ui.buildAccessError = result.error;
    ui.buildAccessErrorZone = String(zone);
    if (result.action) {
      delete ui.buildAccessTargets[String(zone)];
      send(result.action);
    }
    paint();
  }

  function submitProfile() {
    var result = profileRequest(profileDraft(state.faction, ui));
    ui.profileError = result.error;
    paint();
    if (result.action) {
      send(result.action);
    }
  }

  function setState(raw) {
    var next = raw;
    if (typeof raw === "string") {
      try {
        next = JSON.parse(raw);
      } catch (err) {
        return false;
      }
    }
    if (!next || typeof next !== "object" || Array.isArray(next)) {
      next = { enabled: false };
    }
    var key = null;
    try {
      key = JSON.stringify(next);
    } catch (err) {
      key = null;
    }
    if (key !== null && key === lastStateKey) {
      return true;
    }
    lastStateKey = key;
    if (next.enabled !== true && window.coFactionBuild && typeof window.coFactionBuild.resetCache === "function") {
      window.coFactionBuild.resetCache();
    }
    if (profileKey(next) !== profileKey(state)) {
      ui.profileDescription = null;
      ui.profileMotd = null;
      ui.profileColor = null;
      ui.profileError = "";
    }
    state = next;
    if (!state.faction) {
      ui.leaveArmed = false;
    }
    if (!isLeader(state)) {
      ui.disbandArmed = false;
      ui.promoteArmed = "";
    }
    if (!list(state.faction && state.faction.members).some(function (member) {
      return member.profileId === ui.promoteArmed;
    })) {
      ui.promoteArmed = "";
    }
    paint();
    return true;
  }

  function selectTab(tab) {
    if (!TABS.some(function (entry) { return entry[0] === tab; })) {
      return;
    }
    ui.tab = tab;
    paint();
    if (tab === "factions") {
      send({ op: "listFactions" });
    }
  }

  function onRootClick(event) {
    var target = event.target;
    var el = target && target.closest ? target.closest("[data-op]") : null;
    if (!el || !el.dataset || !el.dataset.op) {
      return;
    }
    var op = el.dataset.op;
    if (/^(arm|cancel)-(leave|disband|promote)$/.test(op)) {
      var armed = op.indexOf("arm-") === 0 ? op.slice(4) : "";
      ui.leaveArmed = armed === "leave";
      ui.disbandArmed = armed === "disband";
      ui.promoteArmed = armed === "promote" ? el.dataset.targetProfileId : "";
      paint();
      return;
    }
    if (op === "create-submit") {
      submitCreate();
      return;
    }
    if (op === "setProfile") {
      submitProfile();
      return;
    }
    if (op === "buildAccessApprove") {
      submitBuildAccess(el.dataset.buildZone);
      return;
    }
    if (op === "tab") {
      selectTab(el.dataset.tab);
      return;
    }
    if (op === "mine-tab") {
      if (MINE_TABS.some(function (entry) { return entry[0] === el.dataset.tab; })) {
        ui.mineTab = el.dataset.tab;
        paint();
      }
      return;
    }
    if (op === "clear-color") {
      ui.profileColor = "";
      paint();
      return;
    }
    if (op === "pick-color") {
      var picked = validColor(el.dataset.color);
      if (picked) {
        ui.profileColor = picked;
        paint();
      }
      return;
    }
    var action = actionFor(el.dataset);
    if (!action) {
      return;
    }
    if (op === "leave") {
      ui.leaveArmed = false;
    }
    if (op === "disband") {
      ui.disbandArmed = false;
    }
    if (op === "promote") {
      ui.promoteArmed = "";
    }
    send(action);
  }

  function onRootInput(event) {
    var target = event.target;
    if (target.id === "coFactionName") {
      ui.name = target.value;
    } else if (target.id === "coFactionTag") {
      ui.tag = target.value;
    } else if (target.id === "coFactionDescription") {
      ui.profileDescription = target.value;
    } else if (target.id === "coFactionMotd") {
      ui.profileMotd = target.value;
    } else if (target.id === "coFactionSearch") {
      ui.factionSearch = target.value;
      paint();
    } else if (target.id.indexOf("coBuildAccessTarget-") === 0) {
      ui.buildAccessTargets[target.id.slice("coBuildAccessTarget-".length)] = target.value;
    }
  }

  function focusables() {
    if (!root) {
      return [];
    }
    var voice = document.getElementById("coVoice");
    if (voice && !voice.hidden) {
      return Array.prototype.slice.call(voice.querySelectorAll("button:not([disabled])"));
    }
    return Array.prototype.slice.call(document.querySelectorAll("#coFaction button:not([disabled]), #coFaction input, #coFaction textarea"));
  }

  function moveFocus(delta) {
    var items = focusables();
    if (!items.length) {
      return;
    }
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
    if (!active) {
      return;
    }
    if ((active.tagName === "INPUT" || active.tagName === "TEXTAREA") && window.CO_Keyboard) {
      window.CO_Keyboard.open(active);
      window.CO_Keyboard.render(root);
    } else if (active.tagName === "BUTTON") {
      active.click();
    }
  }

  function onKeydown(event) {
    if (event.key === "Escape") {
      event.preventDefault();
      if (window.coFactionFocus.isKeyboardOpen()) {
        keyboardButton("b");
      } else {
        close();
      }
      return;
    }
    if (window.coFactionFocus.isKeyboardOpen()) {
      return;
    }
    if ((event.key === "ArrowDown" || event.key === "ArrowUp") && document.activeElement && document.activeElement.id === "coFactionMotd") {
      return;
    }
    if (event.key === "ArrowDown") {
      event.preventDefault();
      moveFocus(1);
    } else if (event.key === "ArrowUp") {
      event.preventDefault();
      moveFocus(-1);
    } else if (event.key === "Enter") {
      event.preventDefault();
      var active = document.activeElement;
      if (active && (active.id === "coFactionDescription" || active.id === "coFactionMotd")) {
        submitProfile();
      } else if (active && active.id && active.id.indexOf("coBuildAccessTarget-") === 0) {
        submitBuildAccess(active.id.slice("coBuildAccessTarget-".length));
      } else if (active && active.tagName === "INPUT" && active.id !== "coFactionSearch") {
        submitCreate();
      } else {
        activateFocused();
      }
    }
  }

  function handleControllerInput(button, phase) {
    if (phase !== "down") {
      return;
    }
    if (window.coFactionFocus.isKeyboardOpen()) {
      keyboardButton(button);
      return;
    }
    if (button === "lb" || button === "rb") {
      if (window.coVoiceToggleTab) {
        window.coVoiceToggleTab();
      }
      return;
    }
    if (button === "b") {
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
    var scale = Math.max(0.75, Math.min(1.35, window.innerWidth / 1920));
    document.documentElement.style.setProperty("--co-ui-scale", String(scale));
  }

  function init() {
    root = document.getElementById("coFaction");
    if (!root) {
      return;
    }
    root.addEventListener("click", onRootClick);
    root.addEventListener("input", onRootInput);
    document.addEventListener("keydown", onKeydown);
    updateUiScale();
    if (window.CO_Keyboard) {
      window.CO_Keyboard.onClosed = paint;
    }
    paint();
  }

  window.coFactionSetState = setState;
  window.handleControllerInput = handleControllerInput;
  window.coFactionModel = {
    validateName: validateName,
    validateTag: validateTag,
    createRequest: createRequest,
    reasonText: reasonText,
    actionFor: actionFor,
    renderState: renderState
  };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
