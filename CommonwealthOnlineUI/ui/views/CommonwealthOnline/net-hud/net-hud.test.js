const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const source = fs.readFileSync(__dirname + "/net-hud.js", "utf8");

function fakeElement() {
  return { hidden: false, textContent: "" };
}

function load() {
  const elements = {
    coNetHud: fakeElement(),
    coNetPing: fakeElement(),
    coNetQuality: fakeElement(),
    coNetIn: fakeElement(),
    coNetOut: fakeElement()
  };
  const window = {};
  const document = {
    getElementById(id) {
      return elements[id] || null;
    }
  };
  vm.runInNewContext(source, { window, document });
  return { window, elements };
}

const stats = { pingMs: 42.4, qualityLocal: 100, qualityRemote: 50, inKBps: 2048 / 1024, outKBps: 0.5, inPps: 3, outPps: 4, pendingBytes: 0 };
const steamStats = Object.assign({}, stats, { pingMs: -1 });

function stateJson(showPing, showStats, value) {
  return JSON.stringify({ showPing, showStats, stats: value || stats });
}

{
  const { window } = load();
  const model = window.coNetHudModel;

  const parsed = model.parseState(stateJson(true, false));
  assert.equal(parsed.showPing, true);
  assert.equal(parsed.showStats, false);
  assert.equal(parsed.stats.pingMs, 42.4);
  assert.equal(parsed.stats.inKBps, 2);
  assert.equal(model.parseState("not json"), null);
  assert.equal(model.parseState("[]"), null);
  assert.equal(model.parseState(JSON.stringify({ showPing: true, showStats: false })), null);
  assert.equal(model.parseState(JSON.stringify({ showPing: "yes", showStats: false, stats })), null);
  assert.equal(model.parseState(JSON.stringify({ showPing: true, showStats: false, stats: { pingMs: 1 } })), null);
  assert.equal(model.parseState(stateJson(true, false, Object.assign({}, stats, { inKBps: "2" }))), null);
  assert.equal(model.parseState(""), null);
}

{
  const { window } = load();
  const model = window.coNetHudModel;

  assert.equal(model.pingText(42.4), "Ping: 42 ms");
  assert.equal(model.pingText(-1), "Ping: -");
  assert.equal(model.pingText(0), "Ping: 0 ms");
  assert.equal(model.qualityText(stats), "Quality: 100% / 50%");
  assert.equal(model.rateText("In", 2), "In: 2.0 KB/s");
  assert.equal(model.rateText("Out", 0.5), "Out: 0.5 KB/s");
}

{
  const { window, elements } = load();
  window.coNetHudSetState(stateJson(true, false));
  assert.equal(elements.coNetHud.hidden, false);
  assert.equal(elements.coNetPing.hidden, false);
  assert.equal(elements.coNetPing.textContent, "Ping: 42 ms");
  assert.equal(elements.coNetQuality.hidden, true);
  assert.equal(elements.coNetIn.hidden, true);
  assert.equal(elements.coNetOut.hidden, true);
}

{
  const { window, elements } = load();
  window.coNetHudSetState(stateJson(true, true, steamStats));
  assert.equal(elements.coNetHud.hidden, false);
  assert.equal(elements.coNetPing.textContent, "Ping: -");
  assert.equal(elements.coNetPing.hidden, false);
  assert.equal(elements.coNetQuality.hidden, false);
  assert.equal(elements.coNetQuality.textContent, "Quality: 100% / 50%");
  assert.equal(elements.coNetIn.textContent, "In: 2.0 KB/s");
  assert.equal(elements.coNetOut.textContent, "Out: 0.5 KB/s");
  assert.equal(elements.coNetOut.hidden, false);
}

{
  const { window, elements } = load();
  window.coNetHudSetState(stateJson(false, true));
  assert.equal(elements.coNetHud.hidden, false);
  assert.equal(elements.coNetPing.hidden, true);
  assert.equal(elements.coNetQuality.hidden, false);
  assert.equal(elements.coNetIn.hidden, false);
}

{
  const { window, elements } = load();
  window.coNetHudSetState(stateJson(true, false));
  window.coNetHudSetState(stateJson(false, false));
  assert.equal(elements.coNetHud.hidden, true);
  window.coNetHudSetState("");
  assert.equal(elements.coNetHud.hidden, true);
  window.coNetHudSetState(stateJson(true, true));
  window.coNetHudSetState("");
  assert.equal(elements.coNetHud.hidden, true);
}

{
  const { window, elements } = load();
  window.coNetHudSetState(stateJson(true, false));
  window.coNetHudSetState("garbage");
  assert.equal(elements.coNetHud.hidden, true);
}

{
  const loaded = load();
  loaded.window.coNetHudModel.render(null);
  assert.equal(loaded.elements.coNetHud.hidden, true);
}
