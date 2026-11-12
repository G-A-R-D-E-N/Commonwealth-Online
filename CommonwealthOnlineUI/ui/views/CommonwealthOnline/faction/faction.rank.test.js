const { assert, fs, source, harness, member, faction, roster, notInFaction, leaderState, memberState, plain } = require("./faction.harness.js");

const rankMembers = [
  { profileId: "p1", playerId: 3, name: "Nate", online: true, leader: true, officer: false },
  { profileId: "p2", playerId: 7, name: "Codsworth", online: true, leader: false, officer: true },
  { profileId: "p4", playerId: 8, name: "Dogmeat", online: true, leader: false, officer: false },
  { profileId: "p5", playerId: 9, name: "Hancock", online: true, leader: false, officer: true }
];
const rankFaction = { factionId: "f-1", name: "Minutemen Remnant", tag: "MMR", leaderPlayerId: 3, members: rankMembers };
const rankLeaderState = { enabled: true, localPlayerId: 3, faction: rankFaction, pendingInvites: [], roster, lastResult: null };
const rankOfficerState = { enabled: true, localPlayerId: 7, faction: rankFaction, pendingInvites: [], roster, lastResult: null };
const rankPlainState = { enabled: true, localPlayerId: 8, faction: rankFaction, pendingInvites: [], roster, lastResult: null };

function rowFor(html, name) {
  return html.split('<li class="faction-row">').find(function (row) {
    return row.indexOf(name + "</span>") >= 0;
  }) || "";
}

{
  const h = harness();
  h.setState(rankLeaderState);
  assert.match(rowFor(h.root.innerHTML, "Codsworth"), /<span class="faction-officer">OFFICER<\/span>/, "an officer row shows the OFFICER badge");
  assert.doesNotMatch(rowFor(h.root.innerHTML, "Dogmeat"), /faction-officer/, "a plain member row has no OFFICER badge");
  assert.doesNotMatch(rowFor(h.root.innerHTML, "Nate"), /faction-officer/, "the leader row has no OFFICER badge");
  assert.match(fs.readFileSync(__dirname + "/styles.css", "utf8"), /\.faction-officer \{/, "the OFFICER badge needs a style rule");
}

{
  const h = harness();
  h.setState(rankLeaderState);
  assert.match(h.root.innerHTML, /data-op="setRank" data-target-profile-id="p4" data-rank="officer">Make officer<\/button>/, "leader sees Make officer on a plain member");
  assert.match(h.root.innerHTML, /data-op="setRank" data-target-profile-id="p2" data-rank="member">Demote<\/button>/, "leader sees Demote on an officer");
  assert.doesNotMatch(h.root.innerHTML, /data-op="setRank" data-target-profile-id="p1"/, "leader must not set their own rank");
  h.click({ op: "setRank", targetProfileId: "p4", rank: "officer" });
  assert.deepEqual(plain(h.sent), [{ op: "setRank", targetProfileId: "p4", rank: "officer" }]);
}

{
  const h = harness();
  h.setState(rankOfficerState);
  assert.match(h.root.innerHTML, /data-op="kick" data-target-profile-id="p4"/, "an officer kicks a plain member");
  assert.doesNotMatch(h.root.innerHTML, /data-op="kick" data-target-profile-id="p1"/, "an officer must not kick the leader");
  assert.doesNotMatch(h.root.innerHTML, /data-op="kick" data-target-profile-id="p5"/, "an officer must not kick another officer");
  assert.doesNotMatch(h.root.innerHTML, /data-op="kick" data-target-profile-id="p2"/, "an officer must not kick self");
  h.click({ op: "tab", tab: "invite" });
  assert.match(h.root.innerHTML, /INVITE PLAYERS/, "an officer sees the invite roster");
  assert.match(h.root.innerHTML, /data-op="invite" data-target-player-id="5"/, "an officer can invite from the roster");
}

{
  const h = harness();
  h.setState(rankOfficerState);
  h.click({ op: "mine-tab", tab: "manage" });
  assert.doesNotMatch(h.root.innerHTML, /data-op="(promote|setRank|arm-disband|cancel-disband|disband)"/, "an officer sees no Promote, Disband or rank buttons");
}

{
  const h = harness();
  h.setState(rankPlainState);
  assert.doesNotMatch(h.root.innerHTML, /data-op="(promote|setRank|kick|invite|arm-disband|disband)"/, "a plain member sees no leader or officer controls");
  h.click({ op: "mine-tab", tab: "manage" });
  assert.match(h.root.innerHTML, /data-op="arm-leave"/, "a plain member keeps Leave");
  h.click({ op: "tab", tab: "invite" });
  assert.doesNotMatch(h.root.innerHTML, /data-op="invite"|INVITE PLAYERS/, "a plain member sees no invite roster on the Invite tab");
  assert.match(h.root.innerHTML, /Only the leader and officers can invite players\./);
}

{
  const h = harness();
  assert.equal(h.model.reasonText({ op: "setRank", ok: true }), "Rank updated.");
  assert.deepEqual(plain(h.model.actionFor({ op: "setRank", targetProfileId: "p2", rank: "member" })), { op: "setRank", targetProfileId: "p2", rank: "member" });
}

{
  const cpp = fs.readFileSync(__dirname + "/../../../../src/F4TFactionView.cpp", "utf8");
  const gate = cpp.match(/bool IsActionKnown\(std::string_view a_op\)\s*\{\s*return([\s\S]*?);/);
  assert.ok(gate, "F4TFactionView.cpp must keep IsActionKnown as the op gate");
  const known = Array.from(gate[1].matchAll(/a_op == "(\w+)"/g), (match) => match[1]);
  const emittedBlock = source.slice(source.indexOf("function actionFor(data)"), source.indexOf("function isLeader"));
  const emitted = Array.from(emittedBlock.matchAll(/case "(\w+)":/g), (match) => match[1]);
  assert.ok(emitted.includes("promote") && emitted.includes("disband"), "actionFor must list its ops as case labels");
  for (const op of emitted) {
    if (op === "close") {
      assert.match(cpp, /if \(op == "close"\)/, "close is handled before the op gate");
    } else {
      assert.ok(known.includes(op), `IsActionKnown must accept the ${op} op that faction.js emits`);
    }
  }
}

{
  const h = harness();
  assert.equal(h.model.reasonText({ op: "kick", ok: false, reason: "kicked" }), "You were removed from the faction.");
  assert.equal(h.model.reasonText({ op: "invite", ok: false, reason: "inviteExpired" }), "The invite expired before it was answered.");
}

{
  const h = harness();
  h.setState(memberState);
  assert.match(h.root.innerHTML, /Minutemen Remnant/);
  h.setState({ ...notInFaction, lastResult: { op: "kick", ok: false, reason: "kicked" } });
  assert.doesNotMatch(h.root.innerHTML, /Minutemen Remnant/, "a kicked member loses the faction view");
  assert.match(h.root.innerHTML, /CREATE FACTION/, "a kicked member sees the create form");
  assert.match(h.root.innerHTML, /<p class="faction-status is-error" role="status">You were removed from the faction\.<\/p>/);
}

{
  const h = harness();
  h.setState({ ...leaderState, lastResult: { op: "invite", ok: false, reason: "inviteExpired" } });
  assert.match(h.root.innerHTML, /<p class="faction-status is-error" role="status">The invite expired before it was answered\.<\/p>/);
}

{
  const h = harness();
  h.setState({
    ...leaderState,
    localPlayerId: 99,
    faction: {
      ...faction,
      members: [
        { profileId: "p1", playerId: 3, name: "Nate", online: true, leader: true, health: 42 },
        { profileId: "p2", playerId: 7, name: "Codsworth", online: true, leader: false, health: 12, downed: true },
        { profileId: "p3", playerId: 8, name: "Dogmeat", online: true, leader: false, health: 0, dead: true },
        { profileId: "p4", playerId: 9, name: "Hancock", online: false, leader: false }
      ]
    }
  });
  const html = h.root.innerHTML;
  assert.match(rowFor(html, "Nate"),
    /<span class="faction-health is-ready" role="img" aria-label="42%"><span class="faction-health__fill" style="width:42%"><\/span><\/span>/,
    "an online member shows a health bar at their percentage");
  assert.match(rowFor(html, "Codsworth"), /is-downed"[^>]*aria-label="DOWNED"/, "a downed member says DOWNED");
  assert.match(rowFor(html, "Codsworth"), /style="width:12%"/, "a downed member keeps their health bar");
  assert.match(rowFor(html, "Dogmeat"), /is-dead"[^>]*aria-label="DEAD"/, "a dead member says DEAD");
  assert.match(rowFor(html, "Dogmeat"), /style="width:0%"/, "a dead member keeps an empty bar");
  assert.doesNotMatch(rowFor(html, "Hancock"), /faction-health/, "a member with no reported health shows no bar");
  assert.doesNotMatch(rowFor(html, "Nate"), /faction-condition/, "a healthy member gets no condition badge");

  h.setState({
    ...leaderState,
    localPlayerId: 99,
    faction: { ...faction, members: [{ profileId: "p1", playerId: 3, name: "Nate", online: true, leader: true, health: 480 }] }
  });
  assert.match(h.root.innerHTML, /style="width:100%"/, "an out-of-range health is clamped instead of overflowing the bar");
}

console.log("Faction rank window regression passed");
