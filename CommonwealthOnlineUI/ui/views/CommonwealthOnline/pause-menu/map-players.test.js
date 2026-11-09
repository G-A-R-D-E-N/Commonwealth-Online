const assert = require("node:assert/strict");
const api = require("./map-players.js");

assert.deepEqual(api.REL, ["self", "party", "faction", "friend", "other"]);
assert.ok(Object.isFrozen(api.REL));

assert.deepEqual(api.normalizePlayer({ id: 7, name: "  Preston  ", xPct: 12.5, yPct: 80, rotDeg: 90, worldspace: "FarHarbor", rel: "friend", factionId: 3, factionName: "Minutemen", dim: true, status: [] }, 0), {
  id: "7", name: "Preston", xPct: 12.5, yPct: 80, rotDeg: 90, worldspace: "farharbor", rel: "friend", factionId: 3, factionName: "Minutemen", dim: true, downed: false, canTravel: false, travelCost: 0
});

assert.deepEqual(api.normalizePlayer({ xPct: 1, yPct: 2 }, 4), {
  id: "p4", name: "", xPct: 1, yPct: 2, rotDeg: 0, worldspace: "commonwealth", rel: "other", factionId: 0, factionName: "", dim: false, downed: false, canTravel: false, travelCost: 0
});

assert.equal(api.normalizePlayer({ id: "", xPct: 1, yPct: 1 }, 9).id, "p9");
assert.equal(api.normalizePlayer({ xPct: 1, yPct: 1 }).id, "p0");
assert.equal(api.normalizePlayer({ id: {}, xPct: 1, yPct: 1 }, 3).id, "p3");
assert.equal(api.normalizePlayer({ id: NaN, xPct: 1, yPct: 1 }, 5).id, "p5");
assert.equal(api.normalizePlayer({ id: 42, xPct: 1, yPct: 1 }, 5).id, "42");
assert.equal(api.normalizePlayer({ xPct: 1, yPct: 1, name: "a".repeat(40) }, 0).name.length, 32);
assert.equal(api.normalizePlayer({ xPct: 1, yPct: 1, name: "  " + "b".repeat(40) }, 0).name, "b".repeat(32));
assert.equal(api.normalizePlayer({ xPct: 1, yPct: 1, name: {} }, 0).name, "");
assert.equal(api.normalizePlayer({ xPct: 1, yPct: 1, name: ["Ann"] }, 0).name, "");
assert.equal(api.normalizePlayer({ xPct: 1, yPct: 1, name: 42 }, 0).name, "42");
assert.equal(api.normalizePlayer({ xPct: 1, yPct: 1, name: "a".repeat(31) + "   b" }, 0).name, "a".repeat(31));
assert.equal(api.normalizePlayer({ xPct: 1, yPct: 1, name: "   " + "c".repeat(31) + " d" }, 0).name, "c".repeat(31));

assert.equal(api.normalizePlayer(null, 0), null);
assert.equal(api.normalizePlayer("player", 0), null);
assert.equal(api.normalizePlayer({ xPct: "10", yPct: 5 }, 0), null);
assert.equal(api.normalizePlayer({ xPct: NaN, yPct: 5 }, 0), null);
assert.equal(api.normalizePlayer({ xPct: 5, yPct: Infinity }, 0), null);
assert.equal(api.normalizePlayer({ xPct: 5 }, 0), null);

{
  const clamped = api.normalizePlayer({ xPct: -5, yPct: 140, rotDeg: NaN, factionId: "x" }, 0);
  assert.equal(clamped.xPct, 0);
  assert.equal(clamped.yPct, 100);
  assert.equal(clamped.rotDeg, 0);
  assert.equal(clamped.factionId, 0);
}

function relOf(value) {
  return api.normalizePlayer({ xPct: 0, yPct: 0, rel: value }, 0).rel;
}
assert.equal(relOf("party"), "party");
assert.equal(relOf(["friend", "party"]), "party");
assert.equal(relOf(["other", "faction", "friend"]), "faction");
assert.equal(relOf(["friend", "other"]), "friend");
assert.equal(relOf(["other"]), "other");
assert.equal(relOf(["bogus", "other"]), "other");
assert.equal(relOf("bogus"), "other");
assert.equal(relOf(undefined), "other");
assert.equal(relOf(["self", "party"]), "self");
assert.equal(relOf("self"), "self");

assert.equal(api.normalizePlayer({ xPct: 0, yPct: 0, status: ["downed"] }, 0).downed, true);
assert.equal(api.normalizePlayer({ xPct: 0, yPct: 0, status: ["in-menu"] }, 0).downed, false);
assert.equal(api.normalizePlayer({ xPct: 0, yPct: 0, status: { downed: true } }, 0).downed, true);
assert.equal(api.normalizePlayer({ xPct: 0, yPct: 0, status: { downed: false } }, 0).downed, false);
assert.equal(api.normalizePlayer({ xPct: 0, yPct: 0, downed: true }, 0).downed, true);
assert.equal(api.normalizePlayer({ xPct: 0, yPct: 0 }, 0).downed, false);

assert.deepEqual(api.normalizeRoster(null), []);
assert.deepEqual(api.normalizeRoster("roster"), []);
assert.deepEqual(
  api.normalizeRoster([
    null,
    { xPct: 1, yPct: 1, id: "a" },
    undefined,
    { xPct: "bad", yPct: 1, id: "b" },
    { xPct: 2, yPct: 2, id: "a" },
    { xPct: 3, yPct: 3, id: "a" },
    { xPct: 4, yPct: 4, id: "a-2" }
  ]).map((player) => player.id),
  ["a", "a-2", "a-3", "a-2-2"]
);
assert.deepEqual(
  api.normalizeRoster([{ xPct: 1, yPct: 1 }, { xPct: 1, yPct: 1, id: "p0" }]).map((player) => player.id),
  ["p0", "p0-2"]
);

{
  const many = [];
  for (let i = 0; i < 101; i++) {
    many.push({ id: "n" + i, xPct: 1, yPct: 1 });
  }
  const capped = api.normalizeRoster(many);
  assert.equal(capped.length, 100);
  assert.equal(capped[99].id, "n99");
}

assert.deepEqual(api.defaultFilters(), { players: true, names: true, friends: true, faction: true, party: true, others: true, hostiles: true });

const roster = api.normalizeRoster(api.mockRoster());
assert.deepEqual(roster.map((player) => player.id), ["1", "2", "3", "4", "5", "6", "2-2"]);

const ids = (list) => list.map((player) => player.id);
const all = api.defaultFilters();

assert.deepEqual(ids(api.applyFilters(roster, undefined, "")), ids(roster));
assert.deepEqual(ids(api.applyFilters(roster, all, "")), ids(roster));
assert.deepEqual(ids(api.applyFilters(roster, all, "commonwealth")), ["1", "2", "3", "5", "2-2"]);
assert.deepEqual(ids(api.applyFilters(roster, all, "FARHARBOR")), ["4", "6"]);
assert.deepEqual(ids(api.applyFilters(roster, all, "nukaworld")), []);
assert.deepEqual(ids(api.applyFilters(roster, { players: false, friends: true, faction: true, party: true, others: true }, "")), []);
assert.deepEqual(ids(api.applyFilters(roster, { players: true, friends: false, faction: true, party: true, others: true }, "")), ["1", "2", "3", "5", "6", "2-2"]);
assert.deepEqual(ids(api.applyFilters(roster, { players: true, friends: true, faction: false, party: true, others: true }, "")), ["1", "2", "4", "5", "6", "2-2"]);
assert.deepEqual(ids(api.applyFilters(roster, { players: true, friends: true, faction: true, party: false, others: true }, "")), ["1", "3", "4", "5", "6", "2-2"]);
assert.deepEqual(ids(api.applyFilters(roster, { players: true, friends: true, faction: true, party: true, others: false }, "")), ["1", "2", "3", "4"]);
assert.deepEqual(ids(api.applyFilters(roster, { players: true, friends: false, faction: false, party: false, others: false }, "commonwealth")), ["1"]);
assert.deepEqual(ids(api.applyFilters(roster, { players: false, friends: false, faction: false, party: false, others: false }, "farharbor")), []);
assert.deepEqual(ids(api.applyFilters(null, all, "")), []);
assert.deepEqual(ids(api.applyFilters(roster, { players: true }, "")), ids(roster));
assert.deepEqual(ids(api.applyFilters(roster, {}, "")), ids(roster));
assert.deepEqual(ids(api.applyFilters(roster, { friends: false }, "")), ["1", "2", "3", "5", "6", "2-2"]);
assert.deepEqual(ids(api.applyFilters(roster, { others: false }, "farharbor")), ["4"]);
assert.deepEqual(ids(api.applyFilters(roster, all, "  FarHarbor ")), ["4", "6"]);
assert.deepEqual(ids(api.applyFilters(roster, all, "   ")), ids(roster));
assert.deepEqual(ids(api.applyFilters(roster, all, "", null)), ids(roster));
assert.deepEqual(ids(api.applyFilters(roster, all, "", {})), ids(roster));
assert.deepEqual(ids(api.applyFilters(roster, all, "", { showPlayerMarkers: true, showFriendMarkers: true, mapMarkerSize: "Large" })), ids(roster));
assert.deepEqual(ids(api.applyFilters(roster, all, "", { showPlayerMarkers: false })), []);
assert.deepEqual(ids(api.applyFilters(roster, all, "commonwealth", { showPlayerMarkers: false })), []);
assert.deepEqual(ids(api.applyFilters(roster, { players: false }, "", { showPlayerMarkers: true })), []);
assert.deepEqual(ids(api.applyFilters(roster, all, "", { showFriendMarkers: false })), ["1", "2", "3", "5", "6", "2-2"]);
assert.deepEqual(ids(api.applyFilters(roster, all, "farharbor", { showFriendMarkers: false })), ["6"]);
assert.deepEqual(ids(api.applyFilters(roster, { others: false }, "", { showPlayerMarkers: true, showFriendMarkers: true })), ["1", "2", "3", "4"]);
assert.deepEqual(ids(api.applyFilters(roster, { friends: false }, "", { showFriendMarkers: true })), ["1", "2", "3", "5", "6", "2-2"]);
assert.deepEqual(ids(api.applyFilters(roster, { friends: true }, "", { showFriendMarkers: false })), ["1", "2", "3", "5", "6", "2-2"]);
assert.deepEqual(ids(api.applyFilters(roster, { party: false, others: false }, "", { showFriendMarkers: false })), ["1", "3"]);
assert.equal(api.pipSizeMultiplier("Small"), 0.75);
assert.equal(api.pipSizeMultiplier("Medium"), 1);
assert.equal(api.pipSizeMultiplier("Large"), 1.5);
assert.equal(api.pipSizeMultiplier("Huge"), 1);
assert.equal(api.pipSizeMultiplier(undefined), 1);
assert.equal(api.legendCounts(roster, " farharbor ").total, 2);

assert.deepEqual(api.legendCounts(roster, "commonwealth"), { party: 1, faction: 1, friend: 0, other: 2, downed: 0, total: 5 });
assert.deepEqual(api.legendCounts(roster, "farharbor"), { party: 0, faction: 0, friend: 1, other: 1, downed: 1, total: 2 });
assert.deepEqual(api.legendCounts(roster, ""), { party: 1, faction: 1, friend: 1, other: 3, downed: 1, total: 7 });
assert.deepEqual(api.legendCounts(null, ""), { party: 0, faction: 0, friend: 0, other: 0, downed: 0, total: 0 });

assert.deepEqual(api.partyMembers(roster).map((player) => player.id), ["2"]);
assert.deepEqual(api.partyMembers(null), []);
assert.deepEqual(api.partyMembers(undefined), []);
assert.deepEqual(api.partyMembers([]), []);
assert.deepEqual(api.factionMembers(roster).map((player) => player.id), ["3"]);
assert.deepEqual(api.factionMembers(null), []);
{
  const party = api.partyMembers(api.normalizeRoster([
    { id: "s", name: "Me", xPct: 1, yPct: 1, rel: "self" },
    { id: "c", name: "charlie", xPct: 1, yPct: 1, rel: "party", worldspace: "nukaworld" },
    { id: "o", name: "Other", xPct: 1, yPct: 1, rel: "other" },
    { id: "f", name: "Friend", xPct: 1, yPct: 1, rel: "friend" },
    { id: "b", name: "Bravo", xPct: 1, yPct: 1, rel: "party", worldspace: "farharbor", status: ["downed"] },
    { id: "a", name: "alpha", xPct: 1, yPct: 1, rel: "party" }
  ]));
  assert.deepEqual(party.map((player) => player.name), ["alpha", "Bravo", "charlie"]);
  assert.deepEqual(party.map((player) => player.worldspace), ["commonwealth", "farharbor", "nukaworld"]);
  assert.equal(party[1].downed, true);
}

assert.deepEqual({ ...api.worldspaceCounts(roster) }, { commonwealth: 5, farharbor: 2 });
assert.equal(Object.getPrototypeOf(api.worldspaceCounts(roster)), null);
assert.deepEqual({ ...api.worldspaceCounts([]) }, {});
{
  const counts = api.worldspaceCounts([
    api.normalizePlayer({ xPct: 0, yPct: 0, worldspace: "constructor" }, 0),
    api.normalizePlayer({ xPct: 0, yPct: 0, worldspace: "constructor" }, 1),
    api.normalizePlayer({ xPct: 0, yPct: 0, worldspace: "__proto__" }, 2)
  ]);
  assert.equal(counts.constructor, 2);
  assert.equal(counts["__proto__"], 1);
  assert.deepEqual(Object.keys(counts).sort(), ["__proto__", "constructor"]);
}

assert.equal(api.shouldShowName({ rel: "party", name: "Preston" }), true);
assert.equal(api.shouldShowName({ rel: "faction", name: "Recruit" }), true);
assert.equal(api.shouldShowName({ rel: "friend", name: "Friend" }), true);
assert.equal(api.shouldShowName({ rel: "other", name: "Wanderer" }), true);
assert.equal(api.shouldShowName({ rel: "other", name: "" }), false);

assert.equal(
  api.markerClass({ rel: "other", dim: false, downed: false }),
  "marker marker--player-other marker--rel-other"
);
assert.equal(
  api.markerClass({ rel: "friend", dim: true, downed: false }),
  "marker marker--player-other marker--rel-friend marker--dim"
);
assert.equal(
  api.markerClass({ rel: "other", dim: true, downed: true }),
  "marker marker--player-other marker--rel-other marker--dim marker--downed"
);

{
  const raw = api.mockRoster();
  assert.equal(raw.length, 8);
  assert.deepEqual(api.mockRoster(), raw);
  assert.equal(new Set(raw.map((entry) => entry.id)).size, 7);
  assert.equal(raw.filter((entry, index) => api.normalizePlayer(entry, index) === null).length, 1);
  assert.deepEqual(
    Array.from(new Set(roster.map((player) => player.rel))).sort(),
    ["friend", "other", "party", "faction", "self"].sort()
  );
  assert.equal(roster.filter((player) => player.dim).length, 1);
  assert.equal(roster.filter((player) => player.downed).length, 1);
  assert.deepEqual(Array.from(new Set(roster.map((player) => player.worldspace))).sort(), ["commonwealth", "farharbor"]);
}

assert.deepEqual(
  api.normalizeRoster([{ id: "a b", xPct: 1, yPct: 1 }, { id: "a_b", xPct: 2, yPct: 2 }, { id: "a\\", xPct: 3, yPct: 3 }]).map((player) => player.id),
  ["a_b", "a_b-2", "a_"]
);

assert.equal(api.normalizePlayer({ id: "9", xPct: 1, yPct: 1, faction: true }, 0).rel, "faction");
assert.equal(api.normalizePlayer({ id: "9", xPct: 1, yPct: 1, faction: true, rel: "party" }, 0).rel, "party");

assert.equal(api.factionHue("TAG"), api.factionHue("TAG"));
assert.ok(api.factionHue("ABCDEFGH") >= 0 && api.factionHue("ABCDEFGH") < 360);
assert.notEqual(api.factionHue("AAA"), api.factionHue("BBB"));
const captureSpots = api.applyFilters(api.normalizeRoster([{ id: "9", xPct: 10, yPct: 20 }, { id: "4" }]), { players: false }, "");
assert.equal(captureSpots.length, 0);
const shownCaptures = api.applyFilters(api.normalizeRoster([{ id: "9", xPct: 10, yPct: 20 }, { id: "4" }]), null, "");
assert.deepEqual(shownCaptures.map((spot) => [spot.id, spot.xPct, spot.yPct]), [["9", 10, 20]]);
assert.deepEqual(api.normalizeRoster(undefined), []);
console.log("map-players tests passed");

const test = require("node:test");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

test("map updates reuse markers and recreate them after removal", () => {
  const source = fs.readFileSync(path.join(__dirname, "script.js"), "utf8");
  const start = source.indexOf("  var markerElements = Object.create(null);");
  const end = source.indexOf("  function syncMarkerNameLabel", start);
  assert.ok(start >= 0 && end > start);
  const nodes = [];
  const mapMarkers = {
    appendChild(node) { nodes.push(node); },
    querySelectorAll() { return nodes.slice(); }
  };
  const document = { createElement() {
    return { dataset: {}, remove() { nodes.splice(nodes.indexOf(this), 1); } };
  } };
  const context = vm.createContext({ mapMarkers, document });
  vm.runInContext(source.slice(start, end), context);
  const first = context.ensureMarkerEl("location", "marker");
  assert.equal(context.ensureMarkerEl("location", "marker"), first);
  context.pruneMarkers({ location: true });
  assert.equal(nodes.length, 1);
  context.pruneMarkers({});
  assert.equal(nodes.length, 0);
  assert.notEqual(context.ensureMarkerEl("location", "marker"), first);
});

test("player prefs gate other pips and size only, never the self marker", () => {
  const source = fs.readFileSync(path.join(__dirname, "script.js"), "utf8");
  assert.ok(source.includes("mapPlayers.applyFilters(mapRoster, playerFilters, activeMapWorldspace, payload.prefs)"));
  assert.ok(source.includes('pauseScreen.style.setProperty("--co-pip-size-mult"'));
  const start = source.indexOf('var player = ensureMarkerEl("player", "marker marker--player");');
  const end = source.indexOf("drawMapFog();", start);
  assert.ok(start > 0 && end > start);
  assert.doesNotMatch(source.slice(start, end), /prefs|pipSize|showPlayer|showFriend/);
});
