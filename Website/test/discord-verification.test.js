"use strict";

const assert = require("node:assert/strict");
const {
  ACCOUNT_WINDOW_MS,
  CODE_TTL_MS,
  MAX_ATTEMPTS,
  RESEND_COOLDOWN_MS,
  VERIFIED_TTL_MS,
  createDiscordVerifier,
  describeStartFailure,
  verifyForSubmission,
} = require("../src/lib/discord-verification");

const members = {
  vaultdweller: { id: "111111111111111111", username: "VaultDweller" },
  nodms: { id: "222222222222222222", username: "NoDms" },
};

const harness = () => {
  let clock = 1_000_000;
  let nextCode = 100000;
  const sent = [];
  const verifier = createDiscordVerifier({
    findMember: async (username) => ({ ok: true, member: members[String(username).toLowerCase()] || null }),
    sendCode: async (member, code, minutes) => {
      if (member.id === members.nodms.id) {
        return { ok: false, blocked: true };
      }
      sent.push({ to: member.id, code, minutes });
      return { ok: true };
    },
    now: () => clock,
    randomCode: () => String(nextCode++),
  });
  return { verifier, sent, advance: (ms) => { clock += ms; } };
};

const main = async () => {
  {
    const { verifier, sent } = harness();
    const started = await verifier.start("vaultdweller");
    assert.equal(started.ok, true);
    assert.equal(started.username, "VaultDweller", "the canonical username comes from Discord");
    assert.match(started.verificationId, /^[0-9a-f]{32}$/);
    assert.equal(started.expiresInSeconds, CODE_TTL_MS / 1000);
    assert.deepEqual(sent, [{ to: members.vaultdweller.id, code: "100000", minutes: CODE_TTL_MS / 60_000 }]);

    assert.equal(verifier.consume(started.verificationId).ok, false, "an unconfirmed code can't be used to apply");
    const wrong = verifier.confirm(started.verificationId, "999999");
    assert.equal(wrong.ok, false);
    assert.equal(wrong.attemptsLeft, MAX_ATTEMPTS - 1);
    assert.equal(verifier.confirm(started.verificationId, "100000").ok, true);
    assert.equal(verifier.confirm(started.verificationId, "anything").ok, true, "a verified id stays verified");

    const consumed = verifier.consume(started.verificationId);
    assert.deepEqual(consumed, { ok: true, discordId: members.vaultdweller.id, username: "VaultDweller" });
    assert.equal(verifier.consume(started.verificationId).ok, false, "a verification is single use");
  }

  {
    const { verifier, sent } = harness();
    assert.deepEqual(await verifier.start("stranger"), { ok: false, code: "not_member" });
    assert.deepEqual(await verifier.start("nodms"), { ok: false, code: "dm_blocked" });
    assert.equal(sent.length, 0);
    assert.equal(describeStartFailure({ code: "not_member" }).status, 422);
    assert.equal(describeStartFailure({ code: "dm_blocked" }).status, 422);
    assert.equal(describeStartFailure({ code: "cooldown" }).status, 429);
    assert.equal(describeStartFailure({ code: "surprise" }).status, 503);
  }

  {
    const { verifier, sent, advance } = harness();
    const first = await verifier.start("vaultdweller");
    const blocked = await verifier.start("VaultDweller");
    assert.equal(blocked.code, "cooldown", "the same account can't be DMed again within the cooldown");
    assert.ok(blocked.retryAfterSeconds > 0 && blocked.retryAfterSeconds <= RESEND_COOLDOWN_MS / 1000);

    advance(RESEND_COOLDOWN_MS);
    const second = await verifier.start("vaultdweller");
    assert.equal(second.ok, true);
    assert.equal(verifier.confirm(first.verificationId, sent[0].code).ok, false, "a new code replaces the old one");

    advance(RESEND_COOLDOWN_MS);
    assert.equal((await verifier.start("vaultdweller")).ok, true);
    advance(RESEND_COOLDOWN_MS);
    const capped = await verifier.start("vaultdweller");
    assert.equal(capped.code, "too_many_codes", "at most three codes per account per window");

    advance(ACCOUNT_WINDOW_MS);
    assert.equal((await verifier.start("vaultdweller")).ok, true, "the window resets");
  }

  {
    const { verifier } = harness();
    const started = await verifier.start("vaultdweller");
    for (let attempt = 1; attempt < MAX_ATTEMPTS; attempt += 1) {
      assert.equal(verifier.confirm(started.verificationId, "000001").code, "wrong_code");
    }
    assert.equal(verifier.confirm(started.verificationId, "000001").code, "expired", "the last wrong guess burns the code");
    assert.equal(verifier.confirm(started.verificationId, "100000").ok, false, "the right code no longer works");
  }

  {
    const { verifier, advance } = harness();
    const started = await verifier.start("vaultdweller");
    advance(CODE_TTL_MS);
    assert.equal(verifier.confirm(started.verificationId, "100000").code, "expired");

    advance(RESEND_COOLDOWN_MS);
    const again = await verifier.start("vaultdweller");
    assert.equal(verifier.confirm(again.verificationId, "100001").ok, true);
    advance(VERIFIED_TTL_MS);
    assert.equal(verifier.consume(again.verificationId).ok, false, "a verification expires if the form isn't sent");
    assert.equal(verifier.pendingCount(), 0);
  }

  {
    const { verifier } = harness();
    assert.equal(verifier.confirm("not-an-id", "100000").ok, false);
    assert.equal(verifier.confirm("f".repeat(32), "100000").ok, false);

    assert.equal(verifyForSubmission(verifier, "", "").field, "discordHandle");
    const started = await verifier.start("vaultdweller");
    assert.equal(verifyForSubmission(verifier, started.verificationId, "123456").field, "discordCode");
    assert.equal(verifyForSubmission(verifier, started.verificationId, "").field, "discordHandle", "no code and not yet verified");
    const submitted = verifyForSubmission(verifier, started.verificationId, "100000");
    assert.deepEqual(submitted, { ok: true, discordId: members.vaultdweller.id, username: "VaultDweller" });
  }

  {
    let release;
    const gate = new Promise((resolve) => { release = resolve; });
    let dms = 0;
    const verifier = createDiscordVerifier({
      findMember: async () => ({ ok: true, member: members.vaultdweller }),
      sendCode: async () => { dms += 1; await gate; return { ok: true }; },
    });
    const racing = [verifier.start("vaultdweller"), verifier.start("vaultdweller"), verifier.start("vaultdweller")];
    await new Promise((resolve) => setImmediate(resolve));
    release();
    const results = await Promise.all(racing);
    assert.equal(dms, 1, "parallel requests send one DM");
    assert.equal(results.filter((result) => result.ok).length, 1);
  }

  console.log("discord verification tests passed");
};

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
