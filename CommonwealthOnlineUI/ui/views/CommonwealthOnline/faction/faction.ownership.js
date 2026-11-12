(function () {
  "use strict";

  var STATE_RANK = { capturing: 0, contested: 1, owned: 2, neutral: 3 };

  var REJECT_TEXT = {
    too_far: "Move closer to that workbench to capture it.",
    hold_limit: "You already hold the maximum number of settlements.",
    already_yours: "You already own that settlement.",
    unknown_settlement: "That settlement is unknown to the server.",
    workbench_intact: "Destroy the owner's workbench before you can capture that settlement.",
    timed_out: "Capture timed out. Press H to try again.",
    not_allowed: "You cannot capture that settlement right now."
  };

  var REPAIR_TEXT = {
    underAttack: "You cannot repair while this settlement is under attack.",
    insufficientMaterials: "You do not have enough building materials for that. Nothing was taken.",
    nothingToRepair: "Nothing here needs repair.",
    chargeFailed: "The repair could not be paid for. Nothing was taken.",
    tooFar: "You are too far from the object. Walk up to it and try again.",
    notAllowed: "You are not allowed to do that here."
  };

  var PIECE_TEXT = {
    wood: ["wood", "wood"],
    scrapMetal: ["scrap metal", "steel"],
    concrete: ["concrete", "concrete"],
    reinforced: ["reinforced", "steel"]
  };

  function hasOwn(object, key) {
    return Object.prototype.hasOwnProperty.call(object, key);
  }

  function isId(value) {
    return typeof value === "number" && Number.isSafeInteger(value) && value > 0;
  }

  function duration(seconds) {
    var total = Math.max(0, Math.floor(Number(seconds) || 0));
    var minutes = Math.floor(total / 60);
    var rest = total % 60;
    return minutes > 0 ? minutes + " min " + rest + " s" : rest + " s";
  }

  function percent(row) {
    return Math.min(100, Math.max(0, Math.floor(Number(row.progressPercent) || 0)));
  }

  function rowsOf(capture) {
    if (!capture || !Array.isArray(capture.settlements)) {
      return [];
    }
    return capture.settlements.filter(function (row) {
      return row && typeof row === "object" && isId(row.buildZoneId) && hasOwn(STATE_RANK, row.state);
    });
  }

  function sortRows(capture) {
    var near = capture && capture.nearBuildZoneId;
    return rowsOf(capture).sort(function (a, b) {
      var byNear = (a.buildZoneId === near ? 0 : 1) - (b.buildZoneId === near ? 0 : 1);
      return byNear || STATE_RANK[a.state] - STATE_RANK[b.state] || a.buildZoneId - b.buildZoneId;
    });
  }

  function captureEnabled(row, capture) {
    return capture.promptCapture === true && row.buildZoneId === capture.nearBuildZoneId;
  }

  function cancelEnabled(capture) {
    return capture.localCapturing === true;
  }

  function statusText(row) {
    if (row.state === "capturing") {
      return "Being captured · " + percent(row) + "% · " + duration(row.secondsLeft) + " left";
    }
    if (row.state === "contested") {
      return "Contested, paused at " + percent(row) + "%";
    }
    return row.state === "owned" ? "Owned" : "Unclaimed";
  }

  function hintText(row, capture) {
    if (row.state === "capturing" || row.state === "contested") {
      return "";
    }
    if (row.buildZoneId !== capture.nearBuildZoneId) {
      return "Go to this workbench to capture it.";
    }
    return capture.promptCapture === true ? "" : "You cannot capture right now.";
  }

  function rowHtml(row, capture, esc) {
    var name = typeof row.name === "string" && row.name !== "" ? row.name : "Unnamed settlement";
    var zone = String(row.buildZoneId);
    var parts = [statusText(row)];
    var hint = hintText(row, capture);
    if (hint) {
      parts.push(hint);
    }
    return '<li class="faction-row"><span class="faction-row__name">' + esc(name) + "</span>" +
      '<span class="faction-muted">' + esc(parts.join(" · ")) + "</span>" +
      '<button type="button" class="faction-btn is-primary" data-op="capture" data-build-zone="' + esc(zone) + '"' +
      (captureEnabled(row, capture) ? "" : " disabled") + ">Capture</button></li>";
  }

  function resultLine(capture, esc) {
    var result = capture.lastResult;
    if (!result || typeof result !== "object" || result.ok === true || result.code === "no_attempt") {
      return "";
    }
    var message = hasOwn(REJECT_TEXT, result.code) ? REJECT_TEXT[result.code] : "Something went wrong.";
    return '<p class="faction-status is-error" role="status">' + esc(message) + "</p>";
  }

  function captureSection(s, esc) {
    var capture = s && s.settlementCapture;
    if (!capture || typeof capture !== "object" || !Array.isArray(capture.settlements)) {
      return '<div class="faction-section"><h2>OWNERSHIP</h2><p class="faction-muted">Ownership data is not available yet.</p></div>';
    }
    var rows = sortRows(capture);
    var body = resultLine(capture, esc) +
      (rows.length
        ? '<ul class="faction-list">' + rows.map(function (row) {
          return rowHtml(row, capture, esc);
        }).join("") + "</ul>"
        : '<p class="faction-muted">No settlements discovered yet. Find a workbench and press H to capture it.</p>') +
      '<div class="faction-actions"><button type="button" class="faction-btn is-danger" data-op="cancelCapture"' +
      (cancelEnabled(capture) ? "" : " disabled") + ">Cancel capture</button>" +
      '<button type="button" class="faction-btn" data-op="refreshCapture">Refresh</button></div>';
    return '<div class="faction-section"><h2>OWNERSHIP</h2>' + body + "</div>";
  }

  function isAllowedBuilder(row, profileId) {
    return Array.isArray(row.allowedProfileIds) && row.allowedProfileIds.indexOf(profileId) !== -1;
  }

  function accessRows(s) {
    var access = s && s.buildAccess;
    if (!access || !Array.isArray(access.settlements) || !s.selfProfileId) {
      return [];
    }
    return access.settlements.filter(function (row) {
      return row && isId(row.buildZoneId) && (row.ownerProfileId === s.selfProfileId || isAllowedBuilder(row, s.selfProfileId));
    });
  }

  function dueText(paidUntil) {
    if (!isId(paidUntil)) {
      return "Upkeep is not paid yet.";
    }
    var date = new Date(paidUntil * 1000).toLocaleString();
    return paidUntil * 1000 > Date.now() ? "Upkeep paid until " + date + "." : "Upkeep was due " + date + ". Pay now to stop decay.";
  }

  function refusalLine(access, zone, esc) {
    var reject = access.lastReject;
    if (!reject || typeof reject !== "object" || reject.buildZoneId !== zone || !hasOwn(REPAIR_TEXT, reject.status)) {
      return "";
    }
    return '<p class="faction-status is-error" role="status">' + esc(REPAIR_TEXT[reject.status]) + "</p>";
  }

  function repairRow(row, access, esc, selfId) {
    var zone = row.buildZoneId;
    var name = typeof row.name === "string" && row.name !== "" ? row.name : "Unnamed settlement";
    var zoneAttr = ' data-build-zone="' + esc(String(zone)) + '"';
    var damaged = (Array.isArray(access.damagedStructures) ? access.damagedStructures : []).filter(function (entry) {
      return entry && entry.buildZoneId === zone && isId(entry.objectId);
    });
    var destroyed = (Array.isArray(access.destroyedStructures) ? access.destroyedStructures : []).filter(function (entry) {
      return entry && entry.buildZoneId === zone && isId(entry.objectId) && hasOwn(PIECE_TEXT, entry.pieceType);
    });
    var hasBench = row.workbenchMaxHp > 0;
    var buttons = damaged.map(function (entry) {
      return '<button type="button" class="faction-btn" data-op="structureRepair"' + zoneAttr +
        ' data-object-id="' + esc(String(entry.objectId)) + '">Repair structure</button>';
    }).join("");
    buttons += destroyed.map(function (entry) {
      var piece = PIECE_TEXT[entry.pieceType];
      return '<button type="button" class="faction-btn" data-op="structureRebuild"' + zoneAttr +
        ' data-object-id="' + esc(String(entry.objectId)) + '">Rebuild ' + esc(piece[0]) + " (" +
        esc(String(entry.rebuildComponents)) + " " + esc(piece[1]) + ")</button>";
    }).join("");
    if (hasBench && row.workbenchHp < row.workbenchMaxHp) {
      buttons += '<button type="button" class="faction-btn" data-op="workbenchRepair"' + zoneAttr + ">Repair workbench</button>";
    }
    if (row.ownerProfileId === selfId) {
      buttons += '<button type="button" class="faction-btn is-primary" data-op="upkeepPay"' + zoneAttr + ">Pay upkeep</button>";
    }
    return '<div class="faction-row"><span class="faction-row__name">' + esc(name) + "</span>" +
      '<p class="faction-muted">' + esc(dueText(row.upkeepPaidUntil)) + "</p>" +
      (Number.isFinite(row.hourlyCost) && Number.isFinite(row.hoursCovered) ? '<p class="faction-muted">Upkeep: ' + Math.floor(row.hourlyCost) +
        " caps/hour, " + Math.floor(row.hoursCovered) + " h covered</p>" : "") +
      (row.decaying === true ? '<p class="faction-status is-error">Decaying</p>' : "") +
      (hasBench ? '<p class="faction-muted">Workbench ' + Math.floor(row.workbenchHp) + " / " + Math.floor(row.workbenchMaxHp) +
        (row.workbenchBroken === true ? ", broken" : "") + "</p>" : "") +
      refusalLine(access, zone, esc) +
      '<div class="faction-actions">' + buttons + "</div></div>";
  }

  function repairSection(s, esc) {
    var rows = accessRows(s);
    if (rows.length === 0) {
      return "";
    }
    return '<div class="faction-section"><h2>SETTLEMENT REPAIR</h2>' + rows.map(function (row) {
      return repairRow(row, s.buildAccess, esc, s.selfProfileId);
    }).join("") + "</div>";
  }

  function ownershipSection(s, esc) {
    return captureSection(s, esc) + repairSection(s, esc);
  }

  function nearRow(capture) {
    return rowsOf(capture).filter(function (row) {
      return row.buildZoneId === capture.nearBuildZoneId;
    })[0] || null;
  }

  function promptTitle(s) {
    var row = nearRow(s && s.settlementCapture);
    return row && typeof row.name === "string" && row.name !== "" ? row.name : "SETTLEMENT";
  }

  function promptSection(s, esc) {
    var capture = s && s.settlementCapture;
    var row = nearRow(capture);
    var full = '<button type="button" class="faction-btn" data-op="prompt-full">Full menu</button>';
    if (!row) {
      var ready = !!capture && Array.isArray(capture.settlements);
      return '<p class="faction-muted">' + (ready ? "No settlement found at this workbench." : "Settlement data is not available yet.") +
        '</p><div class="faction-actions">' + full + "</div>";
    }
    var owner = row.state === "owned" || row.state === "contested" ? row.ownerName : "";
    var hint = hintText(row, capture);
    return '<p class="faction-status">' + esc(statusText(row)) + "</p>" +
      (typeof owner === "string" && owner !== "" ? '<p class="faction-muted">Owned by ' + esc(owner) + "</p>" : "") +
      (row.state === "neutral" || row.state === "owned" ? '<p class="faction-desc">Do you want to capture this settlement?</p>' : "") +
      resultLine(capture, esc) +
      (hint ? '<p class="faction-muted">' + esc(hint) + "</p>" : "") +
      '<div class="faction-actions"><button type="button" class="faction-btn is-primary" data-op="capture" data-build-zone="' +
      esc(String(row.buildZoneId)) + '"' + (captureEnabled(row, capture) ? "" : " disabled") + ">Capture</button>" +
      (cancelEnabled(capture) ? '<button type="button" class="faction-btn is-danger" data-op="cancelCapture">Cancel capture</button>' : "") +
      full + "</div>";
  }

  window.coFactionOwnership = {
    ownershipSection: ownershipSection,
    sortRows: sortRows,
    captureEnabled: captureEnabled,
    cancelEnabled: cancelEnabled,
    promptTitle: promptTitle,
    promptSection: promptSection
  };
})();
