const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const source = fs.readFileSync(__dirname + "/help.js", "utf8");
const settingsData = fs.readFileSync(__dirname + "/../settings/settings-data.js", "utf8");

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
  const context = { window, document };
  vm.runInNewContext(settingsData, context);
  vm.runInNewContext(source, context);
  return { window, sent, html: () => html };
}

{
  const { window, html } = load();
  const defaults = window.CO_SettingsData.flatMap((section) => section.items);
  const ptt = defaults.find((item) => item.key === "voice.pushToTalkKey").value;
  assert.ok(ptt, "the settings data carries a push-to-talk default");
  assert.ok(html().includes(">" + ptt + "<"), "help shows the settings default push-to-talk key");
  assert.match(html(), /Enter/);
  assert.match(html(), /downed/);
  assert.doesNotMatch(html(), /shout to distant/i, "unbound whisper and shout keys are not listed");

  window.coVoiceEvent({ kind: "settings", values: { "voice.pushToTalkKey": "V", "voice.shoutKey": "G" } });
  assert.match(html(), />V</, "a rebound key replaces the default");
  assert.match(html(), />G<[\s\S]*shout/);

  window.coVoiceEvent({ kind: "settings", values: { "voice.pushToTalk": false } });
  assert.match(html(), /Push-to-talk is off/);

  window.coVoiceEvent({ kind: "settings", values: { "voice.pushToTalk": true, "voice.pushToTalkKey": "" } });
  assert.match(html(), /no key bound/);

  window.coVoiceEvent({ kind: "settings", values: { "voice.pushToTalkKey": "<b>" } });
  assert.doesNotMatch(html(), /<b>/, "key names are escaped");

  window.coVoiceEvent({ kind: "level", values: { "voice.pushToTalkKey": "Z" } });
  assert.doesNotMatch(html(), />Z</, "only settings events change the keys");
}

{
  const { window, sent } = load();
  window.handleControllerInput("b", "down");
  assert.deepEqual(sent, [{ action: "close" }]);
}

console.log("help view tests passed");
