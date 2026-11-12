const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

function loadComponents(names) {
  const sandbox = { console };
  sandbox.window = sandbox;
  const context = vm.createContext(sandbox);
  for (const name of names) {
    vm.runInContext(
      fs.readFileSync(path.join(__dirname, "..", "components", name + ".js"), "utf8"),
      context
    );
  }
  return sandbox;
}

const names = (list) => list.map((s) => s.name).join(",");

function shownAt(servers, ping) {
  const { CO_FilterState } = loadComponents(["server-records", "filter-state"]);
  const state = {
    allServers: servers,
    filters: Object.assign(CO_FilterState.defaults(), { ping: ping }),
    regionPreference: "Auto",
    favorites: []
  };
  CO_FilterState.apply(state);
  return names(state.servers);
}

test("a server without a measured ping is normalized as unknown", () => {
  const { CO_ServerRecords } = loadComponents(["server-records"]);
  assert.equal(CO_ServerRecords.normalizeDirectoryServer({ host: "10.0.0.3" }).ping, null);
  assert.equal(CO_ServerRecords.normalizeDirectoryServer({ host: "10.0.0.4", ping: 0 }).ping, null);
  assert.equal(CO_ServerRecords.normalizeDirectoryServer({ host: "10.0.0.5", ping: 42 }).ping, 42);
});

test("unknown ping fails every ping threshold and measured ping is compared to the limit", () => {
  const { CO_ServerRecords } = loadComponents(["server-records"]);
  const servers = [
    { name: "measured-40", host: "10.0.0.1", ping: 40 },
    { name: "measured-120", host: "10.0.0.2", ping: 120 },
    { name: "unknown", host: "10.0.0.3" },
    { name: "zero", host: "10.0.0.4", ping: 0 }
  ].map((raw) => CO_ServerRecords.normalizeDirectoryServer(raw));

  assert.equal(shownAt(servers, "Any"), "measured-40,measured-120,unknown,zero");
  assert.equal(shownAt(servers, "< 50 ms"), "measured-40");
  assert.equal(shownAt(servers, "< 100 ms"), "measured-40");
  assert.equal(shownAt(servers, "< 150 ms"), "measured-40,measured-120");
});

test("the probe time stands in for a missing directory ping only while the server answers", () => {
  const { CO_ServerRecords } = loadComponents(["server-records"]);
  const probed = CO_ServerRecords.normalizeDirectoryServer({ host: "10.0.0.6", reachability: "answering", reachabilityMs: 42 });
  assert.equal(CO_ServerRecords.effectivePing(probed), 42);
  const directory = CO_ServerRecords.normalizeDirectoryServer({ host: "10.0.0.7", ping: 30, reachability: "answering", reachabilityMs: 42 });
  assert.equal(CO_ServerRecords.effectivePing(directory), 30);
  const failed = CO_ServerRecords.normalizeDirectoryServer({ host: "10.0.0.8", reachability: "no_answer", reachabilityMs: 42 });
  assert.equal(CO_ServerRecords.effectivePing(failed), null);
});

test("a probed server is filtered by its probe time", () => {
  const { CO_ServerRecords } = loadComponents(["server-records"]);
  const servers = [
    { name: "probed-42", host: "10.0.0.9", reachability: "answering", reachabilityMs: 42 },
    { name: "unprobed", host: "10.0.0.10" }
  ].map((raw) => CO_ServerRecords.normalizeDirectoryServer(raw));

  assert.equal(shownAt(servers, "< 50 ms"), "probed-42");
  assert.equal(shownAt(servers, "< 100 ms"), "probed-42");
});

test("a reachability update that measures a server makes it pass the threshold", () => {
  const { CO_ServerRecords, CO_FilterState, CO_ServerList } = loadComponents(["server-records", "filter-state", "server-list"]);
  const server = CO_ServerRecords.normalizeDirectoryServer({ host: "10.0.0.11", name: "later" });
  const state = {
    allServers: [server],
    filters: Object.assign(CO_FilterState.defaults(), { ping: "< 50 ms" }),
    regionPreference: "Auto",
    favorites: []
  };
  CO_FilterState.apply(state);
  assert.equal(names(state.servers), "");

  const changed = CO_ServerList.applyReachabilityUpdates({ querySelector: () => null }, state, {
    updates: [{ host: "10.0.0.11", port: 7777, state: "answering", milliseconds: 42 }]
  });
  assert.equal(changed, true);
  CO_FilterState.apply(state);
  assert.equal(names(state.servers), "later");
});
