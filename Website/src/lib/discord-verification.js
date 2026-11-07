"use strict";

const crypto = require("node:crypto");

const CODE_TTL_MS = 10 * 60_000;
const VERIFIED_TTL_MS = 60 * 60_000;
const MAX_ATTEMPTS = 5;
const RESEND_COOLDOWN_MS = 60_000;
const MAX_CODES_PER_ACCOUNT = 3;
const ACCOUNT_WINDOW_MS = 15 * 60_000;
const MAX_PENDING = 5000;
const CODE_PATTERN = /^\d{6}$/;
const ID_PATTERN = /^[0-9a-f]{32}$/;

const randomSixDigits = () => String(crypto.randomInt(0, 1_000_000)).padStart(6, "0");

const hashCode = (id, code) => crypto.createHash("sha256").update(`${id}:${code}`).digest();

/**
 * @param {{
 *   findMember: (username: string) => Promise<{ok: boolean, member?: {id: string, username: string} | null}>,
 *   sendCode: (member: object, code: string, minutes: number) => Promise<{ok: boolean, blocked?: boolean}>,
 *   now?: () => number,
 *   randomCode?: () => string,
 * }} deps
 */
const createDiscordVerifier = ({ findMember, sendCode, now = Date.now, randomCode = randomSixDigits }) => {
  const pending = new Map();
  const sendsByAccount = new Map();

  const sweep = (time) => {
    for (const [id, record] of pending) {
      if (record.expiresAt <= time) {
        pending.delete(id);
      }
    }
    for (const [account, times] of sendsByAccount) {
      const recent = times.filter((sent) => time - sent < ACCOUNT_WINDOW_MS);
      if (recent.length) {
        sendsByAccount.set(account, recent);
      } else {
        sendsByAccount.delete(account);
      }
    }
  };

  const start = async (username) => {
    const time = now();
    sweep(time);

    const lookup = await findMember(username);
    if (!lookup?.ok) {
      return { ok: false, code: "unavailable" };
    }
    if (!lookup.member) {
      return { ok: false, code: "not_member" };
    }

    const account = lookup.member.id;
    const times = sendsByAccount.get(account) || [];
    const last = times[times.length - 1];
    if (last !== undefined && time - last < RESEND_COOLDOWN_MS) {
      return { ok: false, code: "cooldown", retryAfterSeconds: Math.ceil((RESEND_COOLDOWN_MS - (time - last)) / 1000) };
    }
    if (times.length >= MAX_CODES_PER_ACCOUNT) {
      return { ok: false, code: "too_many_codes", retryAfterSeconds: Math.ceil((ACCOUNT_WINDOW_MS - (time - times[0])) / 1000) };
    }
    if (pending.size >= MAX_PENDING) {
      return { ok: false, code: "unavailable" };
    }

    // Reserve the send before awaiting the DM so parallel requests can't bypass the cooldown.
    times.push(time);
    sendsByAccount.set(account, times);

    const code = randomCode();
    const sent = await sendCode(lookup.member, code, CODE_TTL_MS / 60_000);
    if (!sent?.ok) {
      times.pop();
      if (!times.length) {
        sendsByAccount.delete(account);
      }
      return { ok: false, code: sent?.blocked ? "dm_blocked" : "unavailable" };
    }

    for (const [id, record] of pending) {
      if (record.discordId === account && !record.verified) {
        pending.delete(id);
      }
    }
    const id = crypto.randomBytes(16).toString("hex");
    pending.set(id, {
      discordId: account,
      username: lookup.member.username,
      codeHash: hashCode(id, code),
      attempts: 0,
      verified: false,
      expiresAt: time + CODE_TTL_MS,
    });
    return { ok: true, verificationId: id, username: lookup.member.username, expiresInSeconds: CODE_TTL_MS / 1000 };
  };

  const live = (verificationId, time) => {
    const id = String(verificationId || "");
    if (!ID_PATTERN.test(id)) {
      return null;
    }
    const record = pending.get(id);
    if (!record || record.expiresAt <= time) {
      pending.delete(id);
      return null;
    }
    return { id, record };
  };

  const confirm = (verificationId, code) => {
    const time = now();
    const found = live(verificationId, time);
    if (!found) {
      return { ok: false, code: "expired" };
    }
    if (found.record.verified) {
      return { ok: true, username: found.record.username };
    }
    const entered = String(code || "").trim();
    found.record.attempts += 1;
    const matches = CODE_PATTERN.test(entered) && crypto.timingSafeEqual(hashCode(found.id, entered), found.record.codeHash);
    if (!matches) {
      if (found.record.attempts >= MAX_ATTEMPTS) {
        pending.delete(found.id);
        return { ok: false, code: "expired" };
      }
      return { ok: false, code: "wrong_code", attemptsLeft: MAX_ATTEMPTS - found.record.attempts };
    }
    found.record.verified = true;
    found.record.expiresAt = time + VERIFIED_TTL_MS;
    return { ok: true, username: found.record.username };
  };

  const consume = (verificationId) => {
    const found = live(verificationId, now());
    if (!found || !found.record.verified) {
      return { ok: false };
    }
    pending.delete(found.id);
    return { ok: true, discordId: found.record.discordId, username: found.record.username };
  };

  return { start, confirm, consume, pendingCount: () => pending.size };
};

const START_FAILURES = {
  not_member: {
    status: 422,
    message: "That Discord username is not in the Commonwealth Online server. Join the server, then try again.",
  },
  dm_blocked: {
    status: 422,
    message:
      "We couldn't send you a DM. In Discord, allow direct messages from members of the Commonwealth Online server, then try again.",
  },
  cooldown: { status: 429, message: "A code was just sent to that account. Wait a minute before asking for another." },
  too_many_codes: { status: 429, message: "Too many codes were sent to that account. Please try again later." },
  unavailable: { status: 503, message: "Discord verification is temporarily unavailable. Please try again later." },
};

const describeStartFailure = (result) => START_FAILURES[result?.code] || START_FAILURES.unavailable;

const CODE_FAILURE_MESSAGE = "That code is wrong or has expired. Check your Discord DMs, or send a new code.";
const UNVERIFIED_MESSAGE = "Verify your Discord account with the code we DM you before applying.";

const verifyForSubmission = (verifier, verificationId, code) => {
  if (!verificationId) {
    return { ok: false, field: "discordHandle", message: UNVERIFIED_MESSAGE };
  }
  if (String(code || "").trim()) {
    const confirmed = verifier.confirm(verificationId, code);
    if (!confirmed.ok) {
      return { ok: false, field: "discordCode", message: CODE_FAILURE_MESSAGE };
    }
  }
  const consumed = verifier.consume(verificationId);
  if (!consumed.ok) {
    return { ok: false, field: "discordHandle", message: UNVERIFIED_MESSAGE };
  }
  return consumed;
};

let defaultVerifier;

const getDiscordVerifier = () => {
  if (!defaultVerifier) {
    const { findGuildMemberByUsername, sendVerificationCode } = require("./discord");
    defaultVerifier = createDiscordVerifier({ findMember: findGuildMemberByUsername, sendCode: sendVerificationCode });
  }
  return defaultVerifier;
};

module.exports = {
  CODE_TTL_MS,
  VERIFIED_TTL_MS,
  MAX_ATTEMPTS,
  RESEND_COOLDOWN_MS,
  MAX_CODES_PER_ACCOUNT,
  ACCOUNT_WINDOW_MS,
  CODE_FAILURE_MESSAGE,
  UNVERIFIED_MESSAGE,
  createDiscordVerifier,
  describeStartFailure,
  getDiscordVerifier,
  verifyForSubmission,
};
