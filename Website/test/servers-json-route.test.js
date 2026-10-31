"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const dbPath = require.resolve("../src/db");
require.cache[dbPath] = { exports: { initDb: () => null, getDb: () => null } };
const config = require("../src/config");
const { createApp } = require("../src/app");

const entry = (overrides) => ({ host: "8.8.4.4", port: 7777, name: "Entry", ...overrides });

const fixture = {
  version: 1,
  updatedAt: null,
  servers: [
    entry({
      id: "valid-1",
      name: "Valid One",
      tags: ["x".repeat(65), ...Array.from({ length: 33 }, (_, i) => `tag-${i + 1}`)],
    }),
    entry({ id: "valid-2", name: "Valid Two", host: "8.8.4.8", port: 7778, description: "d".repeat(2049) }),
    entry({ id: "port-high", host: "8.8.4.5", port: 70000 }),
    entry({ id: "port-fraction", host: "8.8.4.6", port: 7777.5 }),
    entry({ id: "long-name", name: "n".repeat(200) }),
    entry({ id: "multibyte-name", name: "é".repeat(70) }),
  ],
};

const get = async (base, file, route) => {
  const original = config.paths.serversJson;
  config.paths.serversJson = file;
  try {
    const response = await fetch(`${base}${route}`);
    return { status: response.status, headers: response.headers, body: await response.json() };
  } finally {
    config.paths.serversJson = original;
  }
};

(async () => {
  const server = await new Promise((resolve) => {
    const instance = createApp().listen(0, "127.0.0.1", () => resolve(instance));
  });
  const base = `http://127.0.0.1:${server.address().port}`;
  const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "co-servers-json-"));
  const write = (name, content) => {
    const file = path.join(tempDir, name);
    fs.writeFileSync(file, content);
    return file;
  };

  try {
    const curatedFile = write("server.json", JSON.stringify(fixture));
    const page = await get(base, curatedFile, "/servers/server.json");
    assert.equal(page.status, 200);
    assert.match(page.headers.get("content-type"), /application\/json/);
    assert.equal(page.headers.get("cache-control"), "public, max-age=60");
    assert.deepEqual(
      page.body.servers.map((server) => server.id),
      ["valid-1", "valid-2"],
      "entries the client would reject are dropped"
    );
    const [first, second] = page.body.servers;
    assert.equal(first.tags.length, 32, "tags are capped at 32");
    assert.ok(first.tags.every((tag) => tag.length <= 64), "over-long tags are dropped");
    assert.equal(second.description, "", "over-long description is cleared");
    for (const server of page.body.servers) {
      assert.ok(server.host.length > 0, "entry has a host");
      assert.ok(server.name.length > 0, "entry has a name");
      assert.ok(Number.isInteger(server.port) && server.port > 0, "entry has a port");
    }

    const { count, ...shared } = (await get(base, curatedFile, "/api/v1/servers")).body;
    assert.equal(count, page.body.servers.length);
    assert.deepEqual(page.body, shared, "serves the same normalised payload as /api/v1/servers");

    const bulkFile = write(
      "bulk.json",
      JSON.stringify({ servers: Array.from({ length: 513 }, (_, i) => entry({ id: `bulk-${i}`, name: "Bulk" })) })
    );
    assert.equal((await get(base, bulkFile, "/servers/server.json")).body.servers.length, 512, "directory is capped at 512 entries");

    const missing = await get(base, path.join(tempDir, "missing.json"), "/servers/server.json");
    assert.equal(missing.status, 200);
    assert.deepEqual(missing.body, { version: 1, updatedAt: null, servers: [] });

    const empty = await get(base, write("empty.json", ""), "/servers/server.json");
    assert.equal(empty.status, 200);
    assert.deepEqual(empty.body.servers, []);

    console.log("servers json route tests passed");
  } finally {
    server.close();
    fs.rmSync(tempDir, { recursive: true, force: true });
  }
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
