(function () {
  "use strict";

  function healthClamp(health) {
    var value = Math.round(Number(health));
    if (!isFinite(value)) {
      return null;
    }
    return Math.max(0, Math.min(100, value));
  }

  function healthMarkup(member) {
    if (typeof member.health !== "number") {
      return "";
    }
    var health = healthClamp(member.health);
    if (health === null) {
      return "";
    }
    var condition = member.dead ? "is-dead" : (member.downed ? "is-downed" : "is-ready");
    var badge = member.dead ? "DEAD" : (member.downed ? "DOWNED" : "");
    return (
      '<span class="faction-health ' + condition + '" role="img" aria-label="' + (badge || health + "%") + '">' +
      '<span class="faction-health__fill" style="width:' + health + '%"></span></span>' +
      (badge ? '<span class="faction-condition ' + condition + '">' + badge + "</span>" : "")
    );
  }

  function memberSection(s, leader, officer, view, esc, list) {
    var rows = list(s.faction.members).map(function (member) {
      var self = typeof s.localPlayerId === "number" && member.playerId === s.localPlayerId;
      var promote = leader && !self
        ? (view.promoteArmed === member.profileId
          ? '<button type="button" class="faction-btn is-danger" data-op="promote" data-target-profile-id="' +
            esc(member.profileId) + '">Promote ' + esc(member.name) + ' to leader?</button><button type="button" class="faction-btn" data-op="cancel-promote">Cancel</button>'
          : '<button type="button" class="faction-btn" data-op="arm-promote" data-target-profile-id="' +
            esc(member.profileId) + '">Promote</button>')
        : "";
      var rank = leader && !self
        ? '<button type="button" class="faction-btn" data-op="setRank" data-target-profile-id="' +
          esc(member.profileId) + '" data-rank="' + (member.officer ? "member" : "officer") + '">' +
          (member.officer ? "Demote" : "Make officer") + "</button>"
        : "";
      var kick = !self && (leader || (officer && !member.leader && !member.officer))
        ? '<button type="button" class="faction-btn is-danger" data-op="kick" data-target-profile-id="' +
          esc(member.profileId) + '">Kick</button>'
        : "";
      return (
        '<li class="faction-row">' +
        '<span class="faction-dot ' + (member.online ? "is-online" : "is-offline") + '"></span>' +
        '<span class="faction-row__name">' + esc(member.name) + (self ? ' <span class="faction-muted">(you)</span>' : "") + "</span>" +
        healthMarkup(member) +
        (member.leader ? '<span class="faction-leader">LEADER</span>' : "") +
        (member.officer ? '<span class="faction-officer">OFFICER</span>' : "") +
        promote +
        rank +
        kick +
        "</li>"
      );
    });
    return '<div class="faction-section"><h2>MEMBERS</h2><ul class="faction-list">' + rows.join("") + "</ul></div>";
  }

  window.coFactionMembers = { memberSection: memberSection, healthMarkup: healthMarkup };
})();
