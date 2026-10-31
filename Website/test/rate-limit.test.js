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
  return app;
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

  console.log("rate-limit tests passed");
};

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
