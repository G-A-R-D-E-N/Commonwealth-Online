"use strict";

const assert = require("node:assert/strict");
const express = require("express");
const { rateLimit, clientIp } = require("../src/middleware/rate-limit");

const listen = (app) =>
  new Promise((resolve) => {
    const server = app.listen(0, "127.0.0.1", () => resolve(server));
  });

const buildApp = (trustProxy) => {
  const app = express();
  if (trustProxy) {
    app.set("trust proxy", 1);
  }
  app.get("/ip", (req, res) => res.json({ ip: clientIp(req) }));
  app.post("/limited", rateLimit({ windowMs: 60_000, max: 2 }), (req, res) => res.json({ ok: true }));
  app.get("/items/:publicId", rateLimit({ windowMs: 60_000, max: 2 }), (req, res) => res.json({ ok: true }));
  return app;
};

const invokeLimiter = (limiter, ip, path) => {
  let status = 200;
  const res = {
    set() {},
    status(code) {
      status = code;
      return this;
    },
    json() {
      return this;
    },
  };
  limiter({ method: "GET", baseUrl: "/items", path, route: { path: "/:publicId" }, ip }, res, () => {});
  return status;
};

const main = async () => {
  {
    const server = await listen(buildApp(false));
    const base = `http://127.0.0.1:${server.address().port}`;
    try {
      const ip = await (await fetch(`${base}/ip`, { headers: { "x-forwarded-for": "203.0.113.9" } })).json();
      assert.notEqual(ip.ip, "203.0.113.9", "X-Forwarded-For must be ignored without TRUST_PROXY");

      const statuses = [];
      for (let i = 0; i < 4; i += 1) {
        const res = await fetch(`${base}/limited`, { method: "POST", headers: { "x-forwarded-for": `198.51.100.${i}` } });
        statuses.push(res.status);
      }
      assert.deepEqual(statuses, [200, 200, 429, 429], "rotating X-Forwarded-For must not reset the limiter");
    } finally {
      server.close();
    }
  }

  {
    const server = await listen(buildApp(true));
    const base = `http://127.0.0.1:${server.address().port}`;
    try {
      const ip = await (await fetch(`${base}/ip`, { headers: { "x-forwarded-for": "203.0.113.9" } })).json();
      assert.equal(ip.ip, "203.0.113.9", "with TRUST_PROXY the proxy's X-Forwarded-For hop is used");

      const spoofed = await (await fetch(`${base}/ip`, { headers: { "x-forwarded-for": "10.0.0.1, 203.0.113.9" } })).json();
      assert.equal(spoofed.ip, "203.0.113.9", "a client-supplied left-most X-Forwarded-For entry must not be trusted");
    } finally {
      server.close();
    }
  }

  {
    const server = await listen(buildApp(true));
    const base = `http://127.0.0.1:${server.address().port}`;
    try {
      const statuses = [];
      for (const id of ["a", "b", "c"]) {
        const res = await fetch(`${base}/items/${id}`, { headers: { "x-forwarded-for": "198.51.100.40" } });
        statuses.push(res.status);
      }
      assert.deepEqual(statuses, [200, 200, 429], "varying :publicId values must share one bucket");
    } finally {
      server.close();
    }
  }

  {
    const limiter = rateLimit({ windowMs: 60_000, max: 2 });
    const sameSubnet = ["2001:db8:1:2:0:0:0:1", "2001:0DB8:0001:0002::2", "2001:db8:1:2::3"].map((ip) =>
      invokeLimiter(limiter, ip, "/items/v6"),
    );
    assert.deepEqual(sameSubnet, [200, 200, 429], "addresses inside one /64 must share a bucket");
    assert.equal(invokeLimiter(limiter, "2001:db8:1:3::1", "/items/v6"), 200, "a different /64 gets its own bucket");
    const mapped = ["::ffff:198.51.100.50", "198.51.100.50", "::FFFF:198.51.100.50"].map((ip) =>
      invokeLimiter(limiter, ip, "/items/v4"),
    );
    assert.deepEqual(mapped, [200, 200, 429], "an IPv4-mapped IPv6 address shares the IPv4 bucket");
    const hexMappedLimiter = rateLimit({ windowMs: 60_000, max: 2 });
    const hexMapped = ["::ffff:c633:6433", "::FFFF:C633:6433", "::ffff:198.51.100.51"].map((ip) =>
      invokeLimiter(hexMappedLimiter, ip, "/items/v4hex"),
    );
    assert.deepEqual(hexMapped, [200, 200, 429], "the hex form of an IPv4-mapped IPv6 address shares the IPv4 bucket");
    const spellingLimiter = rateLimit({ windowMs: 60_000, max: 2 });
    const spellings = [
      "0:0:0:0:0:ffff:198.51.100.52",
      "0000:0000:0000:0000:0000:FFFF:c633:6434",
      "::ffff:198.51.100.52%eth0",
      "198.51.100.52",
    ].map((ip) => invokeLimiter(spellingLimiter, ip, "/items/spellings"));
    assert.deepEqual(spellings, [200, 200, 429, 429], "every spelling of an IPv4-mapped IPv6 address shares the IPv4 bucket");
  }

  {
    const limiter = rateLimit({ windowMs: 60_000, max: 2 });
    const victim =["/items/b", "/items/b", "/items/b"].map((path) => invokeLimiter(limiter, "198.51.100.30", path));
    assert.deepEqual(victim, [200, 200, 429], "the victim must exhaust its bucket");
    for (let i = 0; i < 20_001; i += 1) {
      invokeLimiter(limiter, "198.51.100.31", `/items/flood-${i}`);
    }
    assert.equal(
      invokeLimiter(limiter, "198.51.100.30", "/items/b"),
      429,
      "flooding one route with many ids from one address must not reset another address's bucket",
    );
  }

  console.log("rate-limit tests passed");
};

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
