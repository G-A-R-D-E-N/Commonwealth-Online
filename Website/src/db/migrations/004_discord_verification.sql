-- Discord applicant verification state, shared by every process that opens the database.
-- Codes are stored as SHA-256 hashes keyed by the verification id. Times are epoch milliseconds.

CREATE TABLE IF NOT EXISTS discord_verifications (
  id         TEXT PRIMARY KEY,
  discord_id TEXT NOT NULL,
  username   TEXT NOT NULL,
  code_hash  BLOB NOT NULL,
  attempts   INTEGER NOT NULL DEFAULT 0,
  verified   INTEGER NOT NULL DEFAULT 0,
  expires_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_discord_verifications_account
  ON discord_verifications (discord_id, verified);

CREATE TABLE IF NOT EXISTS discord_verification_sends (
  id         INTEGER PRIMARY KEY AUTOINCREMENT,
  discord_id TEXT NOT NULL,
  sent_at    INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_discord_verification_sends_account
  ON discord_verification_sends (discord_id, sent_at);
