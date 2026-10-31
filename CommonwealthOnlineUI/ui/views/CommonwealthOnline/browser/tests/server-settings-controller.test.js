const test = require("node:test");
const assert = require("node:assert/strict");
const { settingsEnv, resultEvent, steamPane, steamHtml, click } = require("./settings-harness.js");

test("controller navigation walks the server settings controls", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  const ids = env.window.CO_SteamFriends.fieldIds(env.state);
  const start = ids.indexOf("co-ss-folder");
  assert.ok(start > 0, "settings fields are reachable from the host form");
  const settingsIds = env.window.CO_ServerSettings.fieldIds();
  assert.deepEqual(ids.slice(start, start + settingsIds.length), settingsIds);
  assert.deepEqual(Array.from(ids.slice(start + settingsIds.length)), ["co-steam-friend-code", "co-steam-join-password", "co-steam-join", "co-steam-hosts-refresh", "co-steam-scan"]);

  env.state.focusZone = "steam-form";
  env.state.steamFieldIndex = ids.indexOf("co-ss-doors-shared");
  env.action("co.select");
  assert.equal(env.state.serverSettings.draft.doorsShared, true);
  assert.equal(steamPane(env).querySelector("#co-ss-doors-shared").getAttribute("aria-pressed"), "true");

  env.state.steamFieldIndex = ids.indexOf("co-ss-world-mode");
  env.action("co.select");
  assert.equal(env.state.serverSettings.draft.worldMode, "coop-story");
  assert.match(steamHtml(env), /<option value="coop-story" selected>coop-story<\/option>/);

  env.state.steamFieldIndex = ids.indexOf("co-ss-downed-seconds");
  env.action("co.select");
  assert.equal(env.window.CO_Keyboard.getTarget().id, "co-ss-downed-seconds");
  env.action("co.back");

  env.calls.length = 0;
  env.state.steamFieldIndex = ids.indexOf("co-ss-save");
  env.action("co.select");
  assert.equal(env.calls.filter((call) => call.action === "serverSettingsSave").length, 1);
});

test("the prompt bar labels the settings controls", () => {
  const env = settingsEnv();
  const labelOf = (index) => {
    const ids = env.window.CO_SteamFriends.fieldIds(env.state);
    return env.window.CO_ControllerPrompts.getPrompts({
      activeTab: "steam-friends",
      focusZone: "steam-form",
      steamFieldIndex: index,
      localServers: []
    }).find((prompt) => prompt.key === "A").label;
  };
  const ids = env.window.CO_SteamFriends.fieldIds(env.state);
  assert.equal(labelOf(ids.indexOf("co-ss-name")), "Edit");
  assert.equal(labelOf(ids.indexOf("co-ss-folder")), "Edit");
  assert.equal(labelOf(ids.indexOf("co-ss-world-mode")), "Change");
  assert.equal(labelOf(ids.indexOf("co-ss-chat")), "Change");
  assert.equal(labelOf(ids.indexOf("co-ss-save")), "Save");
  assert.equal(labelOf(ids.indexOf("co-ss-reload")), "Reload");
});

test("settings stay reachable when Steam hosting is unavailable", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  assert.equal(env.state.steamP2pAvailable, false);
  env.state.focusZone = "steam-form";
  const ids = env.window.CO_SteamFriends.fieldIds(env.state);
  env.state.steamFieldIndex = ids.indexOf("co-ss-friendly-fire");
  env.action("co.select");
  assert.equal(env.state.serverSettings.draft.friendlyFire, true);
});

test("max players cycles through every allowed value from the controller", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  assert.equal(env.state.serverSettings.values.maxPlayers, 8);
  let select = steamPane(env).querySelector("#co-ss-max-players");
  select.value = "8";
  env.window.CO_ServerSettings.activate(env.mount, "co-ss-max-players", env.state);
  assert.equal(env.state.serverSettings.draft.maxPlayers, 9);
  select = steamPane(env).querySelector("#co-ss-max-players");
  select.value = "9";
  env.window.CO_ServerSettings.activate(env.mount, "co-ss-max-players", env.state);
  assert.equal(env.state.serverSettings.draft.maxPlayers, 10);
});

test("a second Save before the first reply is ignored", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  click(steamPane(env).querySelector("#co-ss-save"));
  click(steamPane(env).querySelector("#co-ss-save"));
  assert.equal(env.calls.filter((call) => call.action === "serverSettingsSave").length, 1);
  env.window.dispatchCoEvent(Object.assign(resultEvent(), { action: "serverSettingsSave" }));
  click(steamPane(env).querySelector("#co-ss-save"));
  assert.equal(env.calls.filter((call) => call.action === "serverSettingsSave").length, 2);
});

test("a Save still blocks a second Save when an unrelated read reply lands", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  click(steamPane(env).querySelector("#co-ss-save"));
  assert.equal(env.calls.filter((call) => call.action === "serverSettingsSave").length, 1);
  env.window.dispatchCoEvent(resultEvent());
  click(steamPane(env).querySelector("#co-ss-save"));
  assert.equal(env.calls.filter((call) => call.action === "serverSettingsSave").length, 1);
  env.window.dispatchCoEvent(Object.assign(resultEvent(), { action: "serverSettingsSave" }));
  click(steamPane(env).querySelector("#co-ss-save"));
  assert.equal(env.calls.filter((call) => call.action === "serverSettingsSave").length, 2);
});

test("a Reload on a rejected folder keeps the config that was read", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  const pane = steamPane(env);
  const folder = pane.querySelector("#co-ss-folder");
  folder.value = "D:/Not A Server";
  folder.dispatchEvent({ type: "input", bubbles: true });
  click(steamPane(env).querySelector("#co-ss-reload"));
  env.window.dispatchCoEvent(resultEvent({
    data: {
      serverDirectory: "",
      configPath: "",
      errors: ["That server folder does not exist."],
      limits: { maximumPlayers: 16, worldModes: ["coop-story", "free-roam"] },
      values: resultEvent().data.values
    }
  }));
  assert.equal(env.state.serverSettings.folder, "C:/Games/CO");
  assert.equal(steamPane(env).querySelector("#co-ss-name").value, "Vault 111");
  assert.match(steamHtml(env), /That server folder does not exist\./);
});
