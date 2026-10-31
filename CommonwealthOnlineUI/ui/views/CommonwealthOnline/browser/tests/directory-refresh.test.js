const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

function loadRefresh() {
  const sandbox = { console, Promise };
  sandbox.window = sandbox;
  vm.runInContext(
    fs.readFileSync(path.join(__dirname, "..", "components", "directory-refresh.js"), "utf8"),
    vm.createContext(sandbox)
  );
  return sandbox.CO_DirectoryRefresh;
}

const live = "https://live.example/directory";
const curated = "https://static.example/servers/server.json";
const official = { name: "Commonwealth Online Server One", host: "servers.commonwealth-online.com", port: 7777 };

test("an empty live directory falls through to the next directory", async () => {
  const refresh = loadRefresh();
  const requested = [];
  const result = await refresh.query([live, curated], (url) => {
    requested.push(url);
    if (url === live) return Promise.resolve({ servers: [] });
    return Promise.resolve({ servers: [official] });
  });
  assert.deepEqual(requested, [live, curated]);
  assert.equal(result.success, true);
  assert.equal(result.connectionStatus, "Online");
  assert.equal(result.directoryNotice, "");
  assert.deepEqual(result.servers, [official]);
});

test("every failed directory reports the Direct Connect message", async () => {
  const refresh = loadRefresh();
  const requested = [];
  const result = await refresh.query([live, curated], (url) => {
    requested.push(url);
    return Promise.reject(new Error("HTTP 404"));
  });
  assert.deepEqual(requested, [live, curated]);
  assert.equal(result.success, false);
  assert.equal(result.servers.length, 0);
  assert.equal(result.connectionStatus, "Directory unavailable");
  assert.equal(result.directoryNotice, refresh.directoryUnavailableMessage);
});

test("a non-empty directory stops before the next URL", async () => {
  const refresh = loadRefresh();
  const requested = [];
  const liveServer = { name: "Live", host: "203.0.113.10", port: 7777 };
  const result = await refresh.query([live, curated], (url) => {
    requested.push(url);
    return Promise.resolve({ servers: [liveServer] });
  });
  assert.deepEqual(requested, [live]);
  assert.equal(result.success, true);
  assert.deepEqual(result.servers, [liveServer]);
});
