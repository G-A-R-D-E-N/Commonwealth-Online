const { assert, fs, vm, harness, leaderState, notInFaction, plain } = require("./faction.harness.js");

const ownershipContext = { window: {} };
vm.runInNewContext(fs.readFileSync(__dirname + "/faction.ownership.js", "utf8"), ownershipContext);
const ownership = ownershipContext.window.coFactionOwnership;

{
  const esc = (value) => String(value);
  const staleHtml = ownership.ownershipSection(withCapture(leaderState, capture([row({ buildZoneId: 7 })], { lastResult: { ok: false, code: "no_attempt" } })), esc);
  assert.doesNotMatch(staleHtml, /Something went wrong/, "a stale noAttempt reply must not print a refusal");
  assert.doesNotMatch(staleHtml, /faction-status is-error/);
}

function row(extra) {
  return { buildZoneId: 7, name: "Zone " + ((extra && extra.buildZoneId) || 7), state: "neutral", ownerProfileId: "", ownerName: "", capturerProfileId: "", capturerName: "", raid: false, progressPercent: 0, secondsLeft: 0, cooldownSeconds: 0, ...extra };
}

function capture(settlements, extra) {
  return { settlements, nearBuildZoneId: 0, canCapture: true, localProgress: 0, localCapturing: false, ...(extra || {}) };
}

function withCapture(base, cap) {
  return { ...base, settlementCapture: cap };
}

function captureButtonTag(html, zone) {
  const match = html.match(new RegExp('<button[^>]*data-op="capture" data-build-zone="' + zone + '"[^>]*>'));
  assert.ok(match, "a Capture button must render for settlement " + zone);
  return match[0];
}

function isDisabled(tag) {
  return /\sdisabled[\s>]/.test(tag);
}

function cancelTag(html) {
  return html.match(/<button[^>]*data-op="cancelCapture"[^>]*>/)[0];
}

{
  const h = harness();
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 7 })], { nearBuildZoneId: 7 })));
  h.click({ op: "mine-tab", tab: "ownership" });
  assert.match(h.root.innerHTML, /<h2>OWNERSHIP<\/h2>/, "the Ownership section renders in the Social window");
  assert.match(h.root.innerHTML, /Zone 7/);
}

{
  const h = harness();
  h.setState(withCapture(notInFaction, capture([row({ buildZoneId: 7 })], { nearBuildZoneId: 7 })));
  assert.match(h.root.innerHTML, /<h2>OWNERSHIP<\/h2>/, "a player outside any faction can still capture");
}

{
  const h = harness();
  h.setState(leaderState);
  h.click({ op: "mine-tab", tab: "ownership" });
  assert.match(h.root.innerHTML, /Ownership data is not available yet\./, "a server without the capture state shows a placeholder");
  assert.doesNotMatch(h.root.innerHTML, /data-op="capture"/);
}

{
  const h = harness();
  h.setState(withCapture(leaderState, capture([{ ...row({ buildZoneId: 7 }), name: "" }], { nearBuildZoneId: 7 })));
  h.click({ op: "mine-tab", tab: "ownership" });
  assert.match(h.root.innerHTML, /Unnamed settlement/, "a server without names never shows an id");
  assert.equal(isDisabled(captureButtonTag(h.root.innerHTML, 7)), true, "an unnamed settlement cannot be captured");
}

{
  const h = harness();
  h.setState(withCapture(leaderState, capture([])));
  h.click({ op: "mine-tab", tab: "ownership" });
  assert.match(h.root.innerHTML, /No settlements discovered yet\./);
}

{
  const cap = capture([
    row({ buildZoneId: 10, state: "owned", ownerProfileId: "p1" }),
    row({ buildZoneId: 20, state: "capturing", capturerProfileId: "p2", progressPercent: 40, secondsLeft: 72 }),
    row({ buildZoneId: 30, state: "neutral" }),
    row({ buildZoneId: 40, state: "contested", progressPercent: 15 }),
    row({ buildZoneId: 50, state: "neutral" }),
    row({ buildZoneId: 45, state: "neutral" }),
    row({ buildZoneId: 0, state: "neutral" }),
    row({ buildZoneId: "7", state: "neutral" }),
    row({ buildZoneId: 1.5, state: "neutral" }),
    row({ buildZoneId: 60, state: "weird" })
  ], { nearBuildZoneId: 30 });
  assert.deepEqual(ownership.sortRows(cap).map((entry) => entry.buildZoneId), [30, 20, 40, 10, 45, 50],
    "near first, then capturing, contested, owned, neutral; ties by id; invalid ids and states dropped");
  const h = harness();
  h.setState(withCapture(leaderState, cap));
  h.click({ op: "mine-tab", tab: "ownership" });
  const html = h.root.innerHTML;
  const order = [30, 20, 40, 10, 45, 50].map((zone) => html.indexOf("Zone " + zone + "<"));
  order.forEach((at) => assert.ok(at >= 0, "each valid settlement renders"));
  assert.deepEqual(order.slice().sort((a, b) => a - b), order, "the rendered order matches the sort");
  assert.doesNotMatch(html, /Zone 0</, "a zero id is never rendered");
  assert.doesNotMatch(html, /Zone 60</, "an unknown state is never rendered");
}

const enableCases = [
  { name: "neutral, at the workbench, eligible", row: row({ buildZoneId: 7, state: "neutral" }), cap: { nearBuildZoneId: 7, canCapture: true, promptCapture: true }, enabled: true },
  { name: "owned, at the workbench, eligible (raid)", row: row({ buildZoneId: 7, state: "owned", ownerProfileId: "p1" }), cap: { nearBuildZoneId: 7, canCapture: true, promptCapture: true }, enabled: true },
  { name: "not near", row: row({ buildZoneId: 7, state: "neutral" }), cap: { nearBuildZoneId: 9, canCapture: true, promptCapture: false }, enabled: false },
  { name: "near, outside the workbench prompt radius", row: row({ buildZoneId: 7, state: "neutral" }), cap: { nearBuildZoneId: 7, canCapture: true, promptCapture: false }, enabled: false },
  { name: "player not eligible", row: row({ buildZoneId: 7, state: "neutral" }), cap: { nearBuildZoneId: 7, canCapture: false, promptCapture: false }, enabled: false },
  { name: "already being captured", row: row({ buildZoneId: 7, state: "capturing", secondsLeft: 30 }), cap: { nearBuildZoneId: 7, canCapture: true, promptCapture: false }, enabled: false },
  { name: "contested", row: row({ buildZoneId: 7, state: "contested" }), cap: { nearBuildZoneId: 7, canCapture: true, promptCapture: false }, enabled: false }
];
for (const c of enableCases) {
  const cap = capture([c.row], c.cap);
  const h = harness();
  h.setState(withCapture(leaderState, cap));
  h.click({ op: "mine-tab", tab: "ownership" });
  assert.equal(!isDisabled(captureButtonTag(h.root.innerHTML, 7)), c.enabled, "Capture enabled rule: " + c.name);
  assert.equal(ownership.captureEnabled(c.row, cap), c.enabled, "captureEnabled agrees with the rendered button: " + c.name);
}

{
  const h = harness();
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 7 })], { nearBuildZoneId: 9 })));
  h.click({ op: "mine-tab", tab: "ownership" });
  assert.match(h.root.innerHTML, /Go to this workbench to capture it\./);
}

{
  const h = harness();
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 7, state: "owned", ownerProfileId: "p1" })], { nearBuildZoneId: 7 })));
  h.click({ op: "mine-tab", tab: "ownership" });
  assert.match(h.root.innerHTML, /Owned/);
  assert.doesNotMatch(h.root.innerHTML, /Cooldown/, "the server sends no cooldown, so none is shown");
}

{
  const h = harness();
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 20, state: "capturing", capturerProfileId: "p2", progressPercent: 40, secondsLeft: 72 })], { nearBuildZoneId: 20, localProgress: 40, localCapturing: true })));
  h.click({ op: "mine-tab", tab: "ownership" });
  assert.match(h.root.innerHTML, /Being captured · 40% · 1 min 12 s left/);
  assert.equal(isDisabled(cancelTag(h.root.innerHTML)), false, "Cancel is enabled while a local capture is active");
}

{
  const h = harness();
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 40, state: "contested", progressPercent: 15 })])));
  h.click({ op: "mine-tab", tab: "ownership" });
  assert.match(h.root.innerHTML, /Contested, paused at 15%/);
}

{
  const h = harness();
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 7 })], { localProgress: 0 })));
  h.click({ op: "mine-tab", tab: "ownership" });
  assert.equal(isDisabled(cancelTag(h.root.innerHTML)), true, "Cancel is disabled with no local capture progress");
  assert.equal(ownership.cancelEnabled(capture([], { localProgress: 0 })), false);
}

{
  const h = harness();
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 40, state: "contested", progressPercent: 0 })], { nearBuildZoneId: 40, localCapturing: true, localProgress: 0 })));
  h.click({ op: "mine-tab", tab: "ownership" });
  assert.equal(isDisabled(cancelTag(h.root.innerHTML)), false, "Cancel is enabled for a contested or just-started attempt at 0%");
  assert.equal(ownership.cancelEnabled(capture([], { localCapturing: true, localProgress: 0 })), true);
}

{
  const h = harness();
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 7 })], { nearBuildZoneId: 7, lastResult: { ok: false, code: "too_far", message: "x" } })));
  h.click({ op: "mine-tab", tab: "ownership" });
  assert.match(h.root.innerHTML, /Move closer to that workbench to capture it\./);
}

{
  const h = harness();
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 7 })], { nearBuildZoneId: 7, lastResult: { ok: false, code: "hold_limit", message: "x" } })));
  h.click({ op: "mine-tab", tab: "ownership" });
  assert.match(h.root.innerHTML, /You already hold the maximum number of settlements\./);
}

{
  const h = harness();
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 7 })], { nearBuildZoneId: 7, lastResult: { ok: false, code: "timed_out", message: "x" } })));
  h.click({ op: "mine-tab", tab: "ownership" });
  assert.match(h.root.innerHTML, /Capture timed out\. Press H to try again\./);
}

{
  const h = harness();
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 7 })], { nearBuildZoneId: 7, lastResult: { ok: false, code: "made_up_code", message: "x" } })));
  h.click({ op: "mine-tab", tab: "ownership" });
  assert.match(h.root.innerHTML, /Something went wrong\./);
}

{
  const h = harness();
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 7 })], { nearBuildZoneId: 7, lastResult: { ok: true, code: "", message: "" } })));
  h.click({ op: "mine-tab", tab: "ownership" });
  assert.doesNotMatch(h.root.innerHTML, /is-error/, "a successful result shows no error line");
}

{
  const h = harness();
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 7 })], { nearBuildZoneId: 7 })));
  h.click({ op: "mine-tab", tab: "ownership" });
  h.click({ op: "capture", buildZone: "7" });
  h.click({ op: "cancelCapture" });
  h.click({ op: "refreshCapture" });
  assert.deepEqual(plain(h.sent), [
    { op: "capture", buildZoneId: 7 },
    { op: "cancelCapture" },
    { op: "refreshCapture" }
  ]);
}

function bodyClassSet(h) {
  const classes = new Set();
  h.document.body = { classList: { toggle(name, on) { if (on) { classes.add(name); } else { classes.delete(name); } } } };
  return classes;
}

{
  const h = harness();
  const classes = bodyClassSet(h);
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 7 })], { nearBuildZoneId: 7, promptCapture: true })));
  h.window.coFactionSetPrompt(true);
  assert.equal(classes.has("faction-prompt-mode"), true, "prompt mode marks the page");
  assert.doesNotMatch(h.root.innerHTML, /faction-subtabs/, "prompt mode has no tab bar");
  assert.doesNotMatch(h.root.innerHTML, /data-op="refresh"/, "prompt mode has no refresh row");
  assert.match(h.root.innerHTML, /<h1 class="faction-title">Zone 7<\/h1>/, "the card is titled with the settlement name");
  assert.match(h.root.innerHTML, /Do you want to capture this settlement\?/);
  assert.equal(isDisabled(captureButtonTag(h.root.innerHTML, 7)), false, "Capture is enabled at the workbench");
  assert.match(h.root.innerHTML, /data-op="close">Decline</, "prompt mode closes with Decline");
  assert.doesNotMatch(h.root.innerHTML, /data-op="close">Close</);
}

{
  const h = harness();
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 7 })], { nearBuildZoneId: 7, promptCapture: false })));
  h.window.coFactionSetPrompt(true);
  assert.equal(isDisabled(captureButtonTag(h.root.innerHTML, 7)), true, "Capture is disabled when the server refuses");
  assert.match(h.root.innerHTML, /You cannot capture right now\./);
}

{
  const h = harness();
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 7, state: "owned", ownerProfileId: "p1", ownerName: "Nate" })], { nearBuildZoneId: 7, promptCapture: true })));
  h.window.coFactionSetPrompt(true);
  assert.match(h.root.innerHTML, /Owned by Nate/);
  assert.match(h.root.innerHTML, /Do you want to capture this settlement\?/);
}

{
  const h = harness();
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 20, state: "capturing", capturerProfileId: "p2", progressPercent: 40, secondsLeft: 72 })], { nearBuildZoneId: 20, localCapturing: true })));
  h.window.coFactionSetPrompt(true);
  assert.match(h.root.innerHTML, /Being captured · 40% · 1 min 12 s left/);
  assert.doesNotMatch(h.root.innerHTML, /Do you want to capture this settlement\?/);
  assert.equal(isDisabled(cancelTag(h.root.innerHTML)), false, "Cancel capture is enabled during a local capture");
}

{
  const h = harness();
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 7 })], { nearBuildZoneId: 9, promptCapture: true })));
  h.window.coFactionSetPrompt(true);
  assert.match(h.root.innerHTML, /No settlement found at this workbench\./);
  assert.match(h.root.innerHTML, /data-op="prompt-full"/);
  assert.doesNotMatch(h.root.innerHTML, /data-op="capture"/);
}

{
  const h = harness();
  h.setState(leaderState);
  h.window.coFactionSetPrompt(true);
  assert.match(h.root.innerHTML, /Settlement data is not available yet\./);
  assert.match(h.root.innerHTML, /data-op="prompt-full"/);
}

{
  const h = harness();
  const shown = [];
  h.window.coFactionTabs = { show(id) { shown.push(id); }, focusables() { return []; }, cycle() {} };
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 7 })], { nearBuildZoneId: 7, promptCapture: true })));
  h.window.coFactionSetPrompt(true);
  assert.deepEqual(shown, ["coTabFaction"], "prompt mode switches back to the settlement tab");
}

{
  const h = harness();
  const classes = bodyClassSet(h);
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 7 })], { nearBuildZoneId: 7, promptCapture: true })));
  h.window.coFactionSetPrompt(true);
  h.click({ op: "prompt-full" });
  assert.equal(classes.has("faction-prompt-mode"), false, "Full menu removes the prompt class");
  assert.match(h.root.innerHTML, /faction-subtabs/, "Full menu returns to the tabbed window");
  assert.match(h.root.innerHTML, /data-op="refresh"/);
  assert.match(h.root.innerHTML, /data-op="close">Close</);
  assert.deepEqual(plain(h.sent), [], "Full menu sends nothing to the game");
}

{
  const h = harness();
  h.setState(withCapture(leaderState, capture([row({ buildZoneId: 7 })], { nearBuildZoneId: 7, promptCapture: true })));
  assert.match(h.root.innerHTML, /faction-subtabs/, "without prompt mode the tabbed window renders as before");
  assert.match(h.root.innerHTML, /data-op="refresh"/);
  assert.match(h.root.innerHTML, /data-op="close">Close</);
  assert.doesNotMatch(h.root.innerHTML, /Do you want to capture this settlement\?/);
}

{
  const esc = (value) => String(value);
  const self = "profile-owner";
  const ownedState = (access) => ({ ...leaderState, selfProfileId: self, buildAccess: access, settlementCapture: capture([]) });
  const damagedAccess = {
    settlements: [{ buildZoneId: 7, name: "Zone 7", ownerProfileId: self, upkeepPaidUntil: 0, workbenchHp: 400, workbenchMaxHp: 2000, workbenchBroken: false }],
    damagedStructures: [{ buildZoneId: 7, objectId: 42 }],
    lastReject: null,
  };
  const html = ownership.ownershipSection(ownedState(damagedAccess), esc);
  assert.match(html, /<h2>SETTLEMENT REPAIR<\/h2>/, "the owner sees the repair section");
  assert.match(html, /data-op="structureRepair" data-build-zone="7" data-object-id="42"/, "a damaged structure gets a repair button");
  assert.match(html, /data-op="workbenchRepair" data-build-zone="7"/, "a damaged workbench gets a repair button");
  assert.match(html, /data-op="upkeepPay" data-build-zone="7"/, "upkeep can be paid from the window");
  assert.match(html, /Upkeep is not paid yet\./, "unpaid upkeep says so");
  assert.match(html, /Workbench 400 \/ 2000/, "the workbench hp is shown");
}

{
  const esc = (value) => String(value);
  const access = {
    settlements: [{ buildZoneId: 7, name: "Zone 7", ownerProfileId: "someone-else", upkeepPaidUntil: 0 }],
    damagedStructures: [{ buildZoneId: 7, objectId: 42 }],
    lastReject: null,
  };
  const html = ownership.ownershipSection({ ...leaderState, selfProfileId: "profile-owner", buildAccess: access, settlementCapture: capture([]) }, esc);
  assert.doesNotMatch(html, /SETTLEMENT REPAIR/, "a non-owner gets no repair section");
  assert.doesNotMatch(html, /data-op="upkeepPay"/);
  assert.doesNotMatch(html, /data-op="structureRepair"/);
}

{
  const esc = (value) => String(value);
  const self = "profile-owner";
  const base = {
    settlements: [{ buildZoneId: 7, name: "Zone 7", ownerProfileId: self, upkeepPaidUntil: 2000000000, workbenchHp: 2000, workbenchMaxHp: 2000 }],
    damagedStructures: [{ buildZoneId: 7, objectId: 42 }],
  };
  const under = ownership.ownershipSection({ ...leaderState, selfProfileId: self, buildAccess: { ...base, lastReject: { buildZoneId: 7, status: "underAttack" } }, settlementCapture: capture([]) }, esc);
  assert.match(under, /cannot repair while this settlement is under attack/, "an underAttack refusal shows its reason");
  const short = ownership.ownershipSection({ ...leaderState, selfProfileId: self, buildAccess: { ...base, lastReject: { buildZoneId: 7, status: "insufficientMaterials" } }, settlementCapture: capture([]) }, esc);
  assert.match(short, /do not have enough building materials/, "an insufficientMaterials refusal shows its reason");
  assert.match(short, /Upkeep paid until/, "paid upkeep shows the paid-until date");
  assert.doesNotMatch(short, /data-op="workbenchRepair"/, "a full workbench offers no repair");
  const other = ownership.ownershipSection({ ...leaderState, selfProfileId: self, buildAccess: { ...base, lastReject: { buildZoneId: 9, status: "underAttack" } }, settlementCapture: capture([]) }, esc);
  assert.doesNotMatch(other, /under attack/, "a refusal for another settlement is not shown here");
}

{
  const h = harness();
  h.setState({
    ...leaderState,
    selfProfileId: "profile-owner",
    buildAccess: { settlements: [], damagedStructures: [], lastReject: null },
    settlementCapture: capture([]),
  });
  h.click({ op: "structureRepair", buildZone: "7", objectId: "42" });
  assert.deepEqual(h.sent[h.sent.length - 1], { op: "structureRepair", buildZoneId: 7, objectId: 42 });
  h.click({ op: "structureRebuild", buildZone: "7", objectId: "43" });
  assert.deepEqual(h.sent[h.sent.length - 1], { op: "structureRebuild", buildZoneId: 7, objectId: 43 });
  h.click({ op: "upkeepPay", buildZone: "7" });
  assert.deepEqual(h.sent[h.sent.length - 1], { op: "upkeepPay", buildZoneId: 7 });
  h.click({ op: "workbenchRepair", buildZone: "7" });
  assert.deepEqual(h.sent[h.sent.length - 1], { op: "workbenchRepair", buildZoneId: 7 });
}

{
  const esc = (value) => String(value);
  const self = "profile-owner";
  const access = {
    settlements: [{ buildZoneId: 7, name: "Zone 7", ownerProfileId: self, upkeepPaidUntil: 2000000000, workbenchHp: 2000, workbenchMaxHp: 2000 }],
    damagedStructures: [],
    destroyedStructures: [
      { buildZoneId: 7, objectId: 43, pieceType: "scrapMetal", rebuildComponents: 4 },
      { buildZoneId: 9, objectId: 44, pieceType: "wood", rebuildComponents: 4 }
    ],
    lastReject: null,
  };
  const html = ownership.ownershipSection({ ...leaderState, selfProfileId: self, buildAccess: access, settlementCapture: capture([]) }, esc);
  assert.match(html, /data-op="structureRebuild" data-build-zone="7" data-object-id="43">Rebuild scrap metal \(4 steel\)<\/button>/, "a destroyed piece gets a rebuild button with its cost");
  assert.doesNotMatch(html, /data-object-id="44"/, "a destroyed piece of another settlement is not offered here");
  const unknown = ownership.ownershipSection({ ...leaderState, selfProfileId: self, buildAccess: { ...access, destroyedStructures: [{ buildZoneId: 7, objectId: 45, pieceType: "bogus", rebuildComponents: 4 }] }, settlementCapture: capture([]) }, esc);
  assert.doesNotMatch(unknown, /data-op="structureRebuild"/, "an unknown piece type gets no rebuild button");
  const refused = ownership.ownershipSection({ ...leaderState, selfProfileId: self, buildAccess: { ...access, lastReject: { buildZoneId: 7, status: "insufficientMaterials" } }, settlementCapture: capture([]) }, esc);
  assert.match(refused, /do not have enough building materials/, "a refused rebuild shows its reason");
}

{
  const esc = (value) => String(value);
  const self = "profile-builder";
  const access = {
    settlements: [{ buildZoneId: 7, name: "Zone 7", ownerProfileId: "profile-owner", allowedProfileIds: [self], upkeepPaidUntil: 2000000000, workbenchHp: 1000, workbenchMaxHp: 2000 }],
    damagedStructures: [{ buildZoneId: 7, objectId: 42 }],
    destroyedStructures: [],
    lastReject: null,
  };
  const html = ownership.ownershipSection({ ...leaderState, selfProfileId: self, buildAccess: access, settlementCapture: capture([]) }, esc);
  assert.match(html, /<h2>SETTLEMENT REPAIR<\/h2>/, "an allowlisted builder sees the repair section");
  assert.match(html, /data-op="structureRepair" data-build-zone="7" data-object-id="42"/, "a builder can repair a damaged structure");
  assert.match(html, /data-op="workbenchRepair" data-build-zone="7"/, "a builder can repair the workbench");
  assert.doesNotMatch(html, /data-op="upkeepPay"/, "only the owner gets the upkeep button");
  const stranger = ownership.ownershipSection({ ...leaderState, selfProfileId: "profile-stranger", buildAccess: access, settlementCapture: capture([]) }, esc);
  assert.doesNotMatch(stranger, /SETTLEMENT REPAIR/, "a player who is neither owner nor allowlisted gets no repair section");
}

{
  const esc = (value) => String(value);
  const self = "profile-owner";
  const base = {
    settlements: [{ buildZoneId: 7, name: "Zone 7", ownerProfileId: self, upkeepPaidUntil: 2000000000, workbenchHp: 2000, workbenchMaxHp: 2000 }],
    damagedStructures: [{ buildZoneId: 7, objectId: 42 }],
  };
  const far = ownership.ownershipSection({ ...leaderState, selfProfileId: self, buildAccess: { ...base, lastReject: { buildZoneId: 7, status: "tooFar" } }, settlementCapture: capture([]) }, esc);
  assert.match(far, /too far from the object/, "a tooFar refusal tells the player to walk up to the object");
  const denied = ownership.ownershipSection({ ...leaderState, selfProfileId: self, buildAccess: { ...base, lastReject: { buildZoneId: 7, status: "notAllowed" } }, settlementCapture: capture([]) }, esc);
  assert.match(denied, /not allowed/, "a notAllowed refusal shows its reason");
}

console.log("faction.ownership.test: all checks passed.");
