const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

function run(source, location, exercise) {
  const responses = [];
  const classes = [];
  const handlers = {};
  const selectors = [];
  const stack = {
    addEventListener(name, handler) { handlers[name] = handler; },
    querySelectorAll(selector) {
      selectors.push(selector);
      return [];
    },
    querySelector(selector) {
      selectors.push(selector);
      return null;
    },
    set innerHTML(value) {}
  };
  const document = {
    readyState: "complete",
    documentElement: {
      classList: {
        add(value) {
          classes.push(value);
        }
      },
      style: { setProperty() {} }
    },
    getElementById() {
      return stack;
    },
    addEventListener(name, handler) { handlers["document-" + name] = handler; }
  };
  const window = { location, innerWidth: 1920, addEventListener() {}, coAlertAction(payload) { responses.push(JSON.parse(payload)); } };

  vm.runInNewContext(source, {
    document,
    window,
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
    Date,
    Math,
    JSON,
    String,
    Array,
    Object
  });

  if (exercise) exercise(window, handlers);

  return new Promise(resolve => {
    setTimeout(() => resolve({
      previewClass: classes.includes("is-preview"),
      queue: window.getCoAlertQueue(),
      responses,
      selectors
    }), 700);
  });
}

async function main() {
  const alerts = fs.readFileSync(__dirname + "/alerts.js", "utf8");
  const production = await run(alerts, { protocol: "file:", search: "" });
  const explicitDemo = await run(alerts, { protocol: "file:", search: "?demo=1" });
  const previewHtml = fs.readFileSync(__dirname + "/preview.html", "utf8");
  const previewScript = [...previewHtml.matchAll(/<script(?:\s+src="[^"]+")?>([\s\S]*?)<\/script>/g)]
    .map(match => match[1])
    .filter(Boolean)
    .pop();
  const preview = await run(alerts + "\n" + previewScript, { protocol: "file:", search: "" });

  const overflow = await run(alerts, { protocol: "file:", search: "" }, window => {
    for (let i = 0; i < 9; i++) {
      window.showCoAlert({ id: "overflow-" + i, durationMs: 0 });
    }
  });
  assert.equal(overflow.queue.length, 8);
  assert.equal(overflow.responses.length, 1);
  assert.equal(overflow.responses[0].id, "overflow-0");
  assert.equal(overflow.responses[0].action, "expire");

  const retry = await run(alerts, { protocol: "file:", search: "" }, window => {
    window.showCoAlert({ id: "join-failed", actions: [{ id: "retry", hotkey: "F1" }] });
    assert.equal(window.resolveCoAlertHotkey("F1"), true);
  });
  assert.equal(retry.queue.length, 0);
  assert.equal(retry.responses.length, 1);
  assert.equal(retry.responses[0].action, "retry");

  const padA = await run(alerts, { protocol: "file:", search: "" }, window => {
    window.showCoAlert({ id: "join-failed", actions: [{ id: "retry", hotkey: "F1" }] });
    assert.equal(window.resolveCoAlertHotkey("A"), true);
  });
  assert.equal(padA.responses[0].action, "retry");

  const reordered = await run(alerts, { protocol: "file:", search: "" }, window => {
    window.dismissCoAlert("vault", 2);
    window.showCoAlert({ id: "vault", sequence: 1 });
    assert.equal(window.getCoAlertQueue().length, 0);
    window.showCoAlert({ id: "vault", sequence: 3, actions: [{ id: "continue", hotkey: "T" }] });
    window.dismissCoAlert("vault", 2);
    assert.equal(window.getCoAlertQueue().length, 1);
    assert.equal(window.resolveCoAlertHotkey("T"), true);
  });
  assert.equal(reordered.queue.length, 0);
  assert.equal(reordered.responses[0].action, "continue");
  assert.equal(reordered.responses[0].sequence, 3);

  const cleared = await run(alerts, { protocol: "file:", search: "" }, window => {
    window.showCoAlert({ id: "new", sequence: 8 });
    window.dismissAllCoAlerts(true, 7);
    window.showCoAlert({ id: "old", sequence: 6 });
  });
  assert.equal(cleared.queue.length, 1);
  assert.equal(cleared.queue[0].id, "new");

  const invite = await run(alerts, { protocol: "file:", search: "" }, (window, handlers) => {
    window.setCoAlertInteractionMode("mouse");
    window.showCoAlert({ id: "faction-invite-1", sequence: 12, actions: [{ id: "accept", hotkey: "Y" }] });
    const card = { getAttribute: name => name === "data-id" ? "faction-invite-1" : null };
    const button = { getAttribute: name => name === "data-action" ? "accept" : null, closest: () => card };
    handlers.click({ target: { closest: () => button } });
  });
  assert.equal(invite.queue.length, 0);
  assert.equal(invite.responses[0].action, "accept");
  assert.equal(invite.responses[0].sequence, 12);

  const menuKey = await run(alerts, { protocol: "file:", search: "" }, (window, handlers) => {
    window.setCoAlertInteractionMode("mouse");
    window.showCoAlert({ id: "invite-key", sequence: 20, actions: [{ id: "accept", hotkey: "Y" }] });
    let prevented = false;
    handlers["document-keydown"]({ key: "y", preventDefault() { prevented = true; } });
    assert.equal(prevented, true);
  });
  assert.equal(menuKey.responses[0].action, "accept");

  const decline = await run(alerts, { protocol: "file:", search: "" }, window => {
    window.showCoAlert({ id: "faction-invite-2", sequence: 13, actions: [{ id: "decline", hotkey: "N" }] });
    assert.equal(window.resolveCoAlertHotkey("N"), true);
  });
  assert.equal(decline.queue.length, 0);
  assert.equal(decline.responses[0].action, "decline");
  assert.equal(decline.responses[0].sequence, 13);

  const compacted = await run(alerts, { protocol: "file:", search: "" }, window => {
    let requests = 0;
    window.coAlertCompact = () => { requests++; };
    for (let i = 1; i <= 256; i++) window.dismissCoAlert("stale-" + i, i);
    assert.equal(requests, 1);
    window.showCoAlert({ id: "later", sequence: 600 });
    window.compactCoAlerts(500, [{ id: "invite", sequence: 501, actionSequence: 100, actions: [{ id: "accept", hotkey: "Y" }] }]);
    window.showCoAlert({ id: "stale", sequence: 499 });
    assert.deepEqual(Array.from(window.getCoAlertQueue(), a => a.id), ["later", "invite"]);
    assert.equal(window.resolveCoAlertHotkey("Y"), true);
  });
  assert.equal(compacted.responses[0].sequence, 100);

  const hidden = await run(alerts, { protocol: "file:", search: "" }, window => {
    window.showCoAlert({ id: "hidden", sequence: 14, actions: [{ id: "accept", hotkey: "Y" }] });
    for (let i = 0; i < 4; i++) window.showCoAlert({ id: "front-" + i, sequence: 15 + i });
    assert.equal(window.resolveCoAlertHotkey("Y"), false);
  });
  assert.equal(hidden.responses.length, 0);

  const quotedId = await run(alerts, { protocol: "file:", search: "" }, window => {
    window.showCoAlert({ id: 'q"]x', durationMs: 150 });
  });
  assert.equal(quotedId.queue.length, 0, "the timed alert expires");
  assert.equal(quotedId.responses[0].id, 'q"]x');
  assert.equal(quotedId.responses[0].action, "expire");
  assert.ok(quotedId.selectors.includes(".co-alert"), "the progress tick looks cards up through the card list");
  assert.ok(quotedId.selectors.every(selector => !selector.includes('q"]x')), "an id must never be written into a selector");

  assert.equal(production.previewClass, false);
  assert.equal(production.queue.length, 0);
  assert.equal(explicitDemo.previewClass, true);
  assert.equal(explicitDemo.queue.length, 4);
  assert.equal(preview.previewClass, true);
  assert.equal(preview.queue.length, 4);
}

main().then(() => {
  console.log("Alert overlay regression passed");
});
