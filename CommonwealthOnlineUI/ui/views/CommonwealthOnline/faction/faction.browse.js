(function () {
  "use strict";

  function text(value) {
    return typeof value === "string" ? value.trim() : "";
  }

  function count(value) {
    var number = Math.floor(Number(value));
    return isFinite(number) && number > 0 ? number : 0;
  }

  function plural(number, word) {
    return number + " " + word + (number === 1 ? "" : "s");
  }

  function matches(entry, query) {
    if (!query) {
      return true;
    }
    return [entry.name, entry.tag, entry.leader].some(function (field) {
      return text(field).toLowerCase().indexOf(query) >= 0;
    });
  }

  function factionRow(entry, esc) {
    var tag = text(entry.tag);
    var description = text(entry.description);
    return (
      '<li class="faction-row"><span class="faction-browse__main">' +
      '<span class="faction-row__name">' + esc(text(entry.name)) + (tag ? ' <span class="faction-tag">[' + esc(tag) + "]</span>" : "") + "</span>" +
      '<span class="faction-muted">Leader: ' + esc(text(entry.leader)) + " · " + plural(count(entry.memberCount), "member") +
      " · " + count(entry.onlineCount) + " online</span>" +
      (description ? '<span class="faction-desc">' + esc(description) + "</span>" : "") +
      "</span>" +
      (entry.yours === true ? '<span class="faction-yours">YOURS</span>' : "") +
      "</li>"
    );
  }

  function factionsSection(s, view, esc, list) {
    var query = text(view.factionSearch).toLowerCase();
    var body;
    if (s.factionsLoaded !== true) {
      body = '<p class="faction-note">Loading factions...</p>';
    } else {
      var all = list(s.factions);
      var shown = all.filter(function (entry) {
        return matches(entry, query);
      });
      if (!all.length) {
        body = '<p class="faction-muted">No factions have been founded on this server yet.</p>';
      } else if (!shown.length) {
        body = '<p class="faction-muted">No factions match your search.</p>';
      } else {
        body = '<ul class="faction-list">' + shown.map(function (entry) {
          return factionRow(entry, esc);
        }).join("") + "</ul>";
      }
    }
    return (
      '<div class="faction-section"><h2>FACTIONS</h2>' +
      '<label class="faction-field"><span>Search</span>' +
      '<input id="coFactionSearch" type="text" maxlength="64" autocomplete="off" spellcheck="false" value="' +
      esc(view.factionSearch) + '"></label>' +
      body +
      '<div class="faction-actions"><button type="button" class="faction-btn" data-op="listFactions">Refresh list</button></div>' +
      "</div>"
    );
  }

  function inviteRow(entry, esc) {
    return (
      '<li class="faction-row"><span class="faction-row__name">' + esc(entry.name) + (entry.tag ? " [" + esc(entry.tag) + "]" : "") + "</span>" +
      '<button type="button" class="faction-btn is-primary" data-op="invite" data-target-player-id="' +
      esc(entry.playerId) + '">Invite</button></li>'
    );
  }

  function inviteSection(s, canInvite, esc, list) {
    if (!s.faction) {
      return '<div class="faction-section"><p class="faction-muted">Create or join a faction to invite players.</p></div>';
    }
    if (!canInvite) {
      return '<div class="faction-section"><p class="faction-muted">Only the leader and officers can invite players.</p></div>';
    }
    var memberIds = Object.create(null);
    list(s.faction.members).forEach(function (member) {
      memberIds[member.playerId] = true;
    });
    var rows = list(s.roster)
      .filter(function (entry) {
        return !memberIds[entry.playerId];
      })
      .map(function (entry) {
        return inviteRow(entry, esc);
      });
    return (
      '<div class="faction-section"><h2>INVITE PLAYERS</h2>' +
      (rows.length ? '<ul class="faction-list">' + rows.join("") + "</ul>" : '<p class="faction-muted">No players to invite.</p>') +
      "</div>"
    );
  }

  function invitesSection(invites, esc, list) {
    var rows = list(invites).map(function (invite) {
      return (
        '<li class="faction-row">' +
        '<span class="faction-row__name">' + esc(invite.factionName) + (invite.factionTag ? " [" + esc(invite.factionTag) + "]" : "") + "</span>" +
        '<span class="faction-muted">from ' + esc(invite.fromName) + " · " + esc(invite.expiresInSeconds) + "s</span>" +
        '<button type="button" class="faction-btn is-primary" data-op="inviteResponse" data-invite-id="' +
        esc(invite.inviteId) + '" data-accept="true">Accept</button>' +
        '<button type="button" class="faction-btn is-danger" data-op="inviteResponse" data-invite-id="' +
        esc(invite.inviteId) + '" data-accept="false">Decline</button>' +
        "</li>"
      );
    });
    return (
      '<div class="faction-section"><h2>PENDING INVITES</h2>' +
      (rows.length ? '<ul class="faction-list">' + rows.join("") + "</ul>" : '<p class="faction-muted">No pending invites.</p>') +
      "</div>"
    );
  }

  window.coFactionBrowse = { factionsSection: factionsSection, inviteSection: inviteSection, invitesSection: invitesSection };
})();
