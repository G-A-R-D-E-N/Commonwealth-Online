const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

function loadFilterState() {
  const sandbox = { console };
  sandbox.window = sandbox;
  const context = vm.createContext(sandbox);
  vm.runInContext(
    fs.readFileSync(path.join(__dirname, "..", "components", "filter-state.js"), "utf8"),
    context
  );
  return sandbox.CO_FilterState;
}

const servers = [
  { name: "eu-a", region: "Europe" },
  { name: "unset", region: "" },
  { name: "na", region: "North America" },
  { name: "eu-b", region: "Europe" },
  { name: "local", region: "Local" },
  { name: "asia", region: "Asia" },
];

const names = (list) => list.map((s) => s.name).join(",");

test("Auto keeps the directory order", () => {
  const { sortByRegion } = loadFilterState();
  assert.equal(names(sortByRegion(servers, "Auto")), "eu-a,unset,na,eu-b,local,asia");
  assert.equal(names(sortByRegion(servers, undefined)), "eu-a,unset,na,eu-b,local,asia");
});

test("matching region first, unknown second, other regions last, stable", () => {
  const { sortByRegion } = loadFilterState();
  assert.equal(names(sortByRegion(servers, "Europe")), "eu-a,eu-b,unset,local,na,asia");
  assert.equal(names(sortByRegion(servers, "Asia")), "asia,unset,local,eu-a,na,eu-b");
});

test("sortByRegion does not modify its input", () => {
  const { sortByRegion } = loadFilterState();
  const input = servers.slice();
  sortByRegion(input, "Asia");
  assert.equal(names(input), names(servers));
});

test("apply orders the shown list by preference without filtering by region", () => {
  const { apply, defaults } = loadFilterState();
  const state = { allServers: servers, filters: defaults(), regionPreference: "Asia", favorites: [] };
  apply(state);
  assert.equal(names(state.servers), "asia,unset,local,eu-a,na,eu-b");
});

test("an explicit region filter still narrows the list", () => {
  const { apply, defaults } = loadFilterState();
  const state = {
    allServers: servers,
    filters: Object.assign(defaults(), { region: "Europe" }),
    regionPreference: "Asia",
    favorites: [],
  };
  apply(state);
  assert.equal(names(state.servers), "eu-a,eu-b");
});
