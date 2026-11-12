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

test("a saved-only favorite stays in Favorites but is left out of the main list", () => {
  const { CO_ServerRecords, CO_FilterState } = loadComponents(["server-records", "filter-state"]);
  const listed = CO_ServerRecords.normalizeDirectoryServer({ id: "listed", host: "10.0.1.1", name: "listed", favorite: true });
  const saved = CO_ServerRecords.normalizeDirectoryServer({ id: "gone", host: "10.0.1.2", name: "gone", favorite: true, savedOnly: true });
  assert.equal(listed.savedOnly, false);
  assert.equal(saved.savedOnly, true);

  const state = {
    allServers: [listed, saved],
    filters: CO_FilterState.defaults(),
    regionPreference: "Auto"
  };
  CO_FilterState.apply(state);
  assert.equal(names(state.servers), "listed");
  assert.equal(names(state.favorites), "listed,gone");
});

test("a saved-only favorite is labelled Offline (saved) on its row", () => {
  const { CO_ServerRecords, CO_ServerList } = loadComponents(["server-records", "server-list"]);
  const saved = { host: "10.0.1.2", reachability: "unchecked", reachabilityReason: "Not in the latest server directory refresh", savedOnly: true };
  assert.equal(CO_ServerList.reachabilityDescriptor(saved).label, "Offline (saved)");
  assert.equal(CO_ServerList.reachabilityDescriptor({ host: "10.0.1.3", reachability: "unchecked" }).label, "NOT CHECKED");

  const list = { innerHTML: "", scrollHeight: 0, clientHeight: 0 };
  const wrap = { className: "", querySelector: () => null };
  const root = {
    querySelector: (selector) => (selector === ".co-server-list-wrap" ? wrap : selector === ".co-server-list" ? list : null)
  };
  CO_ServerList.render(root, {
    servers: [CO_ServerRecords.normalizeDirectoryServer({ host: "10.0.1.2", name: "gone", savedOnly: true })],
    activeTab: "favorites",
    selectedServerIndex: 0,
    focusZone: ""
  });
  assert.match(list.innerHTML, /Offline \(saved\)/);
});
