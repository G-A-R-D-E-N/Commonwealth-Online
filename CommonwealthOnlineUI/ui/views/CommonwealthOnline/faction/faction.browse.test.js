const { assert, fs, harness, faction, leaderState, memberState, notInFaction, invites, plain } = require("./faction.harness.js");

const factions = [
  { id: "f-1", name: "Minutemen Remnant", tag: "MMR", color: "#3ECF4A", description: "Holding the line.", leader: "Nate", memberCount: 2, onlineCount: 1, yours: true },
  { id: "f-2", name: "Atom Cats", tag: "CAT", color: "", description: "", leader: "Zeke", memberCount: 5, onlineCount: 4, yours: false },
  { id: "f-3", name: "Railroad", tag: "", color: "", description: "", leader: "Desdemona", memberCount: 1, onlineCount: 0, yours: false }
];
const browse = { ...leaderState, factions, factionsLoaded: true };
const officerState = { ...leaderState, localPlayerId: 7, faction: { ...faction, members: [faction.members[0], { ...faction.members[1], officer: true }] } };

function selectedTab(h) {
  const match = h.root.innerHTML.match(/aria-selected="true" data-op="tab" data-tab="(\w+)"/);
  return match ? match[1] : "";
}

function rowsOf(h) {
  return h.root.innerHTML.split('<li class="faction-row">').slice(1);
}

function rowFor(h, name) {
  return rowsOf(h).find((row) => row.indexOf(name) >= 0) || "";
}

function inviteButtons(h) {
  return h.root.innerHTML.match(/data-op="invite" data-target-player-id="\d+"/g) || [];
}

{
  const h = harness();
  h.setState(browse);
  assert.equal(selectedTab(h), "mine", "My faction is the default tab");
  assert.match(h.root.innerHTML, /data-op="kick"/, "My faction renders the members");
  assert.equal((h.root.innerHTML.match(/data-op="tab"/g) || []).length, 4, "four tabs");
  assert.match(h.root.innerHTML, /data-tab="factions">Factions<\/button>/);
  assert.match(h.root.innerHTML, /data-tab="mine">My faction<\/button>/);
  assert.match(h.root.innerHTML, /data-tab="invite">Invite<\/button>/);
  assert.match(h.root.innerHTML, /data-tab="invites">Invites<\/button>/);

  h.click({ op: "tab", tab: "factions" });
  assert.equal(selectedTab(h), "factions");
  assert.deepEqual(plain(h.sent), [{ op: "listFactions" }], "opening Factions asks the server for the list");
  assert.doesNotMatch(h.root.innerHTML, /data-op="kick"/, "only the active tab renders");

  h.click({ op: "tab", tab: "invite" });
  assert.equal(selectedTab(h), "invite");
  assert.deepEqual(plain(h.sent), [{ op: "listFactions" }], "switching to Invite sends nothing");

  h.setState({ ...browse, pendingInvites: invites });
  assert.match(h.root.innerHTML, /data-tab="invites">Invites \(1\)<\/button>/, "the Invites tab shows the pending count");
  h.click({ op: "tab", tab: "invites" });
  assert.equal(selectedTab(h), "invites");
  assert.match(h.root.innerHTML, /data-op="inviteResponse" data-invite-id="i-4" data-accept="true"/);

  h.click({ op: "tab", tab: "mine" });
  assert.match(h.root.innerHTML, /data-op="kick"/, "switching back restores My faction");

  h.click({ op: "tab", tab: "nowhere" });
  assert.equal(selectedTab(h), "mine", "an unknown tab is ignored");

  h.click({ op: "tab", tab: "factions" });
  const before = h.sent.length;
  h.click({ op: "listFactions" });
  assert.deepEqual(plain(h.sent.slice(before)), [{ op: "listFactions" }], "Refresh list asks the server again");
}

{
  const h = harness();
  h.setState(browse);
  h.click({ op: "tab", tab: "factions" });
  assert.equal(rowsOf(h).length, 3);
  assert.notEqual(rowFor(h, "Atom Cats"), "");
  assert.notEqual(rowFor(h, "Railroad"), "");

  h.input("coFactionSearch", "cat");
  assert.equal(rowsOf(h).length, 1, "a name substring filters the list");
  assert.notEqual(rowFor(h, "Atom Cats"), "");
  assert.equal(rowFor(h, "Minutemen Remnant"), "");

  h.input("coFactionSearch", "  mmr ");
  assert.equal(rowsOf(h).length, 1, "a tag matches, ignoring case and surrounding spaces");
  assert.notEqual(rowFor(h, "Minutemen Remnant"), "");

  h.input("coFactionSearch", "ZEKE");
  assert.notEqual(rowFor(h, "Atom Cats"), "", "a leader name matches");
  assert.equal(rowsOf(h).length, 1);

  h.input("coFactionSearch", "zzz");
  assert.equal(rowsOf(h).length, 0);
  assert.match(h.root.innerHTML, /No factions match your search\./);

  h.input("coFactionSearch", "");
  assert.equal(rowsOf(h).length, 3, "an empty search shows every faction");
}

{
  const h = harness();
  h.setState(browse);
  h.click({ op: "tab", tab: "factions" });
  h.input("coFactionSearch", "rail");
  h.click({ op: "tab", tab: "mine" });
  h.click({ op: "tab", tab: "factions" });
  assert.match(h.root.innerHTML, /value="rail"/, "the search survives a tab switch");
  assert.equal(rowsOf(h).length, 1);

  h.input("coFactionSearch", '"><img src=x>');
  assert.doesNotMatch(h.root.innerHTML, /<img/, "a search string is escaped");
  assert.match(h.root.innerHTML, /value="&quot;&gt;&lt;img src=x&gt;"/);
}

{
  const h = harness();
  h.setState(browse);
  h.click({ op: "tab", tab: "factions" });
  const focused = [];
  h.root.onWrite = (target) => {
    target.elements["#coFactionSearch"] = {
      focus() { focused.push("focus"); },
      setSelectionRange(start, end) { focused.push([start, end]); }
    };
  };
  h.document.activeElement = { id: "coFactionSearch", tagName: "INPUT", selectionStart: 3, selectionEnd: 3 };
  h.input("coFactionSearch", "rai");
  assert.deepEqual(focused, ["focus", [3, 3]], "typing keeps focus and the caret in the search box");
}

{
  const h = harness();
  h.setState(browse);
  h.click({ op: "tab", tab: "factions" });
  h.document.activeElement = h.field("coFactionSearch", "ab");
  h.keydown("Enter");
  assert.deepEqual(plain(h.sent), [{ op: "listFactions" }], "Enter in the search box must not submit a faction create");
  assert.doesNotMatch(h.root.innerHTML, /Use 3 to 24 letters/);
}

{
  const h = harness();
  assert.match(h.root.innerHTML, /Loading faction status\.\.\./, "no state yet shows the loading view");
  h.setState({ ...browse, factionsLoaded: false, factions });
  h.click({ op: "tab", tab: "factions" });
  assert.match(h.root.innerHTML, /Loading factions\.\.\./, "an unloaded list shows loading, even with stale rows");
  assert.equal(rowsOf(h).length, 0, "no rows while loading");

  h.setState({ ...browse, factionsLoaded: true, factions: [] });
  assert.match(h.root.innerHTML, /No factions have been founded on this server yet\./);
  assert.doesNotMatch(h.root.innerHTML, /Loading factions/);

  h.setState({ ...leaderState, factionsLoaded: true });
  assert.match(h.root.innerHTML, /No factions have been founded on this server yet\./, "a missing list counts as empty");
}

{
  const h = harness();
  h.setState(browse);
  h.click({ op: "tab", tab: "factions" });
  assert.match(rowFor(h, "Minutemen Remnant"), /<span class="faction-yours">YOURS<\/span>/, "the viewer's own faction carries the YOURS badge");
  assert.doesNotMatch(rowFor(h, "Atom Cats"), /faction-yours/, "other factions have no YOURS badge");
  assert.match(rowFor(h, "Atom Cats"), /Leader: Zeke · 5 members · 4 online/);
  assert.match(rowFor(h, "Railroad"), /1 member · 0 online/, "a single member is singular");
  assert.match(rowFor(h, "Minutemen Remnant"), /<span class="faction-desc">Holding the line\.<\/span>/);
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "tab", tab: "invite" });
  assert.deepEqual(inviteButtons(h), ['data-op="invite" data-target-player-id="5"'], "the leader gets an Invite button only for players outside the faction");
  assert.doesNotMatch(h.root.innerHTML, /data-target-player-id="3"/, "the leader is not offered an invite");
  assert.doesNotMatch(h.root.innerHTML, /data-target-player-id="7"/, "a member is not offered an invite");
  h.click({ op: "invite", targetPlayerId: "5" });
  assert.deepEqual(plain(h.sent), [{ op: "invite", targetPlayerId: 5 }]);
}

{
  const h = harness();
  h.setState(officerState);
  h.click({ op: "tab", tab: "invite" });
  assert.deepEqual(inviteButtons(h), ['data-op="invite" data-target-player-id="5"'], "an officer gets the same Invite buttons");
}

{
  const h = harness();
  h.setState(memberState);
  h.click({ op: "tab", tab: "invite" });
  assert.deepEqual(inviteButtons(h), [], "a plain member gets no Invite button");
  assert.match(h.root.innerHTML, /Only the leader and officers can invite players\./);
}

{
  const h = harness();
  h.setState(notInFaction);
  h.click({ op: "tab", tab: "invite" });
  assert.deepEqual(inviteButtons(h), [], "a player outside a faction gets no Invite button");
  assert.match(h.root.innerHTML, /Create or join a faction to invite players\./);
}

{
  const h = harness();
  h.setState({ ...leaderState, roster: [] });
  h.click({ op: "tab", tab: "invite" });
  assert.match(h.root.innerHTML, /No players to invite\./, "an empty roster shows the empty state");
  assert.deepEqual(inviteButtons(h), []);
}

{
  const h = harness();
  h.setState(notInFaction);
  h.click({ op: "tab", tab: "invites" });
  assert.match(h.root.innerHTML, /PENDING INVITES/);
  h.setState({ ...notInFaction, pendingInvites: [] });
  assert.match(h.root.innerHTML, /No pending invites\./, "an empty invite list shows the empty state");
}

{
  const h = harness();
  h.setState(browse);
  const tabs = ["factions", "mine", "invite", "invites"].map((tab) => ({
    tagName: "BUTTON",
    dataset: { op: "tab", tab },
    focus() { h.document.activeElement = this; },
    click() { h.click({ op: "tab", tab }); }
  }));
  h.document.querySelectorAll = () => tabs;
  h.document.activeElement = tabs[1];
  h.controller("dpad_up", "down");
  h.controller("a", "down");
  assert.equal(selectedTab(h), "factions", "dpad up and A open the Factions tab");
  h.controller("dpad_down", "down");
  h.controller("dpad_down", "down");
  h.controller("a", "down");
  assert.equal(selectedTab(h), "invite", "dpad down and A reach the Invite tab");
}

{
  const markup = fs.readFileSync(__dirname + "/index.html", "utf8");
  const browseAt = markup.indexOf('<script src="faction.browse.js"></script>');
  assert.ok(browseAt > markup.indexOf('<script src="faction.build.js"></script>') && browseAt < markup.indexOf('<script src="faction.js"></script>'),
    "faction.browse.js loads after the build section and before faction.js");
  assert.match(fs.readFileSync(__dirname + "/styles.css", "utf8"), /\.faction-subtab\[aria-selected="true"\],/, "the sub-tabs need a selected style");
}

console.log("Faction browse regression passed");
