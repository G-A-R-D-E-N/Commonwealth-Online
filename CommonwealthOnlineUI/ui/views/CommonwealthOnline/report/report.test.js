const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const source = fs.readFileSync(__dirname + "/report.js", "utf8");

function load() {
  const sent = [];
  let html = "";
  const root = {
    set innerHTML(value) { html = value; },
    get innerHTML() { return html; },
    querySelector() { return null; },
    querySelectorAll() { return []; },
    addEventListener() {}
  };
  const window = { innerWidth: 1920, coInfoAction: (json) => sent.push(JSON.parse(json)) };
  const document = {
    readyState: "complete",
    activeElement: null,
    documentElement: { style: { setProperty() {} } },
    getElementById: (id) => (id === "coInfo" ? root : null),
    addEventListener() {}
  };
  vm.runInNewContext(source, { window, document });
  return { window, sent, html: () => html };
}

{
  const { window, html } = load();
  assert.match(html(), /Report a player/);
  assert.match(html(), /Get admin help/);
  assert.match(html(), /No other players are in this session/);
  assert.doesNotMatch(html(), /data-field="playerId"/, "an empty session shows no player select");
  assert.match(html(), /<button[^>]*data-action="submit"[^>]*disabled/, "the report button is disabled with nobody to report");

  window.coReportSetState({ players: [{ playerId: 7, name: "Raider" }, { playerId: 9, name: "Scout <2>" }] });
  const page = html();
  const select = page.match(/data-field="playerId">([\s\S]*?)<\/select>/);
  assert.ok(select, "the player select is rendered");
  assert.equal((select[1].match(/<option /g) || []).length, 2, "the select lists only the players the server sent");
  assert.match(page, /<option value="7" selected>Raider<\/option>/);
  assert.match(page, /Scout &lt;2&gt;/, "player names are escaped");
  assert.doesNotMatch(page, /No other players are in this session/);
  assert.equal((page.match(/<option value="/g) || []).length, 6, "two players plus four reasons");

  window.coReportSetState({ players: [{ playerId: 9, name: "Scout" }] });
  assert.match(html(), /<option value="9" selected>Scout<\/option>/, "a player who left is replaced by a present one");
  assert.doesNotMatch(html(), /value="7"/);
}

{
  const { window, sent } = load();
  window.coReportSetState({ players: [{ playerId: 7, name: "Raider" }] });
  window.coReportModel.setMode("support");
  assert.equal(window.coReportModel.validate("support", { note: "   " }), "noteRequired");
  assert.equal(window.coReportModel.send("support", { note: "   " }), false);
  assert.deepEqual(sent, [], "a blank admin-help note is not sent");
  assert.match(window.coReportModel.render(), /Describe the problem first\./, "the blank note explains itself on screen");
  assert.equal(window.coReportModel.validate("support", { note: "Need an admin" }), "");
  assert.equal(window.coReportModel.validate("support", { note: "" }), "noteRequired");
}

{
  const { window, html } = load();
  window.coReportSetState({ players: [{ playerId: 7, name: "Raider" }] });
  assert.doesNotMatch(html(), /maxlength=/);
  assert.equal(window.coReportModel.validate("support", { note: "x".repeat(200) }), "");
  assert.equal(window.coReportModel.validate("support", { note: "x".repeat(201) }), "noteTooLong");
  assert.equal(window.coReportModel.validate("support", { note: "\u00e9".repeat(100) }), "");
  assert.equal(window.coReportModel.validate("support", { note: "\u00e9".repeat(101) }), "noteTooLong");
  assert.equal(window.coReportModel.validate("support", { note: " \t" + "\u00e9".repeat(100) + "\n" }), "");
  assert.equal(window.coReportModel.validate("support", { note: "\ud800" }), "unsafeText");
  assert.equal(window.coReportModel.validate("player", { playerId: "7", reason: "other", note: "\ud83d\ude00".repeat(51) }), "noteTooLong");
  assert.match(html(), /0 \/ 200 bytes/);
  assert.equal(window.coReportModel.validate("player", { playerId: "7", reason: "other", note: "x".repeat(201) }), "noteTooLong");
  assert.equal(window.coReportModel.validate("player", { playerId: "7", reason: "other", note: "x".repeat(200) }), "");
}

{
  const { window, html } = load();
  window.coReportSetState({ players: [{ playerId: 7, name: "Raider" }] });
  assert.equal(window.coReportModel.validate("player", { playerId: "8", reason: "other", note: "" }), "targetUnavailable");
  assert.equal(window.coReportModel.validate("player", { playerId: "7", reason: "spam", note: "" }), "invalidReason");

  const expected = {
    invalidKind: "That player is not in this session.",
    invalidTarget: "That player is not in this session.",
    targetUnavailable: "That player is not in this session.",
    selfTarget: "You cannot report yourself.",
    invalidReason: "Pick a reason.",
    noteRequired: "Describe the problem first.",
    noteTooLong: "Keep the note to 200 UTF-8 bytes or less.",
    unsafeText: "The note has characters that cannot be sent.",
    rateLimited: "You sent too many reports. Try again in a minute."
  };
  Object.keys(expected).forEach((error) => {
    assert.equal(window.coReportModel.sentenceFor(error), expected[error], error);
  });
  assert.equal(window.coReportModel.sentenceFor("somethingElse"), "The report could not be sent.");

  assert.equal(window.coReportModel.send("player", { playerId: "7", reason: "other", note: "" }), true);
  window.coReportEvent({ type: "reportResult", ok: false, error: "selfTarget" });
  assert.match(html(), /You cannot report yourself\./);
  assert.match(html(), /class="info-status is-error"/);
  window.coReportModel.send("player", { playerId: "7", reason: "other", note: "" });
  window.coReportEvent({ type: "reportResult", ok: false, error: "unknownFailure" });
  assert.match(html(), /The report could not be sent\./);
  window.coReportModel.send("player", { playerId: "7", reason: "other", note: "" });
  window.coReportEvent({ type: "reportResult", ok: true, reportId: "r1", serverTime: 1 });
  assert.match(html(), /Thanks\. Your report was sent\./);
  assert.doesNotMatch(html(), /is-error/);
}

{
  const { window, sent, html } = load();
  window.coReportSetState({ players: [{ playerId: 7, name: "Raider" }] });
  assert.equal(window.coReportModel.send("player", { playerId: "7", reason: "cheating", note: "aimbot" }), true);
  assert.deepEqual(sent, [{ action: "submit", kind: "player", targetPlayerId: 7, reason: "cheating", note: "aimbot" }]);
  assert.equal(window.coReportModel.send("player", { playerId: "7", reason: "cheating", note: "again" }), false, "a second send waits for the first");
  assert.equal(sent.length, 1);
  assert.match(html(), /<button[^>]*data-action="submit"[^>]*disabled/, "the button is disabled while the send is pending");

  window.coReportEvent({ type: "reportResult", ok: false, error: "rateLimited" });
  assert.match(html(), /You sent too many reports\. Try again in a minute\./);
  assert.doesNotMatch(html(), /<button[^>]*data-action="submit"[^>]*disabled/, "the button is enabled after the result");
  assert.match(html(), /class="info-status is-error"/);
}

{
  const { window, sent } = load();
  assert.equal(window.coReportModel.send("support", { note: "Stuck in a wall" }), true);
  assert.deepEqual(sent, [{ action: "submit", kind: "support", note: "Stuck in a wall" }]);
}

{
  const { window, sent } = load();
  const note = " ".repeat(3000) + "\u00e9" + "\t".repeat(3000);
  assert.equal(window.coReportModel.send("support", { note }), true);
  assert.deepEqual(sent, [{ action: "submit", kind: "support", note: "\u00e9" }]);
  window.coReportEvent({ type: "reportResult", ok: true });
  window.coReportSetState({ players: [{ playerId: 7, name: "Raider" }] });
  assert.equal(window.coReportModel.send("player", { playerId: "7", reason: "other", note }), true);
  assert.equal(sent[1].note, "\u00e9");
}

{
  const { window, sent } = load();
  window.handleControllerInput("a", "down");
  assert.deepEqual(sent, [], "A does not close the report");
  window.handleControllerInput("b", "up");
  assert.deepEqual(sent, [], "B closes on press only");
  window.handleControllerInput("b", "down");
  assert.deepEqual(sent, [{ action: "close" }]);
}

console.log("report view tests passed");
