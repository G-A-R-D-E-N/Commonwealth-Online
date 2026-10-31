const { assert, fs, vm, harness, leaderState, notInFaction, plain } = require("./faction.harness.js");

const ownershipContext = { window: {} };
vm.runInNewContext(fs.readFileSync(__dirname + "/faction.ownership.js", "utf8"), ownershipContext);
const ownership = ownershipContext.window.coFactionOwnership;

function row(extra) {
  return { buildZoneId: 7, state: "neutral", ownerProfileId: "", ownerName: "", capturerProfileId: "", capturerName: "", raid: false, progressPercent: 0, secondsLeft: 0, cooldownSeconds: 0, ...extra };
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
  assert.match(h.root.innerHTML, /Settlement 7/);
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
  h.setState(withCapture(leaderState, capture([])));
  h.click({ op: "mine-tab", tab: "ownership" });
  assert.match(h.root.innerHTML, /No settlements are known yet\./);
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
  const order = [30, 20, 40, 10, 45, 50].map((zone) => html.indexOf("Settlement " + zone));
  order.forEach((at) => assert.ok(at >= 0, "each valid settlement renders"));
  assert.deepEqual(order.slice().sort((a, b) => a - b), order, "the rendered order matches the sort");
  assert.doesNotMatch(html, /Settlement 0/, "a zero id is never rendered");
  assert.doesNotMatch(html, /Settlement 60/, "an unknown state is never rendered");
}

const enableCases = [
  { name: "neutral, near, eligible", row: row({ buildZoneId: 7, state: "neutral" }), cap: { nearBuildZoneId: 7, canCapture: true }, enabled: true },
  { name: "owned, near, eligible (raid)", row: row({ buildZoneId: 7, state: "owned", ownerProfileId: "p1" }), cap: { nearBuildZoneId: 7, canCapture: true }, enabled: true },
  { name: "not near", row: row({ buildZoneId: 7, state: "neutral" }), cap: { nearBuildZoneId: 9, canCapture: true }, enabled: false },
  { name: "player not eligible", row: row({ buildZoneId: 7, state: "neutral" }), cap: { nearBuildZoneId: 7, canCapture: false }, enabled: false },
  { name: "already being captured", row: row({ buildZoneId: 7, state: "capturing", secondsLeft: 30 }), cap: { nearBuildZoneId: 7, canCapture: true }, enabled: false },
  { name: "contested", row: row({ buildZoneId: 7, state: "contested" }), cap: { nearBuildZoneId: 7, canCapture: true }, enabled: false }
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

console.log("faction.ownership.test: all checks passed.");
