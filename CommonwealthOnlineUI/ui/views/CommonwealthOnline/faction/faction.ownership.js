(function () {
  "use strict";

  var STATE_RANK = { capturing: 0, contested: 1, owned: 2, neutral: 3 };

  var REJECT_TEXT = {
    too_far: "Move closer to that workbench to capture it.",
    hold_limit: "You already hold the maximum number of settlements.",
    already_yours: "You already own that settlement.",
    unknown_settlement: "That settlement is unknown to the server.",
    not_allowed: "You cannot capture that settlement right now."
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
    return capture.canCapture === true && row.buildZoneId === capture.nearBuildZoneId &&
      row.state !== "capturing" && row.state !== "contested";
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
    return capture.canCapture === true ? "" : "You cannot capture right now.";
  }

  function rowHtml(row, capture, esc) {
    var zone = String(row.buildZoneId);
    var parts = [statusText(row)];
    var hint = hintText(row, capture);
    if (hint) {
      parts.push(hint);
    }
    return '<li class="faction-row"><span class="faction-row__name">' + esc("Settlement " + row.buildZoneId) + "</span>" +
      '<span class="faction-muted">' + esc(parts.join(" · ")) + "</span>" +
      '<button type="button" class="faction-btn is-primary" data-op="capture" data-build-zone="' + esc(zone) + '"' +
      (captureEnabled(row, capture) ? "" : " disabled") + ">Capture</button></li>";
  }

  function resultLine(capture, esc) {
    var result = capture.lastResult;
    if (!result || typeof result !== "object" || result.ok === true) {
      return "";
    }
    var message = hasOwn(REJECT_TEXT, result.code) ? REJECT_TEXT[result.code] : "Something went wrong.";
    return '<p class="faction-status is-error" role="status">' + esc(message) + "</p>";
  }

  function ownershipSection(s, esc) {
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
        : '<p class="faction-muted">No settlements are known yet.</p>') +
      '<div class="faction-actions"><button type="button" class="faction-btn is-danger" data-op="cancelCapture"' +
      (cancelEnabled(capture) ? "" : " disabled") + ">Cancel capture</button>" +
      '<button type="button" class="faction-btn" data-op="refreshCapture">Refresh</button></div>';
    return '<div class="faction-section"><h2>OWNERSHIP</h2>' + body + "</div>";
  }

  window.coFactionOwnership = {
    ownershipSection: ownershipSection,
    sortRows: sortRows,
    captureEnabled: captureEnabled,
    cancelEnabled: cancelEnabled
  };
})();
