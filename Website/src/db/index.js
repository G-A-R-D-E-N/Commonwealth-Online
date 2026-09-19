"use strict";

/**
 * SQLite access. One connection per process, opened through `initDb()` which
 * also applies any pending migrations from src/db/migrations/*.sql.
 */

const fs = require("node:fs");
const path = require("node:path");
const Database = require("better-sqlite3");

const config = require("../config");

const MIGRATIONS_DIR = path.join(__dirname, "migrations");

let db = null;

const ensureDataDirectory = (databasePath) => {
  if (databasePath === ":memory:") {
    return;
  }
  fs.mkdirSync(path.dirname(databasePath), { recursive: true });
};

const runMigrations = (instance) => {
  instance.exec(`
    CREATE TABLE IF NOT EXISTS schema_migrations (
      id         TEXT PRIMARY KEY,
      applied_at TEXT NOT NULL DEFAULT (datetime('now'))
    )
  `);

  const applied = new Set(
    instance.prepare("SELECT id FROM schema_migrations").all().map((row) => row.id)
  );
  const files = fs
    .readdirSync(MIGRATIONS_DIR)
    .filter((file) => file.endsWith(".sql"))
    .sort();

  const record = instance.prepare("INSERT INTO schema_migrations (id) VALUES (?)");
  const pending = [];

  for (const file of files) {
    if (applied.has(file)) {
      continue;
    }
    const sql = fs.readFileSync(path.join(MIGRATIONS_DIR, file), "utf8");
    instance.transaction(() => {
      instance.exec(sql);
      record.run(file);
    })();
    pending.push(file);
  }

  return pending;
};

const initDb = () => {
  if (db) {
    return db;
  }

  ensureDataDirectory(config.databasePath);
  db = new Database(config.databasePath);
  db.pragma("journal_mode = WAL");
  db.pragma("foreign_keys = ON");

  const applied = runMigrations(db);
  if (applied.length) {
    console.log(`[db] applied migrations: ${applied.join(", ")}`);
  }

  return db;
};

const getDb = () => {
  if (!db) {
    throw new Error("Database has not been initialised. Call initDb() first.");
  }
  return db;
};

const closeDb = () => {
  if (db) {
    db.close();
    db = null;
  }
};

module.exports = { initDb, getDb, closeDb, runMigrations };
