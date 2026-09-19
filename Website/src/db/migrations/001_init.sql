-- Commonwealth Online schema
--
-- Phase 1 scaffolding: the tables the forum, applications and API work will be
-- built on. Forum and account tables are intentionally unused by the current
-- routes - they exist so the data model is ready before the features land.

-- Accounts ---------------------------------------------------------------
-- Forum authors. Logins are expected to come from Discord OAuth (or the bot),
-- so the Discord identity is the stable external key.
CREATE TABLE IF NOT EXISTS accounts (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  discord_id   TEXT UNIQUE,
  username     TEXT NOT NULL,
  display_name TEXT,
  avatar_url   TEXT,
  role         TEXT NOT NULL DEFAULT 'member', -- member | moderator | admin
  created_at   TEXT NOT NULL DEFAULT (datetime('now')),
  updated_at   TEXT NOT NULL DEFAULT (datetime('now'))
);

-- Forum ------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS forum_categories (
  id          INTEGER PRIMARY KEY AUTOINCREMENT,
  slug        TEXT NOT NULL UNIQUE,
  name        TEXT NOT NULL,
  description TEXT,
  position    INTEGER NOT NULL DEFAULT 0,
  is_locked   INTEGER NOT NULL DEFAULT 0,
  created_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS forum_threads (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  category_id  INTEGER NOT NULL REFERENCES forum_categories(id) ON DELETE CASCADE,
  author_id    INTEGER REFERENCES accounts(id) ON DELETE SET NULL,
  title        TEXT NOT NULL,
  slug         TEXT NOT NULL,
  is_pinned    INTEGER NOT NULL DEFAULT 0,
  is_locked    INTEGER NOT NULL DEFAULT 0,
  reply_count  INTEGER NOT NULL DEFAULT 0,
  last_post_at TEXT,
  created_at   TEXT NOT NULL DEFAULT (datetime('now')),
  updated_at   TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_forum_threads_category
  ON forum_threads (category_id, is_pinned DESC, last_post_at DESC);

CREATE TABLE IF NOT EXISTS forum_posts (
  id                INTEGER PRIMARY KEY AUTOINCREMENT,
  thread_id         INTEGER NOT NULL REFERENCES forum_threads(id) ON DELETE CASCADE,
  author_id         INTEGER REFERENCES accounts(id) ON DELETE SET NULL,
  body              TEXT NOT NULL,
  discord_message_id TEXT,
  edited_at         TEXT,
  created_at        TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_forum_posts_thread
  ON forum_posts (thread_id, created_at);

-- Applications -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS applications (
  id                 INTEGER PRIMARY KEY AUTOINCREMENT,
  public_id          TEXT NOT NULL UNIQUE,
  discord_id         TEXT,
  discord_handle     TEXT,
  display_name       TEXT NOT NULL,
  email              TEXT,
  timezone           TEXT,
  availability       TEXT,
  experience         TEXT,
  motivation         TEXT NOT NULL,
  status             TEXT NOT NULL DEFAULT 'pending', -- pending | reviewing | accepted | rejected
  source             TEXT NOT NULL DEFAULT 'web',     -- web | discord-bot | api
  ip_hash            TEXT,
  user_agent         TEXT,
  discord_message_id TEXT,
  reviewed_by        TEXT,
  reviewed_at        TEXT,
  created_at         TEXT NOT NULL DEFAULT (datetime('now')),
  updated_at         TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_applications_status
  ON applications (status, created_at DESC);

-- Seed -------------------------------------------------------------------
INSERT OR IGNORE INTO forum_categories (slug, name, description, position)
VALUES ('general', 'General', 'General discussion about Commonwealth Online.', 0);
