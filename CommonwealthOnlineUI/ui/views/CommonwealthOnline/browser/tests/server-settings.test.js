const test = require("node:test");
const assert = require("node:assert/strict");
const { settingsEnv, resultEvent, steamPane, steamHtml, click } = require("./settings-harness.js");

test("the panel loads the file as soon as the host tab opens", () => {
  const env = settingsEnv();
  assert.equal(env.calls.length, 1);
  assert.equal(env.calls[0].action, "serverSettingsGet");

  env.window.dispatchCoEvent(resultEvent());
  assert.equal(env.calls.filter((call) => call.action === "serverSettingsGet").length, 1, "no reload loop");
});

test("every host-editable setting has a control and the file path is shown", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  const pane = steamPane(env);
  for (const id of env.window.CO_ServerSettings.fieldIds()) {
    assert.ok(pane.querySelector("#" + id), "missing control " + id);
  }
  for (const key of ["serverName", "serverDescription", "maxPlayers", "worldMode", "vats", "doorsShared", "friendlyFire",
    "factionFriendlyFire", "downedSeconds", "enableChat", "enableFactions", "enablePersistence", "enableObjectState",
    "lanDiscoveryEnabled", "publicListing", "resumeTokenLifetimeHours"]) {
    assert.ok(env.window.CO_ServerSettings.FIELDS.some((field) => field.key === key), "no control for " + key);
  }
  assert.equal(pane.querySelector("#co-ss-name").value, "Vault 111");
  assert.equal(pane.querySelector("#co-ss-downed-seconds").value, "45");
  assert.match(steamHtml(env), /<option value="8" selected>8<\/option>/);
  assert.match(steamHtml(env), /<option value="free-roam" selected>free-roam<\/option>/);
  assert.match(steamHtml(env), /<option value="real-time" selected>real-time<\/option>/);
  assert.equal(pane.querySelector("#co-ss-friendly-fire").getAttribute("aria-pressed"), "false");
  assert.equal(pane.querySelector("#co-ss-faction-friendly-fire").getAttribute("aria-pressed"), "true");
  assert.equal(pane.querySelector("#co-ss-listing").getAttribute("aria-pressed"), "true");
  assert.match(steamHtml(env), /C:\/Games\/CO\/commonwealth-server\.json/);
  assert.match(steamHtml(env), /Every setting is applied to the running server straight away/);
  assert.match(steamHtml(env), /the saved file is used at the next Host/);
});

test("network-internal and security fields are absent and starting inventory is called out", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  const html = steamHtml(env).toLowerCase();
  for (const forbidden of ["directory_url", "directoryurl", "admin_port", "adminport", "admin_profiles", "adminprofiles",
    "required_client_version", "required_fallout_runtime_version", "worldPolicyPath".toLowerCase()]) {
    assert.equal(html.includes(forbidden), false, "exposed " + forbidden);
  }
  assert.equal(html.includes('id="co-ss-modpack'), false, "exposed a modpack input");
  assert.match(html, /required_modpack_path/);
  assert.match(html, /not anti-cheat/);
  assert.match(html, /starting_inventory/);
  assert.match(html, /not editable/);
});

test("saving sends the folder, the values and the password mode", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  let pane = steamPane(env);
  pane.querySelector("#co-ss-name").value = "New Name";
  pane.querySelector("#co-ss-name").dispatchEvent({ type: "input", bubbles: true });
  pane.querySelector("#co-ss-password").value = "hunter2";
  click(pane.querySelector("#co-ss-friendly-fire"));
  pane = steamPane(env);
  click(pane.querySelector("#co-ss-save"));

  const save = env.calls.find((call) => call.action === "serverSettingsSave");
  assert.ok(save, "no save action sent");
  assert.equal(save.serverDirectory, "C:/Games/CO");
  assert.equal(save.values.serverName, "New Name");
  assert.equal(save.values.friendlyFire, true);
  assert.equal(save.values.maxPlayers, 8);
  assert.equal(save.values.downedSeconds, 45);
  assert.equal(save.values.publicListing, true);
  assert.equal(save.passwordMode, "set");
  assert.equal(save.password, "hunter2");
  assert.equal(JSON.stringify(env.state.serverSettings).includes("hunter2"), false, "password kept out of state");
  assert.equal(pane.querySelector("#co-ss-password").value, "hunter2", "the field keeps the password until the file takes it");

  env.window.dispatchCoEvent(Object.assign(resultEvent(), {
    action: "serverSettingsSave",
    data: Object.assign({}, resultEvent().data, { liveApplied: false })
  }));
  assert.equal(steamPane(env).querySelector("#co-ss-password").value, "");
});

test("an empty password keeps the saved one and the clear toggle removes it", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  const pane = steamPane(env);
  click(pane.querySelector("#co-ss-save"));
  let save = env.calls.find((call) => call.action === "serverSettingsSave");
  assert.equal(save.passwordMode, "keep");

  env.window.dispatchCoEvent(Object.assign(resultEvent(), { action: "serverSettingsSave" }));
  env.calls.length = 0;
  click(steamPane(env).querySelector("#co-ss-password-clear"));
  click(steamPane(env).querySelector("#co-ss-save"));
  save = env.calls.find((call) => call.action === "serverSettingsSave");
  assert.equal(save.passwordMode, "clear");
});

test("public listing defaults to on and saving the toggle turned off sends false", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  let pane = steamPane(env);
  assert.equal(pane.querySelector("#co-ss-listing").getAttribute("aria-pressed"), "true");
  click(pane.querySelector("#co-ss-listing"));
  pane = steamPane(env);
  assert.equal(pane.querySelector("#co-ss-listing").getAttribute("aria-pressed"), "false");
  click(pane.querySelector("#co-ss-save"));
  const save = env.calls.find((call) => call.action === "serverSettingsSave");
  assert.ok(save, "no save action sent");
  assert.equal(save.values.publicListing, false);
});

test("saving without a server folder is refused and sends nothing", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent({
    data: {
      serverDirectory: "",
      configPath: "",
      errors: ["Set the server folder that holds CommonwealthOnline.Server.exe."],
      limits: { maximumPlayers: 16, worldModes: ["coop-story", "free-roam"] },
      values: resultEvent().data.values
    }
  }));
  const pane = steamPane(env);
  click(pane.querySelector("#co-ss-save"));
  assert.equal(env.calls.filter((call) => call.action === "serverSettingsSave").length, 0);
  assert.match(steamHtml(env), /Set the server folder/);
});

test("reload reads the folder typed into the panel", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  const pane = steamPane(env);
  const folder = pane.querySelector("#co-ss-folder");
  folder.value = "D:/CO Server";
  folder.dispatchEvent({ type: "input", bubbles: true });
  env.calls.length = 0;
  click(pane.querySelector("#co-ss-reload"));
  assert.deepEqual(env.calls, [{ action: "serverSettingsGet", serverDirectory: "D:/CO Server" }]);
});

test("a saved world mode reports whether the running server took it", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  env.window.dispatchCoEvent(Object.assign(resultEvent(), {
    action: "serverSettingsSave",
    data: Object.assign({}, resultEvent().data, { liveApplied: true, liveResult: { applied: ["worldMode"] } })
  }));
  assert.match(steamHtml(env), /Applied to the running server: World mode/);

  env.window.dispatchCoEvent(Object.assign(resultEvent(), {
    action: "serverSettingsSave",
    data: Object.assign({}, resultEvent().data, { liveApplied: false })
  }));
  assert.match(steamHtml(env), /Saved\. No server is running/);
  assert.equal(steamHtml(env).includes("Applied to the running server"), false);
});

test("a saved setting the server could not apply live is reported beside the applied ones", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  env.window.dispatchCoEvent(Object.assign(resultEvent(), {
    action: "serverSettingsSave",
    data: Object.assign({}, resultEvent().data, {
      liveApplied: true,
      liveResult: {
        applied: ["serverName"],
        errors: [{ field: "enablePersistence", message: "Durable persistence is not available on this server." }]
      }
    })
  }));
  const html = steamHtml(env);
  assert.match(html, /Applied to the running server: Listed server name/);
  assert.match(html, /Saved, but not applied live: Persistence \(Durable persistence is not available on this server\.\)/);
  assert.match(html, /It takes effect at the next Host\./);
});

test("an edit after a save drops the claim that it was saved", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  const save = () => env.window.dispatchCoEvent(Object.assign(resultEvent(), {
    action: "serverSettingsSave",
    data: Object.assign({}, resultEvent().data, { liveApplied: true, liveResult: { applied: ["worldMode"] } })
  }));

  save();
  assert.match(steamHtml(env), /Applied to the running server: World mode/);
  assert.ok(steamPane(env).querySelector("#co-ss-status"), "no status line to drop");
  click(steamPane(env).querySelector("#co-ss-friendly-fire"));
  assert.equal(env.state.serverSettings.status, "", "a toggle edit kept the old save claim");
  assert.equal(steamHtml(env).includes("Applied to the running server"), false);
  assert.equal(steamPane(env).querySelector("#co-ss-status"), null);

  save();
  assert.match(steamHtml(env), /Applied to the running server: World mode/);
  const pane = steamPane(env);
  assert.ok(pane.querySelector("#co-ss-status"), "no status line to drop");
  pane.querySelector("#co-ss-name").value = "Edited after saving";
  pane.querySelector("#co-ss-name").dispatchEvent({ type: "input", bubbles: true });
  assert.equal(env.state.serverSettings.status, "", "a text edit kept the old save claim");
  assert.equal(pane.querySelector("#co-ss-status"), null);
});

test("a refused save keeps the typed password and says the file still has the old one", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  let pane = steamPane(env);
  pane.querySelector("#co-ss-password").value = "newpass123";
  click(pane.querySelector("#co-ss-save"));
  let save = env.calls.find((call) => call.action === "serverSettingsSave");
  assert.equal(save.passwordMode, "set");

  env.window.dispatchCoEvent({
    type: "serverSettingsResult",
    action: "serverSettingsSave",
    success: false,
    errors: ["World mode free-roam needs a generated world policy file. Nothing was changed, so the file still matches the running server."]
  });
  pane = steamPane(env);
  assert.equal(pane.querySelector("#co-ss-password").value, "newpass123", "the retry must still set the password");
  assert.ok(pane.querySelector("#co-ss-status"), "the refusal must say what the file still holds");
  assert.match(steamHtml(env), /the file still has the password it had/);
  assert.match(steamHtml(env), /still in the field/);
  assert.match(steamHtml(env), /Nothing was saved/);

  env.calls.length = 0;
  click(pane.querySelector("#co-ss-save"));
  save = env.calls.find((call) => call.action === "serverSettingsSave");
  assert.equal(save.passwordMode, "set", "the second save still sets the password");
  assert.equal(save.password, "newpass123");
  assert.equal(JSON.stringify(env.state.serverSettings).includes("newpass123"), false, "password kept out of state");

  env.window.dispatchCoEvent(Object.assign(resultEvent(), { action: "serverSettingsSave" }));
  pane = steamPane(env);
  assert.equal(pane.querySelector("#co-ss-password").value, "", "only a save that landed clears the field");
  assert.equal(env.state.serverSettings.status.includes("still has the password"), false);
});

test("a saved password is cleared once the file took it", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  let pane = steamPane(env);
  pane.querySelector("#co-ss-password").value = "newpass123";
  click(pane.querySelector("#co-ss-save"));
  env.window.dispatchCoEvent(Object.assign(resultEvent(), {
    action: "serverSettingsSave",
    data: Object.assign({}, resultEvent().data, { liveApplied: true, liveResult: { applied: ["worldMode"] } })
  }));
  pane = steamPane(env);
  assert.equal(pane.querySelector("#co-ss-password").value, "", "a stored password must not stay in the field");

  env.calls.length = 0;
  click(pane.querySelector("#co-ss-save"));
  const save = env.calls.find((call) => call.action === "serverSettingsSave");
  assert.equal(save.passwordMode, "keep");
  assert.equal(save.password, "");
});

test("a refused save keeps the clear-password toggle the host set", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  click(steamPane(env).querySelector("#co-ss-password-clear"));
  env.calls.length = 0;
  click(steamPane(env).querySelector("#co-ss-save"));
  assert.equal(env.calls.find((call) => call.action === "serverSettingsSave").passwordMode, "clear");

  env.window.dispatchCoEvent({ type: "serverSettingsResult", action: "serverSettingsSave", success: false, errors: ["refused"] });
  assert.equal(env.state.serverSettings.passwordClear, true, "the host's clear request must survive the refusal");
  assert.match(env.state.serverSettings.status, /file still has the saved password/);
  assert.match(env.state.serverSettings.status, /remove-password request is still on/);
  env.calls.length = 0;
  click(steamPane(env).querySelector("#co-ss-save"));
  assert.equal(env.calls.find((call) => call.action === "serverSettingsSave").passwordMode, "clear");
});

test("a config that cannot be read is shown as the server's error, not as blank settings", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  env.window.dispatchCoEvent(Object.assign(resultEvent(), {
    data: {
      serverDirectory: "C:/Games/CO",
      configPath: "C:/Games/CO/commonwealth-server.json",
      readable: false,
      errors: ["Server configuration is not valid JSON: [json.exception.parse_error.101] unexpected end of input"],
      limits: { maximumPlayers: 16, worldModes: ["coop-story", "free-roam"] }
    }
  }));
  const html = steamHtml(env);
  assert.match(html, /not valid JSON/);
  assert.match(html, /could not be read/);
  assert.equal(steamPane(env).querySelector("#co-ss-name").value, "", "no default values may be shown as the file's");
  assert.equal(html.includes("Loaded from the server folder"), false);

  env.calls.length = 0;
  click(steamPane(env).querySelector("#co-ss-save"));
  assert.equal(env.calls.filter((call) => call.action === "serverSettingsSave").length, 0, "nothing may be saved over an unreadable file");
  assert.match(steamHtml(env), /could not be read/);
  assert.match(steamHtml(env), /press Reload/, "the refusal must name the way out");
});

test("an unreadable folder can be left by saving another one", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  env.window.dispatchCoEvent(Object.assign(resultEvent(), {
    data: Object.assign({}, resultEvent().data, {
      readable: false,
      errors: ["Server configuration is not valid JSON: [json.exception.parse_error.101] unexpected end of input"]
    })
  }));
  let pane = steamPane(env);
  pane.querySelector("#co-ss-folder").value = "D:/GoodServer";
  pane.querySelector("#co-ss-folder").dispatchEvent({ type: "input", bubbles: true });
  env.calls.length = 0;
  click(pane.querySelector("#co-ss-save"));
  const read = env.calls.find((call) => call.action === "serverSettingsGet");
  assert.ok(read, "the folder typed into the panel must be read before anything is written to it");
  assert.equal(read.serverDirectory, "D:/GoodServer");
  assert.equal(env.calls.filter((call) => call.action === "serverSettingsSave").length, 0,
    "an unread folder cannot be saved to");
  assert.match(env.state.serverSettings.status, /Reading D:\/GoodServer before saving/);

  env.calls.length = 0;
  env.window.dispatchCoEvent(resultEvent({
    data: Object.assign({}, resultEvent().data, { serverDirectory: "D:/GoodServer", configPath: "D:/GoodServer/commonwealth-server.json" })
  }));
  assert.equal(env.state.serverSettings.readable, true);
  const save = env.calls.find((call) => call.action === "serverSettingsSave");
  assert.ok(save, "the folder that was just read must be written without a second Save press");
  assert.equal(save.serverDirectory, "D:/GoodServer");
  assert.equal(save.values.serverName, "Vault 111", "the values on screen are what gets written");
  assert.equal(env.state.serverSettings.status, "Saving...");
  assert.equal(steamPane(env).querySelector("#co-ss-name").value, "Vault 111");
});

test("edits made before the folder changed are the values that get written", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  let pane = steamPane(env);
  pane.querySelector("#co-ss-folder").value = "D:/GoodServer";
  pane.querySelector("#co-ss-folder").dispatchEvent({ type: "input", bubbles: true });
  pane.querySelector("#co-ss-name").value = "Edited Name";
  pane.querySelector("#co-ss-name").dispatchEvent({ type: "input", bubbles: true });
  env.calls.length = 0;
  click(pane.querySelector("#co-ss-save"));
  assert.ok(env.calls.find((call) => call.action === "serverSettingsGet" && call.serverDirectory === "D:/GoodServer"));

  env.calls.length = 0;
  env.window.dispatchCoEvent(resultEvent({
    data: Object.assign({}, resultEvent().data, {
      serverDirectory: "D:/GoodServer",
      configPath: "D:/GoodServer/commonwealth-server.json",
      values: Object.assign({}, resultEvent().data.values, { serverName: "Vault 111" })
    })
  }));
  const save = env.calls.find((call) => call.action === "serverSettingsSave");
  assert.ok(save, "the save must follow the read");
  assert.equal(save.serverDirectory, "D:/GoodServer");
  assert.equal(save.values.serverName, "Edited Name", "the value the host typed must survive the read");
});

test("the folder that was already read is not read again for a trailing separator", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  const pane = steamPane(env);
  pane.querySelector("#co-ss-folder").value = "C:/Games/CO/";
  pane.querySelector("#co-ss-folder").dispatchEvent({ type: "input", bubbles: true });
  env.calls.length = 0;
  click(pane.querySelector("#co-ss-save"));
  assert.equal(env.calls.filter((call) => call.action === "serverSettingsGet").length, 0,
    "the same folder is not read twice");
  const save = env.calls.find((call) => call.action === "serverSettingsSave");
  assert.ok(save, "the save goes out straight away");
  assert.equal(save.serverDirectory, "C:/Games/CO/");
});

test("a folder the bridge rejects is not written and the panel keeps the folder it had read", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  const pane = steamPane(env);
  pane.querySelector("#co-ss-folder").value = "Z:/Nope";
  pane.querySelector("#co-ss-folder").dispatchEvent({ type: "input", bubbles: true });
  env.calls.length = 0;
  click(pane.querySelector("#co-ss-save"));
  assert.ok(env.calls.find((call) => call.action === "serverSettingsGet"));

  env.calls.length = 0;
  env.window.dispatchCoEvent(resultEvent({
    data: Object.assign({}, resultEvent().data, {
      serverDirectory: "",
      configPath: "",
      errors: ["That server folder does not exist."]
    })
  }));
  assert.equal(env.calls.filter((call) => call.action === "serverSettingsSave").length, 0,
    "nothing may be written into a folder the bridge refused");
  assert.equal(env.state.serverSettings.folder, "C:/Games/CO", "the panel keeps the folder it had read");
  assert.equal(env.state.serverSettings.values.serverName, "Vault 111", "the read folder's values are not replaced by defaults");
  assert.match(steamHtml(env), /does not exist/);
});

test("a folder Save cannot read names the error beside the folder field", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  let pane = steamPane(env);
  pane.querySelector("#co-ss-folder").value = "Z:/Nope";
  pane.querySelector("#co-ss-folder").dispatchEvent({ type: "input", bubbles: true });
  click(pane.querySelector("#co-ss-save"));
  assert.equal(steamPane(env).querySelector("#co-ss-folder-error"), null, "nothing is claimed before the read answers");

  env.window.dispatchCoEvent(resultEvent({
    data: Object.assign({}, resultEvent().data, {
      serverDirectory: "",
      configPath: "",
      errors: ["That server folder does not exist."]
    })
  }));
  pane = steamPane(env);
  const note = pane.querySelector("#co-ss-folder-error");
  assert.ok(note, "the read error belongs beside the folder that caused it");
  assert.equal(note.parentNode.getAttribute("data-ss-field"), "co-ss-folder", "in the folder's own row");
  assert.match(steamHtml(env), /That server folder does not exist\./);
  assert.equal(pane.querySelector("#co-ss-errors"), null, "the same sentence is not printed twice");

  pane.querySelector("#co-ss-folder").value = "D:/Other";
  pane.querySelector("#co-ss-folder").dispatchEvent({ type: "input", bubbles: true });
  assert.equal(steamPane(env).querySelector("#co-ss-folder-error"), null, "typing another folder drops the old error");
});

test("a typed folder whose config cannot be parsed names it beside the folder field", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  const pane = steamPane(env);
  pane.querySelector("#co-ss-folder").value = "D:/Broken";
  pane.querySelector("#co-ss-folder").dispatchEvent({ type: "input", bubbles: true });
  click(pane.querySelector("#co-ss-save"));

  env.window.dispatchCoEvent(resultEvent({
    data: Object.assign({}, resultEvent().data, {
      serverDirectory: "D:/Broken",
      configPath: "D:/Broken/commonwealth-server.json",
      readable: false,
      errors: ["Server configuration is not valid JSON: [json.exception.parse_error.101] unexpected end of input"]
    })
  }));
  const note = steamPane(env).querySelector("#co-ss-folder-error");
  assert.ok(note, "an unreadable config is the folder's error too");
  assert.equal(note.parentNode.getAttribute("data-ss-field"), "co-ss-folder");
  assert.match(steamHtml(env), /not valid JSON/);
  assert.equal(env.state.serverSettings.readable, false);
  assert.equal(steamPane(env).querySelector("#co-ss-errors"), null, "the parse error is not listed twice");
});

test("a config that cannot be read refuses hosting for the real reason", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent({ type: "capabilities", steamP2p: true });
  env.browser.render();
  env.window.dispatchCoEvent(resultEvent());
  env.window.dispatchCoEvent(Object.assign(resultEvent(), {
    data: Object.assign({}, resultEvent().data, {
      readable: false,
      errors: ["Server configuration is not valid JSON: [json.exception.parse_error.101] unexpected end of input"]
    })
  }));
  env.calls.length = 0;
  click(steamPane(env).querySelector("#co-steam-host"));
  assert.equal(env.calls.filter((call) => call.action === "steamHostStart").length, 0, "a broken config cannot host");
  const reason = env.state.steamHost.reason;
  assert.match(reason, /could not be read/);
  assert.match(reason, /not valid JSON/);
  assert.match(reason, /parse_error\.101/, "the refusal must quote the error the bridge reported");
  assert.match(reason, /press Reload/);
  assert.equal(reason.includes("Set your server name"), false, "the missing name is not the problem");
  assert.match(steamHtml(env), /could not be read/, "the host card shows the reason");
});

test("a saved validation error refuses hosting and quotes the server's own text", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent({ type: "capabilities", steamP2p: true });
  env.browser.render();
  env.window.dispatchCoEvent(resultEvent({
    data: Object.assign({}, resultEvent().data, { errors: ["server_name must be <= 64 characters"] })
  }));
  env.calls.length = 0;
  click(steamPane(env).querySelector("#co-steam-host"));
  assert.equal(env.calls.filter((call) => call.action === "steamHostStart").length, 0,
    "the server would refuse this config at start");
  const reason = env.state.steamHost.reason;
  assert.match(reason, /server_name must be <= 64 characters/);
  assert.equal(reason.includes("Set your server name"), false, "the name is present; the file is the problem");
  assert.match(reason, /press Reload/);
  assert.match(steamHtml(env), /server_name must be/);
});

test("a readable config without a name still asks for a name", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent({ type: "capabilities", steamP2p: true });
  env.browser.render();
  env.window.dispatchCoEvent(resultEvent({
    data: Object.assign({}, resultEvent().data, {
      values: Object.assign({}, resultEvent().data.values, { serverName: "" })
    })
  }));
  env.calls.length = 0;
  click(steamPane(env).querySelector("#co-steam-host"));
  assert.equal(env.calls.filter((call) => call.action === "steamHostStart").length, 0);
  assert.match(env.state.steamHost.reason, /Set your server name in Server settings/);
});

test("a saved world mode the data file cannot serve refuses hosting without a server", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent({ type: "capabilities", steamP2p: true });
  env.browser.render();
  const refusal = "World mode free-roam needs a generated world policy file with story or companion entries; " +
    "C:/Games/CO/world-policy.json is missing, invalid or the placeholder sample. " +
    "Generate the real data file (docs/protocol/world-policy.md) or set worldMode to coop-story.";
  env.window.dispatchCoEvent(resultEvent({
    data: Object.assign({}, resultEvent().data, { worldModeRefusal: refusal })
  }));
  assert.equal(env.state.serverSettings.worldModeRefusal, refusal);
  env.calls.length = 0;
  click(steamPane(env).querySelector("#co-steam-host"));
  assert.equal(env.calls.filter((call) => call.action === "steamHostStart").length, 0,
    "no server is started for a mode it cannot serve");
  assert.ok(env.state.steamHost.reason.includes(refusal), "the host is told the server's own reason");
  assert.match(env.state.steamHost.reason, /Set World mode in Server settings below/);
  assert.equal(env.state.steamHost.reason.includes("Set your server name"), false, "the name is not the problem");
  assert.match(steamHtml(env), /needs a generated world policy file/, "the host card shows the reason");
});

test("the server settings card shows the world-mode refusal without pressing Host", () => {
  const env = settingsEnv();
  const refusal = "World mode free-roam needs a generated world policy file with story or companion entries; " +
    "C:/Games/CO/world-policy.json is missing, invalid or the placeholder sample.";
  env.window.dispatchCoEvent(resultEvent({
    data: Object.assign({}, resultEvent().data, { worldModeRefusal: refusal })
  }));
  let note = steamPane(env).querySelector("#co-ss-world-mode-refusal");
  assert.ok(note, "the card must say it without a Host press");
  assert.match(steamHtml(env), /needs a generated world policy file/);
  assert.equal((env.state.steamHost || {}).reason || "", "", "nothing was hosted to see the refusal");

  env.window.dispatchCoEvent(resultEvent({
    data: Object.assign({}, resultEvent().data, { worldModeRefusal: "<img src=x> free-roam needs a policy file" })
  }));
  const html = steamHtml(env);
  assert.equal(html.includes("<img"), false, "the server's text is escaped in the card");
  assert.match(html, /&lt;img src=x&gt; free-roam needs a policy file/);

  env.window.dispatchCoEvent(resultEvent());
  note = steamPane(env).querySelector("#co-ss-world-mode-refusal");
  assert.equal(note, null, "a served file clears the card's refusal");
});

test("a served world mode releases the refusal once the file says so", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent({
    data: Object.assign({}, resultEvent().data, { worldModeRefusal: "World mode free-roam needs a generated world policy file." })
  }));
  assert.match(env.window.CO_ServerSettings.hostBlockReason(env.state), /needs a generated world policy file/);
  env.window.dispatchCoEvent(resultEvent());
  assert.equal(env.state.serverSettings.worldModeRefusal, "");
  assert.equal(env.window.CO_ServerSettings.hostBlockReason(env.state), "");
});

test("hosting before the settings arrive asks the host to wait", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent({ type: "capabilities", steamP2p: true });
  env.browser.render();
  click(steamPane(env).querySelector("#co-steam-host"));
  assert.equal(env.calls.filter((call) => call.action === "steamHostStart").length, 0);
  assert.match(env.state.steamHost.reason, /Reading the server settings/);
  assert.equal(env.calls.filter((call) => call.action === "serverSettingsGet").length, 1, "one request while the settings are in flight");
});

test("a world mode the running server refuses is shown and nothing claims a change", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  env.window.dispatchCoEvent({
    type: "serverSettingsResult",
    action: "serverSettingsSave",
    success: false,
    errors: [
      "World mode free-roam needs a generated world policy file with story or companion entries; " +
      "C:/Games/CO/world-policy.json is missing, invalid or the placeholder sample. " +
      "Nothing was changed, so the file still matches the running server."
    ]
  });
  const html = steamHtml(env);
  assert.match(html, /needs a generated world policy file/);
  assert.match(html, /Nothing was changed/);
  assert.equal(html.includes("Applied to the running server"), false);
  assert.equal(html.includes("Restart the server"), false);
  assert.match(html, /<option value="free-roam" selected>free-roam<\/option>/, "the panel still shows what the file says");
});

test("the advertised identity comes from the loaded server settings", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  const identity = env.window.CO_ServerSettings.advertised(env.state);
  assert.equal(identity.name, "Vault 111");
  assert.equal(identity.maxPlayers, 8);
  env.state.serverSettings.values.serverName = "  Behind the Gate  ";
  env.state.serverSettings.values.maxPlayers = 99;
  const clamped = env.window.CO_ServerSettings.advertised(env.state);
  assert.equal(clamped.name, "Behind the Gate");
  assert.equal(clamped.maxPlayers, 16);
});

test("server validation errors come back from the bridge and render escaped", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent({
    type: "serverSettingsResult",
    action: "serverSettingsSave",
    success: false,
    errors: ["downed_seconds must be 5-600", "<img src=x onerror=alert(1)>"]
  });
  const html = steamHtml(env);
  assert.match(html, /downed_seconds must be 5-600/);
  assert.equal(html.includes("<img"), false);
  assert.match(html, /&lt;img src=x onerror=alert\(1\)&gt;/);
});

test("a rejected save keeps every edited value on screen", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  const pane = steamPane(env);
  const name = pane.querySelector("#co-ss-name");
  name.value = "Edited";
  name.dispatchEvent({ type: "input", bubbles: true });
  click(pane.querySelector("#co-ss-save"));
  env.window.dispatchCoEvent({
    type: "serverSettingsResult",
    action: "serverSettingsSave",
    success: false,
    errors: ["server_name must be <= 64 characters"]
  });
  assert.equal(steamPane(env).querySelector("#co-ss-name").value, "Edited");
});

test("a locked Steam P2P panel shows the listing off, disabled and explains why", () => {
  const env = settingsEnv();
  const payload = resultEvent();
  payload.data.publicListingLocked = true;
  payload.data.values.publicListing = false;
  env.window.dispatchCoEvent(payload);
  const toggle = steamPane(env).querySelector("#co-ss-listing");
  assert.equal(toggle.getAttribute("aria-pressed"), "false");
  assert.notEqual(toggle.getAttribute("disabled"), null);
  assert.match(steamHtml(env), /Steam P2P hosts are never listed\./);
  assert.equal(env.state.serverSettings.listingLocked, true);

  env.window.CO_ServerSettings.activate(env.mount, "co-ss-listing", env.state);
  assert.equal(env.state.serverSettings.draft.publicListing, undefined);
  assert.equal(steamPane(env).querySelector("#co-ss-listing").getAttribute("aria-pressed"), "false");
});

test("an unlocked panel lets the listing toggle change and sends the new value", () => {
  const env = settingsEnv();
  env.window.dispatchCoEvent(resultEvent());
  assert.equal(env.state.serverSettings.listingLocked, false);
  const toggle = steamPane(env).querySelector("#co-ss-listing");
  assert.equal(toggle.getAttribute("disabled"), null);
  env.window.CO_ServerSettings.activate(env.mount, "co-ss-listing", env.state);
  assert.equal(env.state.serverSettings.draft.publicListing, false);
  env.calls.length = 0;
  click(steamPane(env).querySelector("#co-ss-save"));
  const save = env.calls.find((call) => call.action === "serverSettingsSave");
  assert.ok(save, "a save was sent");
  assert.equal(save.values.publicListing, false);
});

test("a locked panel still sends the panel values when another setting is saved", () => {
  const env = settingsEnv();
  const payload = resultEvent();
  payload.data.publicListingLocked = true;
  payload.data.values.publicListing = false;
  env.window.dispatchCoEvent(payload);
  const name = steamPane(env).querySelector("#co-ss-name");
  name.value = "Renamed Host";
  name.dispatchEvent({ type: "input", bubbles: true });
  env.calls.length = 0;
  click(steamPane(env).querySelector("#co-ss-save"));
  const save = env.calls.find((call) => call.action === "serverSettingsSave");
  assert.ok(save, "a save was sent");
  assert.equal(save.values.serverName, "Renamed Host");
  assert.equal(save.values.publicListing, false);
});