const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const source = fs.readFileSync(__dirname + "/server.js", "utf8");

function load() {
  const sent = [];
  let html = "";
  const root = {
    set innerHTML(value) { html = value; },
    get innerHTML() { return html; },
    querySelector() { return null; },
    addEventListener() {}
  };
  const window = { innerWidth: 1920, coInfoAction: (json) => sent.push(JSON.parse(json)) };
  const document = {
    readyState: "complete",
    documentElement: { style: { setProperty() {} } },
    getElementById: (id) => (id === "coInfo" ? root : null),
    addEventListener() {}
  };
  vm.runInNewContext(source, { window, document });
  return { window, sent, html: () => html };
}

{
  const { window, html } = load();
  assert.match(html(), /Not connected/, "an empty page reports no connection");
  assert.doesNotMatch(html(), /Your ping/);

  window.coServerSetInfo({ connected: true, name: "Wasteland <One>", host: "195.26.244.234", port: 7777, players: 3, maxPlayers: 16, ping: 42 });
  window.coServerSetMode({ label: "CO-OP STORY" });
  const page = html();
  assert.match(page, /Connected/);
  assert.match(page, /Wasteland &lt;One&gt;/, "the server name is escaped");
  assert.match(page, /195\.26\.244\.234:7777/);
  assert.match(page, /CO-OP STORY/);
  assert.match(page, /3 \/ 16/);
  assert.match(page, /42 ms/);

  window.coServerSetInfo({ connected: true, host: "10.0.0.2", players: 0, maxPlayers: 0, ping: 0 });
  assert.match(html(), /10\.0\.0\.2/);
  assert.match(html(), /<span>1<\/span>/, "an unknown player count shows at least the local player");
  assert.match(html(), /Your ping<\/span><span>—/);

  window.coServerSetInfo({ connected: false });
  assert.match(html(), /Not connected/);
}

{
  const { window, sent } = load();
  window.handleControllerInput("b", "down");
  window.handleControllerInput("b", "up");
  assert.deepEqual(sent, [{ action: "close" }], "B closes on press only");
}

console.log("server view tests passed");
