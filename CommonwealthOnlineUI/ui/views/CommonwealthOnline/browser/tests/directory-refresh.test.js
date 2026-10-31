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

test("an empty live directory still merges the website list", async () => {
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
  assert.equal(result.servers.length, 1);
  assert.equal(result.servers[0].host, official.host);
  assert.equal(result.servers[0].live, false);
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

test("a non-empty directory does not stop the next directory", async () => {
  const refresh = loadRefresh();
  const requested = [];
  const liveServer = { name: "Live", host: "203.0.113.10", port: 7777 };
  const result = await refresh.query([live, curated], (url) => {
    requested.push(url);
    if (url === live) {
      return Promise.resolve({ heartbeatIntervalSeconds: 30, ttlSeconds: 90, servers: [liveServer] });
    }
    return Promise.resolve({ servers: [official] });
  });
  assert.deepEqual(requested, [live, curated]);
  assert.equal(result.success, true);
  assert.equal(result.servers.length, 2);
  assert.equal(result.servers[0].host, liveServer.host);
  assert.equal(result.servers[1].host, official.host);
  assert.equal(result.servers[0].live, true);
  assert.equal(result.servers[1].live, false);
});

test("a duplicate host and port keeps the live entry with its player count", async () => {
  const refresh = loadRefresh();
  const result = await refresh.query([live, curated], (url) => {
    if (url === live) {
      return Promise.resolve({
        heartbeatIntervalSeconds: 30,
        ttlSeconds: 90,
        servers: [{ name: "Live Duplicate", host: official.host.toUpperCase(), port: official.port, players: 4 }]
      });
    }
    return Promise.resolve({ servers: [official] });
  });
  assert.equal(result.servers.length, 1);
  assert.equal(result.servers[0].live, true);
  assert.equal(result.servers[0].players, 4);
});

test("a duplicate keeps the live entry and fills in the curated wording", async () => {
  const refresh = loadRefresh();
  const curatedEntry = Object.assign({}, official, {
    description: "Official dedicated server",
    region: "Oceania"
  });
  const result = await refresh.query([live, curated], (url) => {
    if (url === live) {
      return Promise.resolve({
        heartbeatIntervalSeconds: 30,
        ttlSeconds: 90,
        servers: [{ name: "Live Duplicate", host: official.host.toUpperCase(), port: official.port, players: 4 }]
      });
    }
    return Promise.resolve({ servers: [curatedEntry] });
  });
  assert.equal(result.servers.length, 1);
  assert.equal(result.servers[0].live, true);
  assert.equal(result.servers[0].players, 4);
  assert.equal(result.servers[0].description, "Official dedicated server");
  assert.equal(result.servers[0].region, "Oceania");
});

test("the merged list is capped at 512 entries", async () => {
  const refresh = loadRefresh();
  const makeList = (start) => {
    const servers = [];
    for (let offset = 0; offset < 400; offset += 1) {
      const index = start + offset;
      servers.push({ name: "Server " + index, host: "198.51." + Math.floor(index / 256) + "." + (index % 256), port: 7000 + (index % 500) });
    }
    return servers;
  };
  const result = await refresh.query([live, curated], (url) => Promise.resolve({ servers: url === live ? makeList(0) : makeList(1000) }));
  assert.equal(result.success, true);
  assert.equal(result.servers.length, 512);
  assert.equal(refresh.maxServers, 512);
});

test("an array payload has no live heartbeat signal", () => {
  const refresh = loadRefresh();
  assert.equal(refresh.isLivePayload([official]), false);
  assert.equal(refresh.isLivePayload({ servers: [official] }), false);
  assert.equal(refresh.isLivePayload({ heartbeatIntervalSeconds: 30, servers: [official] }), true);
  assert.equal(refresh.isLivePayload({ ttlSeconds: 90, servers: [official] }), true);
});

function liveList(count, start) {
  const servers = [];
  for (let offset = 0; offset < count; offset += 1) {
    const index = (start || 0) + offset;
    servers.push({ name: "Live " + index, host: "203.0." + Math.floor(index / 256) + "." + (index % 256), port: 7777 });
  }
  return servers;
}

test("the two website entries survive a full live directory", async () => {
  const refresh = loadRefresh();
  const website = [
    { name: "Commonwealth Online Official USA", host: "195.26.244.234", port: 7777 },
    { name: "Commonwealth Online USA 2", host: "195.26.244.234", port: 7787 }
  ];
  const result = await refresh.query([live, curated], (url) => {
    if (url === live) {
      return Promise.resolve({ heartbeatIntervalSeconds: 30, ttlSeconds: 90, servers: liveList(512, 0) });
    }
    return Promise.resolve({ servers: website });
  });
  assert.equal(result.success, true);
  assert.equal(result.servers.length, 512);
  const hosts = result.servers.map((server) => server.host + ":" + server.port);
  for (const entry of website) {
    assert.equal(hosts.includes(entry.host + ":" + entry.port), true, "missing website entry " + entry.host + ":" + entry.port);
  }
  assert.equal(result.servers.filter((server) => server.live === true).length, 510);
});

test("a website duplicate of a live entry does not take a second slot", async () => {
  const refresh = loadRefresh();
  const liveEntries = liveList(512, 0);
  const duplicate = Object.assign({}, liveEntries[0], { name: "Official Duplicate" });
  const extra = { name: "Commonwealth Online Official USA", host: "195.26.244.234", port: 7777 };
  const result = await refresh.query([live, curated], (url) => {
    if (url === live) {
      return Promise.resolve({ heartbeatIntervalSeconds: 30, ttlSeconds: 90, servers: liveEntries });
    }
    return Promise.resolve({ servers: [duplicate, extra] });
  });
  assert.equal(result.servers.length, 512);
  const hosts = result.servers.map((server) => server.host + ":" + server.port);
  assert.equal(hosts.includes(liveEntries[0].host + ":" + liveEntries[0].port), true);
  assert.equal(hosts.includes(extra.host + ":" + extra.port), true);
});

test("a huge website list cannot crowd out the live list", async () => {
  const refresh = loadRefresh();
  const website = liveList(100, 5000).map((server, index) => Object.assign({}, server, { name: "Website " + index }));
  const result = await refresh.query([live, curated], (url) => {
    if (url === live) {
      return Promise.resolve({ heartbeatIntervalSeconds: 30, ttlSeconds: 90, servers: liveList(512, 0) });
    }
    return Promise.resolve({ servers: website });
  });
  assert.equal(result.servers.length, 512);
  assert.equal(result.servers.filter((server) => server.live === false).length, refresh.maxReservedWebsiteServers);
  assert.equal(result.servers.filter((server) => server.live === true).length, 512 - refresh.maxReservedWebsiteServers);
});
