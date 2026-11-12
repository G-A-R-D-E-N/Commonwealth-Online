const test = require("node:test");
const assert = require("node:assert/strict");
const { settingsEnv, resultEvent, steamPane, steamHtml, click } = require("./settings-harness.js");

test("vats cycles through the server modes and the save carries it", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  assert.equal(env.state.serverSettings.values.vats, "real-time");
  let select = steamPane(env).querySelector("#co-ss-vats");
  select.value = "real-time";
  env.window.CO_ServerSettings.activate(env.mount, "co-ss-vats", env.state);
  assert.equal(env.state.serverSettings.draft.vats, "vanilla");
  assert.match(steamHtml(env), /<option value="vanilla" selected>vanilla<\/option>/);
  click(steamPane(env).querySelector("#co-ss-save"));
  const save = env.calls.find((call) => call.action === "serverSettingsSave");
  assert.ok(save, "no save action sent");
  assert.equal(save.values.vats, "vanilla");
});
