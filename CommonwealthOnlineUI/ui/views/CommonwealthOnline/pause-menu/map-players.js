(function () {
  var REL = Object.freeze(["self", "party", "faction", "friend", "other"]);
  var REL_PRECEDENCE = ["self", "party", "faction", "friend"];
  var LEGEND_RELS = ["party", "faction", "friend", "other"];
  var MAX_ROSTER = 100;
  var MAX_NAME = 32;
  var DEFAULT_WORLDSPACE = "commonwealth";

  function isFiniteNumber(value) {
    return typeof value === "number" && isFinite(value);
  }

  function clampPct(value) {
    return Math.min(100, Math.max(0, value));
  }

  function resolveRel(raw) {
    var tags = Array.isArray(raw.rel) ? raw.rel : [raw.rel];
    if (raw.faction === true) {
      tags = tags.concat("faction");
    }
    for (var i = 0; i < REL_PRECEDENCE.length; i++) {
      if (tags.indexOf(REL_PRECEDENCE[i]) !== -1) {
        return REL_PRECEDENCE[i];
      }
    }
    return "other";
  }

  function isDowned(raw) {
    var status = raw.status;
    if (raw.downed === true) {
      return true;
    }
    if (Array.isArray(status)) {
      return status.indexOf("downed") !== -1;
    }
    return status !== null && typeof status === "object" && !!status.downed;
  }

  function asText(value) {
    if (typeof value === "string") {
      return value;
    }
    return isFiniteNumber(value) ? String(value) : "";
  }

  function cleanWorldspace(value) {
    return asText(value).trim().toLowerCase();
  }

  function matchesWorldspace(player, worldspace) {
    var ws = cleanWorldspace(worldspace);
    return !ws || player.worldspace === ws;
  }

  function normalizePlayer(raw, index) {
    if (raw === null || typeof raw !== "object") {
      return null;
    }
    if (!isFiniteNumber(raw.xPct) || !isFiniteNumber(raw.yPct)) {
      return null;
    }
    var id = asText(raw.id).replace(/[^\w-]/g, "_");
    return {
      id: id || "p" + (index | 0),
      name: asText(raw.name).trim().slice(0, MAX_NAME).trim(),
      xPct: clampPct(raw.xPct),
      yPct: clampPct(raw.yPct),
      rotDeg: isFiniteNumber(raw.rotDeg) ? raw.rotDeg : 0,
      worldspace: cleanWorldspace(raw.worldspace) || DEFAULT_WORLDSPACE,
      rel: resolveRel(raw),
      factionId: isFiniteNumber(raw.factionId) ? raw.factionId : 0,
      factionName: typeof raw.factionName === "string" ? raw.factionName : "",
      dim: raw.dim === true,
      downed: isDowned(raw),
      canTravel: raw.canTravel === true,
      travelCost: isFiniteNumber(raw.travelCost) ? raw.travelCost : 0
    };
  }

  function normalizeRoster(list) {
    var roster = [];
    var taken = Object.create(null);
    if (!Array.isArray(list)) {
      return roster;
    }
    for (var i = 0; i < list.length && roster.length < MAX_ROSTER; i++) {
      var player = normalizePlayer(list[i], i);
      if (!player) {
        continue;
      }
      var id = player.id;
      for (var n = 2; taken[id]; n++) {
        id = player.id + "-" + n;
      }
      taken[id] = true;
      player.id = id;
      roster.push(player);
    }
    return roster;
  }

  function defaultFilters() {
    return { players: true, names: true, friends: true, faction: true, party: true, others: true, hostiles: true };
  }

  function isOn(filters, key) {
    return !filters || filters[key] !== false;
  }

  function pipSizeMultiplier(size) {
    return size === "Small" ? 0.75 : size === "Large" ? 1.5 : 1;
  }

  function applyFilters(players, filters, worldspace, prefs) {
    var list = Array.isArray(players) ? players : [];
    var visible = [];
    if (!isOn(filters, "players") || !isOn(prefs, "showPlayerMarkers")) {
      return visible;
    }
    for (var i = 0; i < list.length; i++) {
      var player = list[i];
      if (!matchesWorldspace(player, worldspace)) {
        continue;
      }
      if (player.rel === "friend" && (!isOn(filters, "friends") || !isOn(prefs, "showFriendMarkers"))) {
        continue;
      }
      if (player.rel === "faction" && !isOn(filters, "faction")) {
        continue;
      }
      if (player.rel === "party" && !isOn(filters, "party")) {
        continue;
      }
      if (player.rel === "other" && !isOn(filters, "others")) {
        continue;
      }
      visible.push(player);
    }
    return visible;
  }

  function legendCounts(players, worldspace) {
    var list = Array.isArray(players) ? players : [];
    var counts = { party: 0, faction: 0, friend: 0, other: 0, downed: 0, total: 0 };
    for (var i = 0; i < list.length; i++) {
      var player = list[i];
      if (!matchesWorldspace(player, worldspace)) {
        continue;
      }
      counts.total++;
      if (player.downed) {
        counts.downed++;
      }
      if (LEGEND_RELS.indexOf(player.rel) !== -1) {
        counts[player.rel]++;
      }
    }
    return counts;
  }

  function worldspaceCounts(players) {
    var list = Array.isArray(players) ? players : [];
    var counts = Object.create(null);
    for (var i = 0; i < list.length; i++) {
      counts[list[i].worldspace] = (counts[list[i].worldspace] || 0) + 1;
    }
    return counts;
  }

  function partyMembers(players) {
    return membersWithRel(players, "party");
  }

  function factionMembers(players) {
    return membersWithRel(players, "faction");
  }

  function membersWithRel(players, rel) {
    var list = Array.isArray(players) ? players : [];
    var members = [];
    for (var i = 0; i < list.length; i++) {
      if (list[i].rel === rel) {
        members.push(list[i]);
      }
    }
    return members.sort(function (a, b) {
      var x = a.name.toLowerCase();
      var y = b.name.toLowerCase();
      return x < y ? -1 : x > y ? 1 : 0;
    });
  }

  function shouldShowName(player) {
    return !!player.name;
  }

  function factionHue(name) {
    var hash = 0;
    for (var i = 0; i < name.length; i++) {
      hash = (hash * 31 + name.charCodeAt(i)) % 360;
    }
    return hash;
  }

  function markerClass(player) {
    return "marker marker--player-other marker--rel-" + player.rel +
      (player.dim ? " marker--dim" : "") +
      (player.downed ? " marker--downed" : "");
  }

  function mockRoster() {
    return [
      { id: "1", name: "Sole Survivor", xPct: 50, yPct: 50, rotDeg: 15, worldspace: "Commonwealth", rel: "self", factionId: 0, factionName: "", dim: false, status: [] },
      { id: "2", name: "Preston Garvey", xPct: 38.5, yPct: 61.2, rotDeg: 270, worldspace: "commonwealth", rel: "party", factionId: 0, factionName: "", dim: false, status: [] },
      { id: "3", name: "Minuteman Recruit", xPct: 22, yPct: 41, rotDeg: 0, worldspace: "commonwealth", rel: "faction", factionId: 7, factionName: "Minutemen", dim: false, status: [] },
      { id: "4", name: "Steam Friend", xPct: 71, yPct: 24, rotDeg: 45, worldspace: "farharbor", rel: "friend", factionId: 0, factionName: "", dim: false, status: [] },
      { id: "5", name: "Wanderer", xPct: 80, yPct: 80, rotDeg: 180, worldspace: "commonwealth", rel: "other", factionId: 0, factionName: "", dim: true, status: [] },
      { id: "6", name: "Downed Raider", xPct: 15, yPct: 75, rotDeg: 10, worldspace: "farharbor", rel: "other", factionId: 0, factionName: "", dim: false, status: ["downed"] },
      { id: "7", name: "Bad Coordinate", xPct: "nope", yPct: 10, rotDeg: 0, worldspace: "commonwealth", rel: "other", factionId: 0, factionName: "", dim: false, status: [] },
      { id: "2", name: "Duplicate Id", xPct: 40, yPct: 60, rotDeg: 0, worldspace: "commonwealth", rel: "other", factionId: 0, factionName: "", dim: false, status: [] }
    ];
  }

  var api = {
    REL: REL,
    normalizePlayer: normalizePlayer,
    normalizeRoster: normalizeRoster,
    defaultFilters: defaultFilters,
    applyFilters: applyFilters,
    pipSizeMultiplier: pipSizeMultiplier,
    legendCounts: legendCounts,
    worldspaceCounts: worldspaceCounts,
    partyMembers: partyMembers,
    factionMembers: factionMembers,
    shouldShowName: shouldShowName,
    factionHue: factionHue,
    markerClass: markerClass,
    mockRoster: mockRoster
  };

  if (typeof window !== "undefined") {
    window.COMapPlayers = api;
  }
  if (typeof module !== "undefined" && module.exports) {
    module.exports = api;
  }
})();
