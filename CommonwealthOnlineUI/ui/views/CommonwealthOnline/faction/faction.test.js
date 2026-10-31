const { assert, fs, source, harness, member, faction, roster, notInFaction, leaderState, memberState, onlineRecruit, promoteState, plain } = require("./faction.harness.js");

{
  const h = harness();
  h.setState({ enabled: false });
  assert.match(h.root.innerHTML, /Factions are not available on this server/);
  assert.doesNotMatch(h.root.innerHTML, /data-op="(?!close")/);

  h.setState(JSON.stringify(notInFaction));
  assert.match(h.root.innerHTML, /CREATE FACTION/);
  h.click({ op: "tab", tab: "invites" });
  assert.match(h.root.innerHTML, /PENDING INVITES/);

  h.setState(JSON.stringify(leaderState));
  assert.match(h.root.innerHTML, /Minutemen Remnant/);
  assert.match(h.root.innerHTML, /\[MMR\]/);
}

{
  const h = harness();
  h.setState(notInFaction);
  h.click({ op: "tab", tab: "invites" });
  assert.match(h.root.innerHTML, /data-op="inviteResponse" data-invite-id="i-4" data-accept="true"/);
  assert.match(h.root.innerHTML, /Atom Cats \[CAT\]/);
  assert.match(h.root.innerHTML, /data-op="inviteResponse" data-invite-id="i-4" data-accept="false"/);
  assert.doesNotMatch(h.root.innerHTML, /data-op="kick"/);
  assert.doesNotMatch(h.root.innerHTML, /data-op="leave"/);
}

{
  const h = harness();
  h.setState({ ...notInFaction, pendingInvites: [{ inviteId: "i-5", factionName: "Tagless", fromName: "Zeke", expiresInSeconds: 10 }] });
  h.click({ op: "tab", tab: "invites" });
  assert.match(h.root.innerHTML, /Tagless</);
  assert.doesNotMatch(h.root.innerHTML, /Tagless \[/);
}

{
  const h = harness();
  h.setState(memberState);
  assert.doesNotMatch(h.root.innerHTML, /data-op="kick"/, "non-leader must not see Kick");
  assert.doesNotMatch(h.root.innerHTML, /data-op="invite"/, "non-leader must not see Invite");
  assert.doesNotMatch(h.root.innerHTML, /INVITE PLAYERS/, "non-leader must not see the invite list");
  assert.doesNotMatch(h.root.innerHTML, /data-op="arm-leave"/, "Leave lives on the Manage sub tab");
  h.click({ op: "mine-tab", tab: "manage" });
  assert.match(h.root.innerHTML, /data-op="arm-leave"/);
}

{
  const h = harness();
  h.setState(leaderState);
  assert.match(h.root.innerHTML, /data-op="kick" data-target-profile-id="p2"/);
  assert.doesNotMatch(h.root.innerHTML, /data-target-profile-id="p1"/, "leader must not kick self");
  h.click({ op: "tab", tab: "invite" });
  assert.match(h.root.innerHTML, /data-op="invite" data-target-player-id="5"/);
  assert.match(h.root.innerHTML, /Piper \[CA\]/, "roster row shows the player's faction tag");
  assert.doesNotMatch(h.root.innerHTML, /data-target-player-id="3"/, "roster must exclude the leader");
  assert.doesNotMatch(h.root.innerHTML, /data-target-player-id="7"/, "roster must exclude members");
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "manage" });
  h.click({ op: "arm-leave" });
  assert.deepEqual(h.sent, [], "arming leave must not send an action");
  assert.match(h.root.innerHTML, /data-op="leave"/);
  h.click({ op: "leave" });
  assert.deepEqual(plain(h.sent), [{ op: "leave" }]);
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "manage" });
  h.click({ op: "arm-leave" });
  h.click({ op: "cancel-leave" });
  assert.doesNotMatch(h.root.innerHTML, /data-op="leave"/);
}

{
  const h = harness();
  h.setState(promoteState);
  assert.match(h.root.innerHTML, /data-op="arm-promote" data-target-profile-id="p2"/, "leader sees Promote on an online member");
  assert.doesNotMatch(h.root.innerHTML, /data-op="arm-promote" data-target-profile-id="p2"[^>]* disabled/, "online member promote is enabled");
  assert.doesNotMatch(h.root.innerHTML, /data-target-profile-id="p1"[^>]*>Promote/, "leader must not promote self");
  h.click({ op: "arm-promote", targetProfileId: "p2" });
  assert.deepEqual(h.sent, [], "arming promote must not send an action");
  assert.match(h.root.innerHTML, /data-op="promote" data-target-profile-id="p2"[^>]*>Promote Codsworth to leader\?/, "arming promote names the target in the confirm");
  assert.doesNotMatch(h.root.innerHTML, /data-op="arm-promote"/, "an armed promote does not show Promote again");
  h.click({ op: "cancel-promote" });
  assert.match(h.root.innerHTML, /data-op="arm-promote" data-target-profile-id="p2"/, "cancel returns to Promote");
  h.click({ op: "arm-promote", targetProfileId: "p2" });
  h.click({ op: "promote", targetProfileId: "p2" });
  assert.deepEqual(plain(h.sent), [{ op: "promote", targetProfileId: "p2" }]);
  h.setState({ ...promoteState, lastResult: { op: "promote", ok: false, reason: "invalidTarget" } });
  assert.match(h.root.innerHTML, /data-op="arm-promote" data-target-profile-id="p2"/, "a sent promote clears its confirm");
}

{
  const h = harness();
  h.setState(leaderState);
  assert.match(h.root.innerHTML, /data-op="arm-promote" data-target-profile-id="p2">Promote</, "offline members can be promoted");
  assert.doesNotMatch(h.root.innerHTML, /data-op="arm-promote" data-target-profile-id="p2"[^>]* disabled/, "offline promote is not disabled");
  h.click({ op: "arm-promote", targetProfileId: "p2" });
  h.click({ op: "promote", targetProfileId: "p2" });
  assert.deepEqual(plain(h.sent), [{ op: "promote", targetProfileId: "p2" }], "an offline target's confirm sends promote");
}

{
  const h = harness();
  h.setState({ ...promoteState, faction: { ...faction, members: [member, { ...onlineRecruit, name: "Ann <b>&" }] } });
  h.click({ op: "arm-promote", targetProfileId: "p2" });
  assert.match(h.root.innerHTML, /Promote Ann &lt;b&gt;&amp; to leader\?/, "the armed name is escaped");
  assert.doesNotMatch(h.root.innerHTML, /Ann <b>/);
}

{
  const h = harness();
  h.setState(promoteState);
  h.click({ op: "arm-promote", targetProfileId: "p2" });
  h.setState({ ...promoteState, faction: { ...faction, members: [member] } });
  h.setState(promoteState);
  assert.match(h.root.innerHTML, /data-op="arm-promote" data-target-profile-id="p2"/, "an arm whose target left the faction is cleared");
  assert.doesNotMatch(h.root.innerHTML, /data-op="promote"/);
}

{
  const h = harness();
  h.setState(promoteState);
  h.click({ op: "arm-promote", targetProfileId: "p2" });
  h.click({ op: "mine-tab", tab: "manage" });
  h.click({ op: "arm-leave" });
  assert.match(h.root.innerHTML, /data-op="leave"/, "arming leave shows its confirm");
  h.click({ op: "mine-tab", tab: "members" });
  assert.doesNotMatch(h.root.innerHTML, /data-op="promote"/, "arming leave clears the promote arm");
  h.click({ op: "mine-tab", tab: "manage" });
  h.click({ op: "arm-disband" });
  assert.doesNotMatch(h.root.innerHTML, /data-op="leave"/, "arming disband clears the leave arm");
  assert.match(h.root.innerHTML, /data-op="disband"/);
  h.click({ op: "arm-promote", targetProfileId: "p2" });
  assert.doesNotMatch(h.root.innerHTML, /data-op="disband"/, "arming promote clears the disband arm");
  h.click({ op: "mine-tab", tab: "members" });
  assert.match(h.root.innerHTML, /data-op="promote" data-target-profile-id="p2"/);
  assert.deepEqual(h.sent, [], "arming never sends an action");
}

{
  const h = harness();
  h.setState(memberState);
  assert.doesNotMatch(h.root.innerHTML, /data-op="promote"/, "non-leader must not see Promote");
  h.click({ op: "mine-tab", tab: "manage" });
  assert.doesNotMatch(h.root.innerHTML, /data-op="(arm-|cancel-)?disband"/, "non-leader must not see Disband");
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "manage" });
  h.click({ op: "arm-disband" });
  assert.deepEqual(h.sent, [], "arming disband must not send an action");
  assert.match(h.root.innerHTML, /data-op="disband"/);
  h.click({ op: "cancel-disband" });
  assert.doesNotMatch(h.root.innerHTML, /data-op="disband"/);
  h.click({ op: "arm-disband" });
  h.click({ op: "disband" });
  assert.deepEqual(plain(h.sent), [{ op: "disband" }]);
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "manage" });
  h.click({ op: "arm-disband" });
  h.setState(notInFaction);
  h.setState(leaderState);
  assert.match(h.root.innerHTML, /data-op="arm-disband"/, "a disband confirm must not survive the faction ending");
  assert.doesNotMatch(h.root.innerHTML, /data-op="disband"/);
}

{
  const h = harness();
  assert.equal(h.model.reasonText({ op: "promote", ok: true }), "Leadership passed on.");
  assert.equal(h.model.reasonText({ op: "disband", ok: true }), "Faction disbanded.");
  assert.equal(h.model.reasonText({ op: "promote", ok: false, reason: "invalidTarget" }), "You cannot target that player.");
  assert.equal(h.model.reasonText({ op: "disband", ok: false, reason: "notLeader" }), "Only the faction leader can do that.");
  assert.deepEqual(plain(h.model.actionFor({ op: "promote", targetProfileId: "p2" })), { op: "promote", targetProfileId: "p2" });
  assert.deepEqual(plain(h.model.actionFor({ op: "disband" })), { op: "disband" });
}

{
  const h = harness();
  assert.notEqual(h.model.validateName("ab"), "", "two-character names fail");
  assert.notEqual(h.model.validateName(""), "", "empty names fail");
  assert.equal(h.model.validateName("Abc"), "");
  assert.equal(h.model.validateName("Mini's-Remnant 2"), "");
  assert.equal(h.model.validateName("  Abc  "), "", "padded names are trimmed before checking");
  assert.notEqual(h.model.validateName("   "), "", "all-space names fail");
  assert.notEqual(h.model.validateName(" \t  \t"), "", "all-whitespace names fail");
  assert.notEqual(h.model.validateName("A".repeat(25)), "");
  assert.notEqual(h.model.validateName("Bad!"), "");
  assert.equal(h.model.validateTag(""), "", "tag is optional");
  assert.equal(h.model.validateTag("   "), "", "whitespace-only tag counts as no tag");
  assert.equal(h.model.validateTag(" MMR "), "", "padded tag is trimmed before checking");
  assert.equal(h.model.validateTag("MMR"), "");
  assert.equal(h.model.validateTag("MMRR"), "");
  assert.notEqual(h.model.validateTag("M"), "");
  assert.equal(h.model.validateTag("mm"), "", "lowercase tags are upper-cased before validation");
  assert.notEqual(h.model.validateTag("MMRRR"), "");
  assert.notEqual(h.model.validateTag("M-R"), "");
}

{
  const h = harness();
  h.setState(notInFaction);
  h.input("coFactionName", "ab");
  h.click({ op: "create-submit" });
  assert.deepEqual(h.sent, [], "invalid create must not send");
  assert.match(h.root.innerHTML, /Use 3 to 24 letters/);

  h.input("coFactionName", "   ");
  h.click({ op: "create-submit" });
  assert.deepEqual(h.sent, [], "all-space create must not send");

  h.input("coFactionName", "Minutemen Remnant");
  h.input("coFactionTag", "MMR");
  h.click({ op: "create-submit" });
  assert.deepEqual(plain(h.sent), [{ op: "create", name: "Minutemen Remnant", tag: "MMR" }]);
}

{
  const expected = {
    alreadyInFaction: "You are already in a faction.",
    nameTaken: "That faction name is already taken.",
    invalidName: "That faction name is not allowed.",
    notLeader: "Only the faction leader can do that.",
    targetUnavailable: "That player is no longer available.",
    targetInFaction: "That player is already in a faction.",
    factionFull: "The faction is full.",
    inviteExists: "That player already has an invite pending.",
    rateLimited: "Too many requests. Try again in a moment.",
    notMember: "You are not in a faction.",
    declined: "The invite was declined."
  };
  const h = harness();
  for (const [reason, text] of Object.entries(expected)) {
    assert.equal(h.model.reasonText({ op: "invite", ok: false, reason }), text, reason);
  }
  assert.equal(h.model.reasonText({ op: "x", ok: false, reason: "mystery" }), "Something went wrong.");
  assert.equal(h.model.reasonText({ op: "create", ok: true }), "Faction created.");
  assert.equal(h.model.reasonText(null), "");
  h.setState({ ...notInFaction, lastResult: { op: "create", ok: false, reason: "nameTaken" } });
  assert.match(h.root.innerHTML, /That faction name is already taken\./);
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "invite", targetPlayerId: "5" });
  h.click({ op: "kick", targetProfileId: "p2" });
  h.click({ op: "close" });
  h.setState(notInFaction);
  h.click({ op: "inviteResponse", inviteId: "i-4", accept: "true" });
  h.click({ op: "inviteResponse", inviteId: "i-4", accept: "false" });
  assert.deepEqual(plain(h.sent), [
    { op: "invite", targetPlayerId: 5 },
    { op: "kick", targetProfileId: "p2" },
    { op: "close" },
    { op: "inviteResponse", inviteId: "i-4", accept: true },
    { op: "inviteResponse", inviteId: "i-4", accept: false }
  ]);
}

{
  const h = harness();
  assert.equal(h.keydown("Escape"), true);
  assert.deepEqual(plain(h.sent), [{ op: "close" }]);
  h.controller("b", "down");
  assert.deepEqual(plain(h.sent), [{ op: "close" }, { op: "close" }]);
  h.controller("b", "up");
  assert.equal(h.sent.length, 2, "release must not close again");
}

{
  const h = harness();
  h.setState(notInFaction);
  const writes = h.root.writes;
  h.setState(JSON.stringify(notInFaction));
  h.setState(plain(notInFaction));
  assert.equal(h.root.writes, writes, "identical state must not repaint");
  h.setState({ ...notInFaction, lastResult: { op: "invite", ok: true } });
  assert.equal(h.root.writes, writes + 1, "changed state must repaint");
}

{
  const h = harness();
  h.setState(notInFaction);
  const focused = [];
  h.root.onWrite = (target) => {
    target.elements["#coFactionName"] = {
      focus() { focused.push("focus"); },
      setSelectionRange(start, end) { focused.push([start, end]); }
    };
    target.elements['[data-op="inviteResponse"][data-invite-id="i-4"][data-accept="true"]'] = {
      focus() { focused.push("invite"); }
    };
  };
  h.document.activeElement = { id: "coFactionName", tagName: "INPUT", selectionStart: 2, selectionEnd: 2 };
  h.setState({ ...notInFaction, lastResult: { op: "create", ok: false, reason: "nameTaken" } });
  assert.deepEqual(focused, ["focus", [2, 2]], "input focus and caret restore after repaint");

  h.click({ op: "tab", tab: "invites" });
  focused.length = 0;
  h.document.activeElement = { dataset: { op: "inviteResponse", inviteId: "i-4", accept: "true" } };
  h.setState({ ...notInFaction, lastResult: { op: "invite", ok: true } });
  assert.deepEqual(focused, ["invite"], "button focus restores by data attributes");
}

{
  const h = harness();
  h.setState({
    enabled: true,
    localPlayerId: 3,
    faction: { name: "Ruined", tag: "RU", leaderPlayerId: 3, members: "nope" },
    roster: 5,
    pendingInvites: "x",
    lastResult: null
  });
  assert.match(h.root.innerHTML, /Ruined/);
  h.click({ op: "tab", tab: "invite" });
  assert.match(h.root.innerHTML, /No players to invite\./);
  assert.doesNotMatch(h.root.innerHTML, /data-op="kick"/);
}

{
  const h = harness();
  h.setState({
    enabled: true,
    localPlayerId: 1,
    faction: {
      get name() {
        throw new Error("boom");
      },
      tag: "RU",
      members: []
    },
    pendingInvites: [],
    roster: [],
    lastResult: null
  });
  assert.match(h.root.innerHTML, /Factions are not available on this server/);
}

{
  const h = harness();
  const hostile = '"><img src=x onerror=alert(1)></a></script>' + String.fromCharCode(0x2028) + '<b>' + String.fromCharCode(0x2029) + "'";
  h.setState({
    enabled: true,
    localPlayerId: 3,
    faction: {
      factionId: "f-9",
      name: hostile,
      tag: "<i>",
      leaderPlayerId: 3,
      members: [{ profileId: "p9", playerId: 3, name: hostile, online: true, leader: true }]
    },
    pendingInvites: [],
    roster: [],
    lastResult: null
  });
  assert.doesNotMatch(h.root.innerHTML, /<img/);
  assert.doesNotMatch(h.root.innerHTML, /<\/script>/);
  assert.doesNotMatch(h.root.innerHTML, /<b>/);
  assert.doesNotMatch(h.root.innerHTML, /<i>/);
  assert.match(h.root.innerHTML, /&quot;&gt;&lt;img src=x onerror=alert\(1\)&gt;/);
  assert.match(h.root.innerHTML, /&#39;/);
  assert.match(h.root.innerHTML, new RegExp("\u2028&lt;b&gt;\u2029"));
}

{
  const h = harness();
  h.setState(notInFaction);
  const name = h.field("coFactionName", "");
  h.document.activeElement = name;
  h.keydown("Enter");
  assert.deepEqual(h.sent, [], "empty name must not submit");
  name.value = "Abc";
  name.dispatchEvent();
  h.keydown("Enter");
  assert.deepEqual(plain(h.sent), [{ op: "create", name: "Abc", tag: "" }]);
}

{
  const h = harness();
  h.setState(notInFaction);
  const name = h.field("coFactionName", "");
  h.document.activeElement = name;
  h.controller("a", "down");
  assert.equal(h.keyboard.isOpen(), true, "A on a focused input opens the keyboard");
  assert.equal(h.keyboardHost.classList.hidden, false);
  h.controller("a", "down");
  h.controller("dpad_right", "down");
  h.controller("a", "down");
  h.controller("dpad_right", "down");
  h.controller("a", "down");
  assert.equal(name.value, "123", "keyboard presses reach the input value");
  assert.deepEqual(h.keyboard.getState().value, "123");
  for (let i = 0; i < 4; i += 1) {
    h.controller("dpad_down", "down");
  }
  h.controller("a", "down");
  assert.equal(h.keyboard.isOpen(), false, "Done closes the keyboard");
  assert.equal(h.keyboardHost.classList.hidden, true);
  h.click({ op: "create-submit" });
  assert.deepEqual(plain(h.sent), [{ op: "create", name: "123", tag: "" }], "typed value flows into the form");
}

{
  const h = harness();
  h.setState(notInFaction);
  const name = h.field("coFactionName", "");
  h.document.activeElement = name;
  h.controller("a", "down");
  h.controller("a", "down");
  assert.equal(name.value, "1");
  assert.equal(h.keydown("Escape"), true);
  assert.equal(h.keyboard.isOpen(), false, "Escape closes the keyboard first");
  assert.equal(name.value, "", "Escape cancels the keyboard edit");
  assert.deepEqual(h.sent, [], "Escape closing the keyboard must not close the window");
}

{
  const h = harness();
  h.setState({ ...notInFaction, localPlayerId: 9, faction: { ...faction, tag: "  " } });
  assert.doesNotMatch(h.root.innerHTML, /faction-tag/, "blank tag must not render a tag span");
  assert.match(h.root.innerHTML, /Minutemen Remnant/);
}

{
  const h = harness();
  h.setState(notInFaction);
  const focused = [];
  h.root.onWrite = (target) => {
    target.elements["#a\\5c b\\a c"] = { focus() { focused.push("focus"); } };
  };
  h.document.activeElement = { id: "a\\b\nc", tagName: "BUTTON" };
  h.setState({ ...notInFaction, lastResult: { op: "invite", ok: true } });
  assert.deepEqual(focused, ["focus"], "ids with a backslash and a newline restore by escaped selector");
}

{
  const h = harness();
  h.setState(notInFaction);
  h.root.failQuery = true;
  h.document.activeElement = { id: "coFactionName", tagName: "INPUT", selectionStart: 0, selectionEnd: 0 };
  h.setState({ ...notInFaction, lastResult: { op: "invite", ok: true } });
  assert.match(h.root.innerHTML, /Invite sent\./, "a failing selector must not abort the repaint");
}

{
  const h = harness();
  h.setState(notInFaction);
  const name = h.field("coFactionName", "");
  h.document.activeElement = name;
  h.controller("a", "down");
  h.setState({ ...notInFaction, lastResult: { op: "invite", ok: true } });
  assert.equal(h.keyboard.isOpen(), true, "a state push must not close the keyboard");
  assert.doesNotMatch(h.root.innerHTML, /Invite sent\./, "the pushed state waits for the keyboard to close");
  for (let i = 0; i < 4; i += 1) {
    h.controller("dpad_down", "down");
  }
  h.controller("dpad_right", "down");
  h.controller("dpad_right", "down");
  h.controller("a", "down");
  assert.equal(h.keyboard.isOpen(), false, "Done closes the keyboard");
  assert.match(h.root.innerHTML, /Invite sent\./, "the pending state paints when the keyboard closes");
  assert.equal(h.keyboardHost.classList.hidden, true);
}

{
  const h = harness();
  h.setState({ ...leaderState, localPlayerId: undefined });
  assert.doesNotMatch(h.root.innerHTML, /data-op="kick"/, "missing localPlayerId must not show leader controls");
  assert.doesNotMatch(h.root.innerHTML, /data-op="invite"/, "missing localPlayerId must not show Invite");
  assert.doesNotMatch(h.root.innerHTML, /\(you\)/, "missing localPlayerId must not mark a member as you");
  h.click({ op: "tab", tab: "invite" });
  assert.doesNotMatch(h.root.innerHTML, /data-op="invite"/, "missing localPlayerId must not show Invite on the Invite tab");
}

{
  const h = harness();
  assert.equal(h.model.validateTag("mmr"), "", "lowercase tags are upper-cased before validation");
  h.setState(notInFaction);
  h.input("coFactionName", "Minutemen");
  h.input("coFactionTag", "mmr");
  h.click({ op: "create-submit" });
  assert.deepEqual(plain(h.sent), [{ op: "create", name: "Minutemen", tag: "MMR" }]);
}

{
  const h = harness();
  const serverReasons = [
    "alreadyInFaction", "factionCapacity", "factionFull", "factionIdConflict", "factionNotFound",
    "identityUnavailable", "invalidDescription", "invalidName", "invalidTag", "invalidTarget", "inviteCapacity", "inviteExists",
    "inviteNotFound", "nameTaken", "notInvited", "notLeader", "notMember", "persistenceUnavailable",
    "rateLimited", "tagTaken", "targetInFaction", "targetUnavailable", "declined", "kicked", "inviteExpired"
  ];
  for (const reason of serverReasons) {
    assert.notEqual(h.model.reasonText({ op: "create", ok: false, reason }), "Something went wrong.", reason);
  }
}

{
  const h = harness();
  h.setState(notInFaction);
  assert.match(h.root.innerHTML, /data-op="refresh"/);
  h.click({ op: "refresh" });
  h.setState(leaderState);
  h.click({ op: "refresh" });
  assert.deepEqual(plain(h.sent), [{ op: "refresh" }, { op: "refresh" }], "refresh button forwards a refresh action");
}

{
  const markup = fs.readFileSync(__dirname + "/index.html", "utf8");
  assert.doesNotMatch(markup, /coTabChat|coChat/, "chat lives in its own on-screen window, never in the Social window");
  assert.doesNotMatch(source, /coChat/, "the Social window must not route input to a chat tab");
  assert.match(source, /querySelectorAll\("#coFaction button:not\(\[disabled\]\), #coFaction input, #coFaction textarea"\)/,
    "the faction focus list must cover the faction controls only");
}

console.log("Faction window regression passed");
