"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const http = require("node:http");
const path = require("node:path");

process.env.NODE_ENV = "production";
process.env.ADMIN_TOKEN = "security-test-admin-token";
process.env.DATABASE_PATH = ":memory:";
process.env.DISCORD_BOT_TOKEN = "";
process.env.DISCORD_GUILD_ID = "";

const { createApp } = require("../src/app");
const { closeDb, getDb } = require("../src/db");
const { createApplication, TYPES, validateApplication } = require("../src/lib/applications");
const { clientIp, rateLimit } = require("../src/middleware/rate-limit");
const { isSafeImage, isSafeLink, protocolOf } = require("../static/js/url-policy");

const listen = (server) =>
  new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => resolve(server.address()));
  });

const close = (server) =>
  new Promise((resolve) => server.close(resolve));

const request = (base, pathname, headers = {}) =>
  new Promise((resolve, reject) => {
    const req = http.get(
      {
        hostname: base.address,
        port: base.port,
        path: pathname,
        headers,
      },
      (res) => {
        const chunks = [];
        res.on("data", (chunk) => chunks.push(chunk));
        res.on("end", () => {
          resolve({
            status: res.statusCode,
            headers: res.headers,
            text: Buffer.concat(chunks).toString("utf8"),
          });
        });
      }
    );
    req.on("error", reject);
  });

const fakeResponse = () => {
  const state = { headers: {}, status: 200, body: null };
  return {
    state,
    set(name, value) {
      state.headers[name] = value;
      return this;
    },
    status(code) {
      state.status = code;
      return this;
    },
    json(body) {
      state.body = body;
      return this;
    },
  };
};

const rateReq = (forwarded) => ({
  method: "POST",
  baseUrl: "/api/v1/applications",
  path: "/",
  ip: "203.0.113.10",
  headers: { "x-forwarded-for": forwarded },
  socket: { remoteAddress: "203.0.113.10" },
});

assert.equal(clientIp(rateReq("198.51.100.1")), "203.0.113.10");

const limiter = rateLimit({ windowMs: 60_000, max: 1 });
let passed = 0;
const first = fakeResponse();
limiter(rateReq("198.51.100.1"), first, () => {
  passed += 1;
});
const second = fakeResponse();
limiter(rateReq("198.51.100.2"), second, () => {
  passed += 1;
});
assert.equal(passed, 1);
assert.equal(second.state.status, 429);

for (const unsafe of [
  "javascript:alert(1)",
  "java\tscript:alert(1)",
  "java\nscript:alert(1)",
  "java\rscript:alert(1)",
  "data:text/html,<script>alert(1)</script>",
  "vbscript:msgbox(1)",
]) {
  assert.equal(isSafeLink(unsafe, "https://example.com/"), false, unsafe);
}
assert.equal(protocolOf("java\nscript:alert(1)", "https://example.com/"), "javascript:");
assert.equal(isSafeLink("/relative", "https://example.com/"), true);
assert.equal(isSafeLink("mailto:test@example.com", "https://example.com/"), true);
assert.equal(isSafeImage("data:image/svg+xml,<svg></svg>", "https://example.com/"), false);
assert.equal(isSafeImage("data:image/png;base64,AA==", "https://example.com/"), true);

const updatesSource = fs.readFileSync(path.join(__dirname, "../static/js/updates.js"), "utf8");
const repoSource = fs.readFileSync(path.join(__dirname, "../static/js/repo.js"), "utf8");
for (const source of [updatesSource, repoSource]) {
  assert.match(source, /allowedTags/);
  assert.match(source, /CoUrlPolicy/);
  assert.doesNotMatch(source, /\^javascript:/);
}

const workflow = fs.readFileSync(
  path.join(__dirname, "../../.github/workflows/co-website-static.yml"),
  "utf8"
);
assert.match(workflow, /actions\/configure-pages@983d7736d9b0ae728b81ab479565c72886d7745b/);
assert.match(workflow, /actions\/upload-pages-artifact@7b1f4a764d45c48632c6b24a0339c27f5614fb0b/);
assert.match(workflow, /actions\/deploy-pages@d6db90164ac5ed86f2b6aed7e0febac5b3c0c03e/);

const run = async () => {
  const app = createApp();
  const server = http.createServer(app);
  const address = await listen(server);

  try {
  const home = await request(address, "/");
  assert.equal(home.status, 200);
  assert.match(home.text, /http-equiv="Content-Security-Policy"/);
  assert.match(String(home.headers["content-security-policy"] || ""), /script-src 'self'/);

  const membership = await request(
    address,
    "/api/v1/applications/discord-membership?username=example"
  );
  assert.equal(membership.status, 401);
  assert.doesNotMatch(membership.text, /"member"/);

  const noToken = await request(address, "/api/v1/applications");
  assert.equal(noToken.status, 401);

  const badToken = await request(address, "/api/v1/applications", {
    Authorization: "Bearer wrong-token",
  });
  assert.equal(badToken.status, 401);

  const goodToken = await request(address, "/api/v1/applications", {
    Authorization: "Bearer security-test-admin-token",
  });
  assert.equal(goodToken.status, 200);

  const application = { type: "team", source: "web" };
  for (const field of TYPES.team.fields) {
    if (field.type === "checkbox") {
      application[field.key] = true;
    } else if (field.type === "select") {
      application[field.key] = field.options[0].value;
    } else if (field.type === "email") {
      application[field.key] = "security@example.com";
    } else if (field.type === "url") {
      application[field.key] = "https://example.com";
    } else {
      application[field.key] = "Security test value that satisfies the required minimum length.";
    }
  }
  const validated = validateApplication(application);
  assert.equal(validated.ok, true);
  const stored = createApplication(validated.value, {
    ip: "198.51.100.25",
    userAgent: "security-test",
  });
  const row = getDb().prepare("SELECT ip_hash FROM applications WHERE id = ?").get(stored.id);
  assert.equal(row.ip_hash, null);

  closeDb();
  const originalError = console.error;
  console.error = () => {};
  let health;
  try {
    health = await request(address, "/api/v1/health");
  } finally {
    console.error = originalError;
  }
  assert.equal(health.status, 503);
  assert.doesNotMatch(health.text, /Database has not been initialised/);
  const healthBody = JSON.parse(health.text);
  assert.equal(healthBody.checks.database, "error");
  assert.equal(Object.hasOwn(healthBody, "environment"), false);
  assert.equal(Object.hasOwn(healthBody.checks, "discordBot"), false);
  } finally {
    await close(server);
    closeDb();
  }

  console.log("security checks passed");
};

run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
