"use strict";

const assert = require("node:assert/strict");

require.cache[require.resolve("../src/db")] = { exports: { getDb: () => null, initDb: () => {} } };

const members = {
  vaultdweller: { id: "111111111111111111", username: "VaultDweller" },
  overseer: { id: "333333333333333333", username: "Overseer" },
};
const dms = [];
require.cache[require.resolve("../src/lib/discord")] = {
  exports: {
    discordConfigured: () => false,
    membershipConfigured: () => true,
    postApplication: async () => ({ ok: false, skipped: true }),
    syncApplicationPost: async () => ({ ok: false, skipped: true }),
    findGuildMemberByUsername: async (username) => ({
      ok: true,
      member: members[String(username).trim().toLowerCase()] || null,
    }),
    sendVerificationCode: async (member, code) => {
      dms.push({ to: member.id, code });
      return { ok: true };
    },
  },
};

const applications = require("../src/lib/applications");
const created = [];
applications.createApplication = (value) => {
  created.push(value);
  return { publicId: `ref-${created.length}`, type: value.type, status: "pending", createdAt: "2026-10-25 00:00:00" };
};

const { createApp } = require("../src/app");

const teamFields = () => {
  const values = {};
  for (const field of applications.TYPES.team.fields) {
    if (field.type === "checkbox") {
      values[field.key] = true;
    } else if (field.type === "select") {
      values[field.key] = field.options[0].value;
    } else if (field.type === "email") {
      values[field.key] = "applicant@example.com";
    } else if (field.type === "url") {
      values[field.key] = "https://example.com";
    } else {
      values[field.key] = "Applicant information that meets the required minimum length.";
    }
  }
  return values;
};

const main = async () => {
  const server = await new Promise((resolve) => {
    const listening = createApp().listen(0, "127.0.0.1", () => resolve(listening));
  });
  const base = `http://127.0.0.1:${server.address().port}`;
  const json = (path, body) =>
    fetch(`${base}${path}`, {
      method: "POST",
      headers: { "Content-Type": "application/json", Accept: "application/json" },
      body: JSON.stringify(body),
    });
  const form = (path, body) =>
    fetch(`${base}${path}`, {
      method: "POST",
      redirect: "manual",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams(Object.entries(body).map(([key, value]) => [key, String(value)])).toString(),
    });

  try {
    {
      const response = await json("/api/v1/applications/team", { ...teamFields(), discordHandle: "VaultDweller" });
      assert.equal(response.status, 422, "an unverified application is refused");
      assert.equal((await response.json()).error.details[0].field, "discordHandle");
      assert.equal(created.length, 0);
    }

    {
      const response = await json("/api/v1/applications/discord-verification", { username: "stranger" });
      assert.equal(response.status, 422);
      assert.match((await response.json()).error.details[0].message, /not in the Commonwealth Online server/);
    }

    let verificationId;
    {
      const response = await json("/api/v1/applications/discord-verification", { username: "vaultdweller" });
      assert.equal(response.status, 200);
      const body = await response.json();
      verificationId = body.verificationId;
      assert.equal(body.username, "VaultDweller");
      assert.equal(dms.length, 1);
      assert.equal(dms[0].to, members.vaultdweller.id);
      assert.equal("code" in body, false, "the code is only sent by DM");
    }

    {
      const response = await json("/api/v1/applications/discord-verification/confirm", { verificationId, code: "000000" === dms[0].code ? "000001" : "000000" });
      assert.equal(response.status, 422);
      assert.equal((await response.json()).error.details[0].field, "discordCode");
      const confirmed = await json("/api/v1/applications/discord-verification/confirm", { verificationId, code: dms[0].code });
      assert.equal(confirmed.status, 200);
      assert.deepEqual(await confirmed.json(), { verified: true, username: "VaultDweller" });
    }

    {
      const response = await json("/api/v1/applications/team", {
        ...teamFields(),
        discordHandle: "SomeoneElse",
        discordId: "999999999999999999",
        discordVerification: verificationId,
      });
      assert.equal(response.status, 201);
      assert.equal(created.length, 1);
      assert.equal(created[0].discordId, members.vaultdweller.id, "the verified Discord id is stored");
      assert.equal(created[0].discordHandle, "VaultDweller", "the verified username replaces the typed one");

      const reused = await json("/api/v1/applications/team", { ...teamFields(), discordVerification: verificationId });
      assert.equal(reused.status, 422, "a verification can't be reused for a second application");
    }

    {
      const first = await form("/apply/team", teamFields());
      assert.equal(first.status, 422, "the no-JS form validates the Discord username first");
    }

    {
      const step1 = await form("/apply/team", { ...teamFields(), discordHandle: "overseer" });
      assert.equal(step1.status, 200);
      const html = await step1.text();
      assert.match(html, /We sent a 6-digit code to your Discord DMs/);
      const id = /name="discordVerification" id="discord-verification-id" value="([0-9a-f]{32})"/.exec(html)?.[1];
      assert.ok(id, "the no-JS form carries the verification id to the second step");
      const sentCode = dms[dms.length - 1].code;
      assert.equal(dms[dms.length - 1].to, members.overseer.id);

      const wrong = await form("/apply/team", { ...teamFields(), discordHandle: "Overseer", discordVerification: id, discordCode: "12345x" });
      assert.equal(wrong.status, 422);
      assert.match(await wrong.text(), new RegExp(`value="${id}"`), "a wrong code keeps the verification so the applicant can retry");

      const step2 = await form("/apply/team", { ...teamFields(), discordHandle: "Overseer", discordVerification: id, discordCode: sentCode });
      assert.equal(step2.status, 303);
      assert.match(step2.headers.get("location"), /^\/apply\/thanks\?ref=ref-2$/);
      assert.equal(created[1].discordId, members.overseer.id);
    }

    {
      const removed = await fetch(`${base}/api/v1/applications/discord-membership?username=VaultDweller`);
      assert.notEqual(removed.status, 200, "the username-only membership check is gone");
    }
  } finally {
    server.close();
  }

  console.log("application verification route tests passed");
};

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
