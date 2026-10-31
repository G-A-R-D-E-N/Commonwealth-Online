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

console.log("faction.build.test: all checks passed.");
