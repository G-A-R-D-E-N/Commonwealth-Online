(function () {
  "use strict";

  var PROFILE_PATTERN = /^[A-Za-z0-9._:@-]{8,128}$/;

  var REJECT_TEXT = {
    invalidRequest: "That build access request was not valid.",
    unknownBuildZone: "That settlement is unknown to the server.",
    notOwner: "Only the settlement owner can do that.",
    alreadyAllowed: "That player can already build there.",
    notAllowed: "That player is not on the build list.",
    full: "The build list is full (15 players).",
    rateLimited: "Too many build access requests. Try again in a moment.",
    persistenceUnavailable: "Build access data is unavailable right now. Try again later.",
    underAttack: "This settlement is under attack. Try again once it is over.",
    insufficientMaterials: "You do not have enough building materials for that. Nothing was taken.",
    tooFar: "You are too far from the workbench. Walk up to it and try again.",
    chargeFailed: "The settler could not be paid for. Nothing was taken."
  };

  var knownNames = {};
  var drafts = {};
  var served = {};
  var ASSIGNMENTS = ["guard", "farm", "scavenge", "purifier", "idle"];
  var OPEN_RAIDS_NOTE = '<p class="faction-note">Raids are open on this server.</p>';

  function hasOwn(object, key) {
    return Object.prototype.hasOwnProperty.call(object, key);
  }

  function rejectText(status) {
    return hasOwn(REJECT_TEXT, status) ? REJECT_TEXT[status] : "Something went wrong.";
  }

  function selfProfileId(s, list) {
    if (s && typeof s.selfProfileId === "string" && s.selfProfileId) {
      return s.selfProfileId;
    }
    if (!s || typeof s.localPlayerId !== "number") {
      return "";
    }
    var found = list(s.faction && s.faction.members).filter(function (member) {
      return member.playerId === s.localPlayerId && typeof member.profileId === "string";
    })[0];
    return found ? found.profileId : "";
  }

  function rememberNames(s, list) {
    list(s.faction && s.faction.members).forEach(function (member) {
      if (typeof member.profileId === "string" && typeof member.name === "string") {
        knownNames[member.profileId] = member.name;
      }
    });
  }

  function nameFor(profileId, s, list) {
    var found = list(s.faction && s.faction.members).filter(function (member) {
      return member.profileId === profileId && typeof member.name === "string";
    })[0];
    if (found) {
      return found.name;
    }
    return typeof profileId === "string" && hasOwn(knownNames, profileId) ? knownNames[profileId] : "";
  }

  function approveRequest(zone, target) {
    var buildZoneId = Math.floor(Number(zone));
    if (!isFinite(buildZoneId) || buildZoneId <= 0 || !Number.isSafeInteger(buildZoneId)) {
      return { error: "Unknown settlement.", action: null };
    }
    var value = String(target == null ? "" : target).trim();
    if (!PROFILE_PATTERN.test(value)) {
      return { error: "Enter a player profile id (8 to 128 letters, digits or . _ : @ -).", action: null };
    }
    return { error: "", action: { op: "buildAccessGrant", buildZoneId: buildZoneId, targetProfileId: value } };
  }

  function displayName(profileId, s, esc, list) {
    var name = nameFor(profileId, s, list);
    return name ? esc(name) : esc(profileId);
  }

  function ownerLine(entry, s, selfId, esc, list) {
    var owner = typeof entry.ownerProfileId === "string" ? entry.ownerProfileId : "";
    if (!owner) {
      return '<p class="faction-note">Owner: none. Anyone can build here.</p>';
    }
    var label = owner === selfId ? "you" : displayName(owner, s, esc, list);
    var verdict = "";
    if (!selfId) {
      verdict = " Your access is unknown.";
    } else if (owner === selfId) {
      verdict = "";
    } else if (allowedIds(entry).indexOf(selfId) >= 0) {
      verdict = " You can build here.";
    } else {
      verdict = " You cannot build here.";
    }
    return '<p class="faction-note">Owner: ' + label + "." + esc(verdict) + "</p>";
  }

  function allowedIds(entry) {
    if (!entry || !Array.isArray(entry.allowedProfileIds)) {
      return [];
    }
    return entry.allowedProfileIds.filter(function (id) {
      return typeof id === "string";
    });
  }

  function approvedRows(entry, s, owned, selfId, esc, list) {
    var ids = allowedIds(entry);
    if (!ids.length) {
      return '<p class="faction-muted">Nobody approved yet.</p>';
    }
    var zone = String(entry.buildZoneId);
    var rows = ids.map(function (id) {
      var revoke = owned
        ? '<button type="button" class="faction-btn is-danger" data-op="buildAccessRevoke" data-build-zone="' +
          esc(zone) + '" data-target-profile-id="' + esc(id) + '">Revoke</button>'
        : "";
      return '<li class="faction-row"><span class="faction-row__name">' + displayName(id, s, esc, list) + "</span>" + revoke + "</li>";
    });
    return '<ul class="faction-list">' + rows.join("") + "</ul>";
  }

  function candidateRows(entry, s, selfId, esc, list) {
    var zone = String(entry.buildZoneId);
    var allowed = allowedIds(entry);
    var rows = list(s.faction && s.faction.members)
      .filter(function (member) {
        return member.online === true &&
          typeof member.profileId === "string" &&
          member.profileId !== selfId &&
          allowed.indexOf(member.profileId) < 0;
      })
      .map(function (member) {
        return (
          '<li class="faction-row"><span class="faction-row__name">' + esc(member.name) + "</span>" +
          '<button type="button" class="faction-btn is-primary" data-op="buildAccessGrant" data-build-zone="' +
          esc(zone) + '" data-target-profile-id="' + esc(member.profileId) + '">Approve</button></li>'
        );
      });
    if (!rows.length) {
      return '<p class="faction-muted">No online players to approve.</p>';
    }
    return '<ul class="faction-list">' + rows.join("") + "</ul>";
  }

  function approveField(entry, view, esc) {
    var zone = String(entry.buildZoneId);
    var targets = view.buildAccessTargets && typeof view.buildAccessTargets === "object" ? view.buildAccessTargets : {};
    var value = typeof targets[zone] === "string" ? targets[zone] : "";
    var error = view.buildAccessErrorZone === zone
      ? '<p class="faction-error">' + esc(view.buildAccessError) + "</p>"
      : "";
    return (
      '<div class="faction-inline"><label class="faction-field"><span>Profile id</span>' +
      '<input id="coBuildAccessTarget-' + esc(zone) + '" type="text" maxlength="128" autocomplete="off" spellcheck="false" value="' +
      esc(value) + '"></label>' +
      '<button type="button" class="faction-btn is-primary" data-op="buildAccessApprove" data-build-zone="' +
      esc(zone) + '">Approve</button></div>' +
      error
    );
  }

  function settlementBlock(entry, s, selfId, view, esc, list) {
    var owned = typeof entry.ownerProfileId === "string" && selfId !== "" && entry.ownerProfileId === selfId;
    var zone = String(entry.buildZoneId);
    var builders = '<div class="settlement-col"><h3>APPROVED BUILDERS</h3>' + approvedRows(entry, s, owned, selfId, esc, list) + "</div>";
    var approve = owned
      ? '<div class="settlement-col"><h3>APPROVE ONLINE PLAYERS</h3>' +
        candidateRows(entry, s, selfId, esc, list) +
        approveField(entry, view, esc) + "</div>"
      : "";
    return (
      '<div class="faction-section settlement">' +
      '<div class="settlement-head"><h3>SETTLEMENT ' + esc(zone) + "</h3>" +
      ownerLine(entry, s, selfId, esc, list) +
      '<button type="button" class="faction-btn" data-op="buildAccessGet" data-build-zone="' +
      esc(zone) + '">Refresh</button></div>' +
      '<div class="settlement-cols">' + builders + approve + "</div>" +
      raidBlock(entry, s, owned, selfId, esc, list) +
      "</div>"
    );
  }

  function buildSection(s, view, esc, list) {
    if (!s || s.enabled !== true) {
      knownNames = {};
      return "";
    }
    var access = s.buildAccess;
    if (!access || typeof access !== "object" || !Array.isArray(access.settlements)) {
      return '<div class="faction-section"><h2>BUILD ACCESS</h2><p class="faction-muted">Build access data is not available yet.</p></div>';
    }
    var selfId = selfProfileId(s, list);
    rememberNames(s, list);
    var body = "";
    if (access.lastReject && typeof access.lastReject === "object") {
      body += '<p class="faction-status is-error" role="status">' + esc(rejectText(access.lastReject.status)) + "</p>";
    }
    if (s.settlementRaid && s.settlementRaid.refusal && typeof s.settlementRaid.refusal.text === "string") {
      body += '<p class="faction-status is-error" role="status">' + esc(s.settlementRaid.refusal.text) + "</p>";
    }
    var settlements = Array.isArray(access.settlements) ? access.settlements.filter(function (entry) {
      return entry && typeof entry === "object";
    }) : [];
    if (!settlements.length) {
      body += '<p class="faction-muted">No settlements you own yet.</p>';
    }
    body += settlements.map(function (entry) {
      return settlementBlock(entry, s, selfId, view || {}, esc, list);
    }).join("");
    if (access.truncated === true) {
      body += '<p class="faction-muted">Some settlements are hidden because the Social window is full.</p>';
    }
    if (s.settlementRaid && s.settlementRaid.truncated === true) {
      body += '<p class="faction-muted">Some settlement raid details are hidden because the Social window is full.</p>';
    }
    return '<div class="faction-section"><h2>BUILD ACCESS</h2>' + body + "</div>";
  }

  function draftValue(id, fallback) {
    return hasOwn(drafts, id) ? drafts[id] : fallback;
  }

  function syncDraft(id, servedValue) {
    if (hasOwn(served, id) && served[id] !== servedValue) {
      delete drafts[id];
    }
    served[id] = servedValue;
  }

  function rangeValues(from, to) {
    var values = [];
    for (var value = from; value <= to; value += 1) {
      values.push(String(value));
    }
    return values;
  }

  function selectOptions(values, current, esc) {
    var options = values.indexOf(current) < 0 ? values.concat([current]) : values;
    return options.map(function (value) {
      return '<option value="' + esc(value) + '"' + (value === current ? " selected" : "") + ">" + esc(value) + "</option>";
    }).join("");
  }

  function raidZone(s, zone) {
    var zones = s && s.settlementRaid && Array.isArray(s.settlementRaid.zones) ? s.settlementRaid.zones : [];
    return zones.filter(function (entry) {
      return entry && String(entry.buildZoneId) === zone;
    })[0] || null;
  }

  function raidsWindowed(s) {
    return !!(s && s.settlementRaid && s.settlementRaid.windowedRaids === true);
  }

  function windowControls(zone, info, esc) {
    var win = info && info.window ? info.window : null;
    var start = win ? String(win.windowStartHour) : "0";
    var hours = win ? String(win.windowDurationHours) : "2";
    syncDraft("coBuildWindowStart-" + zone, start);
    syncDraft("coBuildWindowHours-" + zone, hours);
    return '<div class="faction-inline">' +
      '<label class="faction-field"><span>Opens at (your hour)</span><select id="coBuildWindowStart-' + esc(zone) + '">' +
      selectOptions(rangeValues(0, 23), draftValue("coBuildWindowStart-" + zone, start), esc) + "</select></label>" +
      '<label class="faction-field"><span>Lasts (hours)</span><select id="coBuildWindowHours-' + esc(zone) + '">' +
      selectOptions(rangeValues(1, 24), draftValue("coBuildWindowHours-" + zone, hours), esc) + "</select></label>" +
      '<button type="button" class="faction-btn is-primary" data-op="settlementWindowSet" data-build-zone="' + esc(zone) +
      '" data-window-start="' + esc(start) + '" data-window-hours="' + esc(hours) + '">Set window</button></div>';
  }

  function settlerRows(zone, info, esc) {
    var settlers = info && Array.isArray(info.settlers) ? info.settlers : [];
    if (!settlers.length) {
      return '<p class="faction-muted">No settlers listed yet. Recruit one to start.</p>';
    }
    return '<ul class="faction-list">' + settlers.map(function (settler) {
      var id = String(Math.floor(Number(settler.settlerId)));
      var assignment = String(settler.assignment || "idle");
      var dead = settler.dead === true;
      var status = dead ? "dead" : esc(assignment) + ", " + esc(String(Math.round(Number(settler.health) || 0))) + " HP";
      syncDraft("coBuildSettlerTask-" + id, assignment);
      var controls = dead
        ? '<button type="button" class="faction-btn is-primary" data-op="settlerRecover" data-build-zone="' +
          esc(zone) + '" data-settler-id="' + esc(id) + '">Recover</button>'
        : '<select id="coBuildSettlerTask-' + esc(id) + '">' +
          selectOptions(ASSIGNMENTS, draftValue("coBuildSettlerTask-" + id, assignment), esc) + "</select>" +
          '<button type="button" class="faction-btn" data-op="settlerAssign" data-build-zone="' + esc(zone) +
          '" data-settler-id="' + esc(id) + '" data-assignment="' + esc(assignment) + '">Assign</button>';
      return '<li class="faction-row"><span class="faction-row__name">Settler ' + esc(id) + " (" + status + ")</span>" + controls + "</li>";
    }).join("") + "</ul>";
  }

  function raidClock(declared) {
    if (typeof declared.secondsLeft !== "number" || !isFinite(declared.secondsLeft)) {
      return "";
    }
    var total = Math.max(0, Math.floor(declared.secondsLeft));
    var clock = Math.floor(total / 60) + ":" + (total % 60 < 10 ? "0" : "") + (total % 60);
    return (declared.pending === true ? "Raid starts in " : "Raid live, ends in ") + clock + ".";
  }

  function raidBlock(entry, s, owned, selfId, esc, list) {
    var zone = String(entry.buildZoneId);
    var info = raidZone(s, zone);
    var declared = info && info.raid && typeof info.raid === "object" ? info.raid : null;
    var owner = typeof entry.ownerProfileId === "string" ? entry.ownerProfileId : "";
    var windowed = raidsWindowed(s);
    var body = "";
    if (declared) {
      var who = declared.declarerProfileId === selfId ? "you" : displayName(String(declared.declarerProfileId), s, esc, list);
      body += '<p class="faction-note">Raid declared by ' + who + ".</p>";
      body += '<p class="faction-note">' + raidClock(declared) + "</p>";
    }
    if (owned) {
      if (!declared) {
        body += windowed ? windowControls(zone, info, esc) : OPEN_RAIDS_NOTE;
      }
      body += settlerRows(zone, info, esc) +
        '<button type="button" class="faction-btn" data-op="settlerRecruit" data-build-zone="' + esc(zone) + '">Recruit settler</button>';
    } else if (owner && selfId && !declared && allowedIds(entry).indexOf(selfId) < 0) {
      body += windowed
        ? '<button type="button" class="faction-btn is-danger" data-op="raidDeclare" data-build-zone="' + esc(zone) + '">Declare raid</button>'
        : OPEN_RAIDS_NOTE;
    }
    return body ? '<div class="settlement-raid">' + body + "</div>" : "";
  }

  function settlementAction(data) {
    var zone = Math.floor(Number(data.buildZone));
    if (!Number.isSafeInteger(zone) || zone <= 0) {
      return null;
    }
    if (data.op === "raidDeclare") {
      return { op: "raidDeclare", buildZoneId: zone };
    }
    if (data.op === "settlementWindowSet") {
      var startKey = "coBuildWindowStart-" + zone;
      var hoursKey = "coBuildWindowHours-" + zone;
      var windowAction = {
        op: "settlementWindowSet",
        buildZoneId: zone,
        windowStartHour: Number(draftValue(startKey, String(data.windowStart || "0"))),
        windowDurationHours: Number(draftValue(hoursKey, String(data.windowHours || "2")))
      };
      delete drafts[startKey];
      delete drafts[hoursKey];
      return windowAction;
    }
    if (data.op === "settlerRecruit") {
      return { op: "settlerRecruit", buildZoneId: zone };
    }
    var settler = Math.floor(Number(data.settlerId));
    if (!Number.isSafeInteger(settler) || settler <= 0) {
      return null;
    }
    if (data.op === "settlerRecover") {
      return { op: "settlerRecover", buildZoneId: zone, settlerId: settler };
    }
    if (data.op === "settlerAssign") {
      var taskKey = "coBuildSettlerTask-" + settler;
      var assignAction = {
        op: "settlerAssign",
        buildZoneId: zone,
        settlerId: settler,
        assignment: String(draftValue(taskKey, String(data.assignment || "idle")))
      };
      delete drafts[taskKey];
      return assignAction;
    }
    return null;
  }

  function resetCache() {
    knownNames = {};
    drafts = {};
    served = {};
  }

  document.addEventListener("change", function (event) {
    var target = event.target;
    if (target && /^coBuild(WindowStart|WindowHours|SettlerTask)-/.test(String(target.id))) {
      drafts[target.id] = String(target.value);
    }
  });

  window.coFactionBuild = {
    REJECT_TEXT: REJECT_TEXT,
    approveRequest: approveRequest,
    buildSection: buildSection,
    nameFor: nameFor,
    rejectText: rejectText,
    resetCache: resetCache,
    selfProfileId: selfProfileId,
    settlementAction: settlementAction
  };
})();
