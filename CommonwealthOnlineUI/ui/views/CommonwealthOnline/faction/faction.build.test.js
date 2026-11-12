const { assert, harness, leaderState, promoteState, notInFaction, plain } = require("./faction.harness.js");

const VALID_ID = "co-12345678";
const OTHER_ID = "co-87654321";

function withAccess(base, settlements, lastReject, extra) {
  return { ...base, ...(extra || {}), buildAccess: { settlements, lastReject: lastReject || null, truncated: false } };
}

const owned = withAccess(leaderState, [{ buildZoneId: 7, ownerProfileId: "p1", allowedProfileIds: ["p2"] }]);
const ownedEmpty = withAccess(leaderState, [{ buildZoneId: 7, ownerProfileId: "p1", allowedProfileIds: [] }]);
const foreign = withAccess(leaderState, [{ buildZoneId: 12, ownerProfileId: "p9", allowedProfileIds: [] }]);
const guest = withAccess(leaderState, [{ buildZoneId: 12, ownerProfileId: "p9", allowedProfileIds: ["p1"] }]);
const openZone = withAccess(leaderState, [{ buildZoneId: 12, ownerProfileId: "", allowedProfileIds: [] }]);
const rejected = withAccess(owned, owned.buildAccess.settlements, { buildZoneId: 7, status: "rateLimited" });
const denied = withAccess(owned, owned.buildAccess.settlements, { buildZoneId: 7, status: "notOwner" });

{
  const h = harness();
  h.setState(owned);
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /BUILD ACCESS/);
  assert.match(h.root.innerHTML, /SETTLEMENT 7/);
  assert.match(h.root.innerHTML, /Owner: you/);
  assert.match(h.root.innerHTML, /Codsworth/);
  assert.match(h.root.innerHTML, /data-op="buildAccessRevoke" data-build-zone="7" data-target-profile-id="p2"/);
  assert.match(h.root.innerHTML, /data-op="buildAccessGet" data-build-zone="7"/);
  assert.match(h.root.innerHTML, /coBuildAccessTarget-7/);
}

{
  const h = harness();
  h.setState(owned);
  h.click({ op: "mine-tab", tab: "build" });
  h.click({ op: "buildAccessRevoke", buildZone: "7", targetProfileId: "p2" });
  assert.deepEqual(plain(h.sent), [{ op: "buildAccessRevoke", buildZoneId: 7, targetProfileId: "p2" }]);
}

{
  const h = harness();
  h.setState(owned);
  h.click({ op: "mine-tab", tab: "build" });
  h.click({ op: "buildAccessGet", buildZone: "7" });
  assert.deepEqual(plain(h.sent), [{ op: "buildAccessGet", buildZoneId: 7 }]);
}

{
  const h = harness();
  h.setState({ ...promoteState, buildAccess: ownedEmpty.buildAccess });
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /data-op="buildAccessGrant" data-build-zone="7" data-target-profile-id="p2"/);
  h.click({ op: "buildAccessGrant", buildZone: "7", targetProfileId: "p2" });
  assert.deepEqual(plain(h.sent), [{ op: "buildAccessGrant", buildZoneId: 7, targetProfileId: "p2" }]);
}

{
  const h = harness();
  h.setState(ownedEmpty);
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /Nobody approved yet/);
  assert.match(h.root.innerHTML, /No online players to approve/);
  h.input("coBuildAccessTarget-7", VALID_ID);
  h.click({ op: "buildAccessApprove", buildZone: "7" });
  assert.deepEqual(plain(h.sent), [{ op: "buildAccessGrant", buildZoneId: 7, targetProfileId: VALID_ID }]);
  assert.doesNotMatch(h.root.innerHTML, /Enter a player profile id/);
  assert.match(h.root.innerHTML, /value=""/, "the field clears after a sent grant");
}

{
  const h = harness();
  h.setState(ownedEmpty);
  h.click({ op: "mine-tab", tab: "build" });
  h.input("coBuildAccessTarget-7", 'bad"quote');
  h.click({ op: "buildAccessApprove", buildZone: "7" });
  assert.deepEqual(plain(h.sent), []);
  assert.match(h.root.innerHTML, /Enter a player profile id/);
}

{
  const h = harness();
  h.setState(ownedEmpty);
  h.click({ op: "mine-tab", tab: "build" });
  h.input("coBuildAccessTarget-7", "p9");
  h.click({ op: "buildAccessApprove", buildZone: "7" });
  assert.deepEqual(plain(h.sent), [], "the client enforces the server's 8 character profile id minimum");
  assert.match(h.root.innerHTML, /Enter a player profile id/);
}

{
  const h = harness();
  h.setState(ownedEmpty);
  h.click({ op: "mine-tab", tab: "build" });
  h.input("coBuildAccessTarget-7", "co bad id");
  h.click({ op: "buildAccessApprove", buildZone: "7" });
  assert.deepEqual(plain(h.sent), [], "spaces are not valid profile id characters");
}

{
  const h = harness();
  h.setState(ownedEmpty);
  h.click({ op: "mine-tab", tab: "build" });
  h.input("coBuildAccessTarget-7", VALID_ID);
  h.document.activeElement = { id: "coBuildAccessTarget-7" };
  assert.equal(h.keydown("Enter"), true);
  assert.deepEqual(plain(h.sent), [{ op: "buildAccessGrant", buildZoneId: 7, targetProfileId: VALID_ID }]);
}

{
  const h = harness();
  const two = [
    { buildZoneId: 7, ownerProfileId: "p1", allowedProfileIds: [] },
    { buildZoneId: 12, ownerProfileId: "p1", allowedProfileIds: [] }
  ];
  h.setState(withAccess(leaderState, two));
  h.click({ op: "mine-tab", tab: "build" });
  h.input("coBuildAccessTarget-7", VALID_ID);
  h.click({ op: "buildAccessApprove", buildZone: "12" });
  assert.deepEqual(plain(h.sent), [], "a value typed for one settlement is not approved for another");
  h.input("coBuildAccessTarget-12", OTHER_ID);
  h.click({ op: "buildAccessApprove", buildZone: "12" });
  assert.deepEqual(plain(h.sent), [{ op: "buildAccessGrant", buildZoneId: 12, targetProfileId: OTHER_ID }]);
}

{
  const h = harness();
  h.setState(foreign);
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /Owner: p9/);
  assert.match(h.root.innerHTML, /You cannot build here/);
  assert.doesNotMatch(h.root.innerHTML, /data-op="buildAccessRevoke"/);
  assert.doesNotMatch(h.root.innerHTML, /data-op="buildAccessGrant"/);
  assert.doesNotMatch(h.root.innerHTML, /coBuildAccessTarget-/);
}

{
  const h = harness();
  h.setState(guest);
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /You can build here/);
  assert.doesNotMatch(h.root.innerHTML, /data-op="buildAccessRevoke"/);
}

{
  const h = harness();
  h.setState(openZone);
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /Owner: none/);
  assert.match(h.root.innerHTML, /Anyone can build here/);
}

{
  const h = harness();
  h.setState(withAccess(notInFaction, foreign.buildAccess.settlements));
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /BUILD ACCESS/);
  assert.match(h.root.innerHTML, /Owner: p9/);
  assert.doesNotMatch(h.root.innerHTML, /data-op="buildAccessRevoke"/);
}

{
  const h = harness();
  h.setState(withAccess(notInFaction, owned.buildAccess.settlements, null, { selfProfileId: "p1" }));
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /Owner: you/, "the plugin-reported profile id identifies an owner outside a faction");
  assert.match(h.root.innerHTML, /data-op="buildAccessApprove"/);
  assert.match(h.root.innerHTML, /data-op="buildAccessRevoke"/);
}

{
  const h = harness();
  h.setState(withAccess(notInFaction, foreign.buildAccess.settlements, null, { selfProfileId: "p1" }));
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /You cannot build here/, "a foreign settlement stays read-only for the real profile id");
  assert.doesNotMatch(h.root.innerHTML, /data-op="buildAccessApprove"/);
}

{
  const h = harness();
  h.setState(rejected);
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /Too many build access requests/);
}

{
  const h = harness();
  h.setState(denied);
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /Only the settlement owner can do that/);
}

{
  for (const [status, text] of [
    ["underAttack", /This settlement is under attack. Try again once it is over/],
    ["insufficientMaterials", /do not have enough building materials/],
    ["tooFar", /You are too far from the workbench. Walk up to it and try again/],
    ["chargeFailed", /could not be paid for/]
  ]) {
    const h = harness();
    h.setState(withAccess(owned, owned.buildAccess.settlements, { buildZoneId: 7, status }));
    h.click({ op: "mine-tab", tab: "build" });
    assert.match(h.root.innerHTML, text, `a ${status} refusal shows its reason`);
  }
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /Build access data is not available yet/);
  assert.doesNotMatch(h.root.innerHTML, /data-op="buildAccessGet"/);
}

{
  const h = harness();
  h.setState(withAccess(leaderState, []));
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /No settlements you own yet/);
}

{
  const h = harness();
  const truncated = withAccess(leaderState, owned.buildAccess.settlements);
  truncated.buildAccess.truncated = true;
  h.setState(truncated);
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /Some settlements are hidden/);
}

{
  const h = harness();
  h.setState(withAccess(leaderState, [{ buildZoneId: 7, ownerProfileId: "p1", allowedProfileIds: ["p9"] }]));
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, />p9</);
}

{
  const h = harness();
  const access9 = { settlements: [{ buildZoneId: 7, ownerProfileId: "p1", allowedProfileIds: ["p9"] }], lastReject: null, truncated: false };
  h.setState({ ...leaderState, faction: { ...leaderState.faction, members: [
    { profileId: "p1", playerId: 3, name: "Nate", online: true, leader: true },
    { profileId: "p9", playerId: 5, name: "Piper", online: true, leader: false }
  ] }, buildAccess: access9 });
  assert.match(h.root.innerHTML, />Piper</);
  h.setState({ ...leaderState, buildAccess: access9 });
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, />Piper</, "a name seen earlier this session outlives the roster");
  assert.doesNotMatch(h.root.innerHTML, />p9</);
  h.setState({ enabled: false });
  h.click({ op: "mine-tab", tab: "build" });
  h.setState({ ...leaderState, buildAccess: access9 });
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, />p9</, "a disconnect drops the remembered names, so a new server cannot inherit them");
  assert.doesNotMatch(h.root.innerHTML, />Piper</);
}

{
  const h = harness();
  const injection = '<img src=x onerror=alert(1)>';
  h.setState({
    ...leaderState,
    faction: { ...leaderState.faction, members: [
      { profileId: "p1", playerId: 3, name: "Nate", online: true, leader: true },
      { profileId: "p2", playerId: 7, name: injection, online: true, leader: false }
    ] },
    buildAccess: { settlements: [{ buildZoneId: 7, ownerProfileId: "p1", allowedProfileIds: ["p2"] }], lastReject: null, truncated: false }
  });
  assert.doesNotMatch(h.root.innerHTML, /<img src=x/, "player names are escaped, never injected as markup");
  assert.match(h.root.innerHTML, /&lt;img src=x onerror=alert\(1\)&gt;/);
}

{
  const h = harness();
  const longName = "N".repeat(200);
  h.setState({
    ...leaderState,
    faction: { ...leaderState.faction, members: [
      { profileId: "p1", playerId: 3, name: "Nate", online: true, leader: true },
      { profileId: "p2", playerId: 7, name: longName, online: true, leader: false }
    ] },
    buildAccess: { settlements: [{ buildZoneId: 7, ownerProfileId: "p1", allowedProfileIds: ["p2"] }], lastReject: null, truncated: false }
  });
  assert.match(h.root.innerHTML, new RegExp("N{200}"), "a very long name still renders as plain text");
}

function withRaid(base, zones, windowed = true) {
  return { ...base, settlementRaid: { zones, truncated: false, windowedRaids: windowed } };
}

const settled = withRaid(owned, [{
  buildZoneId: 7,
  window: { windowStartHour: 20, windowDurationHours: 2, timezoneOffsetHours: -5 },
  raid: null,
  settlers: [
    { settlerId: 4, assignment: "guard", health: 88.4, dead: false },
    { settlerId: 5, assignment: "idle", health: 0, dead: true }
  ]
}]);

const raidedForeign = withRaid(foreign, [{ buildZoneId: 12, window: null, raid: { declarerProfileId: "p1" }, settlers: [] }]);

{
  const h = harness();
  h.setState(settled);
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /data-op="settlementWindowSet" data-build-zone="7" data-window-start="20" data-window-hours="2"/);
  assert.match(h.root.innerHTML, /data-op="settlerRecruit" data-build-zone="7"/);
  assert.match(h.root.innerHTML, /data-op="settlerAssign" data-build-zone="7" data-settler-id="4" data-assignment="guard"/);
  assert.match(h.root.innerHTML, /data-op="settlerRecover" data-build-zone="7" data-settler-id="5"/);
  assert.doesNotMatch(h.root.innerHTML, /data-op="settlerRecover" data-build-zone="7" data-settler-id="4"/);
  assert.doesNotMatch(h.root.innerHTML, /data-op="raidDeclare"/, "an owner cannot declare a raid on their own settlement");
}

{
  const h = harness();
  h.setState(settled);
  h.click({ op: "mine-tab", tab: "build" });
  h.click({ op: "settlementWindowSet", buildZone: "7", windowStart: "20", windowHours: "2" });
  h.change("coBuildWindowStart-7", "21");
  h.setState(settled);
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /<option value="21" selected>21<\/option>/, "a changed window draft survives a redraw");
  h.click({ op: "settlementWindowSet", buildZone: "7", windowStart: "20", windowHours: "2" });
  assert.deepEqual(plain(h.sent), [
    { op: "settlementWindowSet", buildZoneId: 7, windowStartHour: 20, windowDurationHours: 2 },
    { op: "settlementWindowSet", buildZoneId: 7, windowStartHour: 21, windowDurationHours: 2 }
  ]);
}

{
  const h = harness();
  h.setState(settled);
  h.click({ op: "mine-tab", tab: "build" });
  h.change("coBuildSettlerTask-4", "purifier");
  h.click({ op: "settlerAssign", buildZone: "7", settlerId: "4", assignment: "guard" });
  h.click({ op: "settlerRecover", buildZone: "7", settlerId: "5" });
  h.click({ op: "settlerRecruit", buildZone: "7" });
  assert.deepEqual(plain(h.sent), [
    { op: "settlerAssign", buildZoneId: 7, settlerId: 4, assignment: "purifier" },
    { op: "settlerRecover", buildZoneId: 7, settlerId: 5 },
    { op: "settlerRecruit", buildZoneId: 7 }
  ]);
}

{
  const h = harness();
  h.setState(withRaid(foreign, []));
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /data-op="raidDeclare" data-build-zone="12"/);
  h.click({ op: "raidDeclare", buildZone: "12" });
  assert.deepEqual(plain(h.sent), [{ op: "raidDeclare", buildZoneId: 12 }]);
}

{
  const h = harness();
  h.setState(raidedForeign);
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /Raid declared by you/);
  assert.doesNotMatch(h.root.innerHTML, /data-op="raidDeclare"/, "a live declaration is not declared twice");
}

{
  const h = harness();
  h.setState(withRaid(guest, []));
  h.click({ op: "mine-tab", tab: "build" });
  assert.doesNotMatch(h.root.innerHTML, /data-op="raidDeclare"/, "an approved builder cannot declare a raid");
}

{
  const h = harness();
  h.setState(withRaid(openZone, []));
  h.click({ op: "mine-tab", tab: "build" });
  assert.doesNotMatch(h.root.innerHTML, /data-op="raidDeclare"/, "an unowned settlement has no owner to raid");
}

{
  const h = harness();
  h.setState(withRaid(owned, [{ buildZoneId: 7, window: { windowStartHour: 20, windowDurationHours: 2, timezoneOffsetHours: -5 }, raid: { declarerProfileId: "p2" }, settlers: [] }]));
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /Raid declared by Codsworth/);
  assert.doesNotMatch(h.root.innerHTML, /data-op="settlementWindowSet"/, "a live raid locks the window");
  assert.match(h.root.innerHTML, /data-op="settlerRecruit" data-build-zone="7"/);
}

{
  const h = harness();
  h.setState(withRaid(owned, [{ buildZoneId: 7, window: null, raid: { declarerProfileId: "p2", pending: true, secondsLeft: 754 }, settlers: [] }]));
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /Raid starts in 12:34\./, "a pending raid shows its start countdown");
  h.setState(withRaid(owned, [{ buildZoneId: 7, window: null, raid: { declarerProfileId: "p2", pending: false, secondsLeft: 65 }, settlers: [] }]));
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /Raid live, ends in 1:05\./, "a live raid shows its end countdown");
}

{
  const h = harness();
  h.setState({ ...withRaid(owned, []), settlementRaid: { zones: [], truncated: true } });
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /Some settlement raid details are hidden/);
}

{
  const h = harness();
  const refused = withRaid(owned, settled.settlementRaid.zones);
  refused.settlementRaid.refusal = { text: "You cannot raid a settlement you help run." };
  h.setState(refused);
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /You cannot raid a settlement you help run\./, "the plugin refusal text shows in the build tab");
  h.setState(withRaid(owned, settled.settlementRaid.zones));
  h.click({ op: "mine-tab", tab: "build" });
  assert.doesNotMatch(h.root.innerHTML, /You cannot raid a settlement/, "a cleared refusal is not shown");
}

{
  const h = harness();
  h.setState(withRaid(owned, settled.settlementRaid.zones, false));
  h.click({ op: "mine-tab", tab: "build" });
  assert.doesNotMatch(h.root.innerHTML, /data-op="settlementWindowSet"/, "an open server has no window controls");
  assert.doesNotMatch(h.root.innerHTML, /data-op="raidDeclare"/);
  assert.match(h.root.innerHTML, /Raids are open on this server/);
  assert.match(h.root.innerHTML, /data-op="settlerRecruit" data-build-zone="7"/, "settler controls stay on an open server");
  assert.match(h.root.innerHTML, /data-op="settlerAssign" data-build-zone="7" data-settler-id="4" data-assignment="guard"/);
}

{
  const h = harness();
  h.setState(withRaid(foreign, [], false));
  h.click({ op: "mine-tab", tab: "build" });
  assert.doesNotMatch(h.root.innerHTML, /data-op="raidDeclare"/, "an open server offers no raid declaration");
  assert.match(h.root.innerHTML, /Raids are open on this server/);
}

{
  const h = harness();
  h.setState(withRaid(foreign, [], true));
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /data-op="raidDeclare" data-build-zone="12"/);
  assert.doesNotMatch(h.root.innerHTML, /Raids are open on this server/, "a windowed server shows no open-raids note");
}

{
  const h = harness();
  h.setState({ ...foreign, settlementRaid: { zones: [], truncated: false } });
  h.click({ op: "mine-tab", tab: "build" });
  assert.doesNotMatch(h.root.innerHTML, /data-op="raidDeclare"/, "a state without the windowed flag offers no raid declaration");
  assert.match(h.root.innerHTML, /Raids are open on this server/);
}

function windowZone(startHour, durationHours) {
  return { buildZoneId: 7, window: { windowStartHour: startHour, windowDurationHours: durationHours, timezoneOffsetHours: -5 }, raid: null, settlers: [] };
}

{
  const h = harness();
  h.setState(settled);
  h.click({ op: "mine-tab", tab: "build" });
  h.change("coBuildWindowStart-7", "21");
  h.click({ op: "settlementWindowSet", buildZone: "7", windowStart: "20", windowHours: "2" });
  h.setState(withRaid(owned, [windowZone(22, 2)]));
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /<option value="22" selected>22<\/option>/, "the server window replaces a draft that was already sent");
  h.click({ op: "settlementWindowSet", buildZone: "7", windowStart: "22", windowHours: "2" });
  assert.deepEqual(plain(h.sent), [
    { op: "settlementWindowSet", buildZoneId: 7, windowStartHour: 21, windowDurationHours: 2 },
    { op: "settlementWindowSet", buildZoneId: 7, windowStartHour: 22, windowDurationHours: 2 }
  ], "a sent draft is not sent again after the server value changes");
}

{
  const h = harness();
  h.setState(settled);
  h.click({ op: "mine-tab", tab: "build" });
  h.change("coBuildWindowStart-7", "21");
  h.setState(withRaid(owned, [windowZone(22, 2)]));
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /<option value="22" selected>22<\/option>/, "a server window change replaces an unsent draft");
  h.click({ op: "settlementWindowSet", buildZone: "7", windowStart: "22", windowHours: "2" });
  assert.deepEqual(plain(h.sent), [{ op: "settlementWindowSet", buildZoneId: 7, windowStartHour: 22, windowDurationHours: 2 }]);
}

{
  const h = harness();
  h.setState(settled);
  h.click({ op: "mine-tab", tab: "build" });
  h.change("coBuildSettlerTask-4", "purifier");
  h.click({ op: "settlerAssign", buildZone: "7", settlerId: "4", assignment: "guard" });
  h.setState(withRaid(owned, [{ ...windowZone(20, 2), settlers: [{ settlerId: 4, assignment: "farm", health: 88.4, dead: false }] }]));
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /<option value="farm" selected>farm<\/option>/, "a sent assignment draft gives way to the server assignment");
  h.change("coBuildSettlerTask-4", "scavenge");
  h.setState(withRaid(owned, [{ ...windowZone(20, 2), settlers: [{ settlerId: 4, assignment: "guard", health: 88.4, dead: false }] }]));
  h.click({ op: "mine-tab", tab: "build" });
  h.click({ op: "settlerAssign", buildZone: "7", settlerId: "4", assignment: "guard" });
  assert.deepEqual(plain(h.sent), [
    { op: "settlerAssign", buildZoneId: 7, settlerId: 4, assignment: "purifier" },
    { op: "settlerAssign", buildZoneId: 7, settlerId: 4, assignment: "guard" }
  ]);
}

{
  const h = harness();
  h.setState(withRaid(owned, [windowZone(2.5, 1.5)]));
  h.click({ op: "mine-tab", tab: "build" });
  assert.match(h.root.innerHTML, /<option value="2.5" selected>2.5<\/option>/, "a server window between whole hours is shown as itself");
  assert.match(h.root.innerHTML, /<option value="1.5" selected>1.5<\/option>/);
  assert.match(h.root.innerHTML, /data-window-start="2.5" data-window-hours="1.5"/);
  h.click({ op: "settlementWindowSet", buildZone: "7", windowStart: "2.5", windowHours: "1.5" });
  assert.deepEqual(plain(h.sent), [{ op: "settlementWindowSet", buildZoneId: 7, windowStartHour: 2.5, windowDurationHours: 1.5 }], "the sent window matches what the select shows");
}

console.log("faction.build.test: all checks passed.");
