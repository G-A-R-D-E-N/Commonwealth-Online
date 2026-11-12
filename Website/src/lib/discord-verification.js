"use strict";

const crypto = require("node:crypto");

const { getDb } = require("../db");

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
 *   db: import("better-sqlite3").Database,
 *   now?: () => number,
 *   randomCode?: () => string,
 * }} deps
 */
const createDiscordVerifier = ({ findMember, sendCode, db, now = Date.now, randomCode = randomSixDigits }) => {
  const records = {
    insert: db.prepare(
      "INSERT INTO discord_verifications (id, discord_id, username, code_hash, expires_at) VALUES (?, ?, ?, ?, ?)"
    ),
    byId: db.prepare("SELECT * FROM discord_verifications WHERE id = ?"),
    remove: db.prepare("DELETE FROM discord_verifications WHERE id = ?"),
    removeUnverified: db.prepare("DELETE FROM discord_verifications WHERE discord_id = ? AND verified = 0"),
    removeExpired: db.prepare("DELETE FROM discord_verifications WHERE expires_at <= ?"),
    count: db.prepare("SELECT COUNT(*) AS count FROM discord_verifications"),
    recordAttempt: db.prepare("UPDATE discord_verifications SET attempts = attempts + 1 WHERE id = ?"),
    markVerified: db.prepare("UPDATE discord_verifications SET verified = 1, expires_at = ? WHERE id = ?"),
  };
  const sends = {
    insert: db.prepare("INSERT INTO discord_verification_sends (discord_id, sent_at) VALUES (?, ?)"),
    remove: db.prepare("DELETE FROM discord_verification_sends WHERE id = ?"),
    removeBefore: db.prepare("DELETE FROM discord_verification_sends WHERE sent_at <= ?"),
    summary: db.prepare(
      "SELECT COUNT(*) AS count, MIN(sent_at) AS oldest, MAX(sent_at) AS newest FROM discord_verification_sends WHERE discord_id = ?"
    ),
  };

  const sweep = (time) => {
    records.removeExpired.run(time);
    sends.removeBefore.run(time - ACCOUNT_WINDOW_MS);
  };

  // Constraint: the send is reserved in the same transaction that checks the limits, before the DM is awaited.
  const reserveSend = db.transaction((account, time) => {
    const { count, oldest, newest } = sends.summary.get(account);
    if (newest !== null && time - newest < RESEND_COOLDOWN_MS) {
      return { ok: false, code: "cooldown", retryAfterSeconds: Math.ceil((RESEND_COOLDOWN_MS - (time - newest)) / 1000) };
    }
    if (count >= MAX_CODES_PER_ACCOUNT) {
      return { ok: false, code: "too_many_codes", retryAfterSeconds: Math.ceil((ACCOUNT_WINDOW_MS - (time - oldest)) / 1000) };
    }
    if (records.count.get().count >= MAX_PENDING) {
      return { ok: false, code: "unavailable" };
    }
    return { ok: true, sendId: Number(sends.insert.run(account, time).lastInsertRowid) };
  }).immediate;

  const storePending = db.transaction((id, account, username, codeHash, expiresAt) => {
    records.removeUnverified.run(account);
    records.insert.run(id, account, username, codeHash, expiresAt);
  }).immediate;

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
    const reserved = reserveSend(account, time);
    if (!reserved.ok) {
      return reserved;
    }

    const code = randomCode();
    const sent = await sendCode(lookup.member, code, CODE_TTL_MS / 60_000);
    if (!sent?.ok) {
      sends.remove.run(reserved.sendId);
      return { ok: false, code: sent?.blocked ? "dm_blocked" : "unavailable" };
    }

    const id = crypto.randomBytes(16).toString("hex");
    storePending(id, account, lookup.member.username, hashCode(id, code), time + CODE_TTL_MS);
    return { ok: true, verificationId: id, username: lookup.member.username, expiresInSeconds: CODE_TTL_MS / 1000 };
  };

  const live = (verificationId, time) => {
    const id = String(verificationId || "");
    if (!ID_PATTERN.test(id)) {
      return null;
    }
    const record = records.byId.get(id);
    if (!record || record.expires_at <= time) {
      records.remove.run(id);
      return null;
    }
    return record;
  };

  const confirm = db.transaction((verificationId, code) => {
    const time = now();
    const record = live(verificationId, time);
    if (!record) {
      return { ok: false, code: "expired" };
    }
    if (record.verified) {
      return { ok: true, username: record.username };
    }
    const entered = String(code || "").trim();
    records.recordAttempt.run(record.id);
    const attempts = record.attempts + 1;
    const matches = CODE_PATTERN.test(entered) && crypto.timingSafeEqual(hashCode(record.id, entered), record.code_hash);
    if (!matches) {
      if (attempts >= MAX_ATTEMPTS) {
        records.remove.run(record.id);
        return { ok: false, code: "expired" };
      }
      return { ok: false, code: "wrong_code", attemptsLeft: MAX_ATTEMPTS - attempts };
    }
    records.markVerified.run(time + VERIFIED_TTL_MS, record.id);
    return { ok: true, username: record.username };
  }).immediate;

  const verifiedRecord = (verificationId) => {
    const record = live(verificationId, now());
    return record && record.verified ? record : null;
  };

  // Constraint: the save and the consume share one transaction, so a failed save rolls back and the verification survives.
  const redeem = db.transaction((verificationId, save) => {
    const record = verifiedRecord(verificationId);
    if (!record) {
      return { ok: false };
    }
    const saved = save({ discordId: record.discord_id, username: record.username });
    records.remove.run(record.id);
    return { ok: true, saved };
  }).immediate;

  const consume = (verificationId) => {
    const redeemed = redeem(verificationId, (verified) => verified);
    return redeemed.ok ? { ok: true, ...redeemed.saved } : { ok: false };
  };

  return { start, confirm, redeem, consume, pendingCount: () => records.count.get().count };
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

const verifyForSubmission = (verifier, verificationId, code, save) => {
  if (!verificationId) {
    return { ok: false, field: "discordHandle", message: UNVERIFIED_MESSAGE };
  }
  if (String(code || "").trim()) {
    const confirmed = verifier.confirm(verificationId, code);
    if (!confirmed.ok) {
      return { ok: false, field: "discordCode", message: CODE_FAILURE_MESSAGE };
    }
  }
  const redeemed = verifier.redeem(verificationId, save);
  if (!redeemed.ok) {
    return { ok: false, field: "discordHandle", message: UNVERIFIED_MESSAGE };
  }
  return { ok: true, saved: redeemed.saved };
};

let defaultVerifier;

const getDiscordVerifier = () => {
  if (!defaultVerifier) {
    const { findGuildMemberByUsername, sendVerificationCode } = require("./discord");
    defaultVerifier = createDiscordVerifier({
      findMember: findGuildMemberByUsername,
      sendCode: sendVerificationCode,
      db: getDb(),
    });
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
