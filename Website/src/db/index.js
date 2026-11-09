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

const DATA_DIR_MODE = 0o700;
const DATABASE_FILE_MODE = 0o600;
const DATABASE_SIDECAR_SUFFIXES = ["-wal", "-shm"];

let db = null;

const applyMode = (filePath, mode) => {
  if (process.platform === "win32") {
    return;
  }
  try {
    fs.chmodSync(filePath, mode);
  } catch (error) {
    if (error.code === "EPERM" || error.code === "EACCES") {
      console.warn(`[db] could not set mode ${mode.toString(8)} on ${filePath} (${error.code}); leaving it unchanged`);
    } else if (error.code !== "ENOENT") {
      throw error;
    }
  }
};

const ensureDataDirectory = (databasePath) => {
  if (databasePath === ":memory:") {
    return;
  }
  const directory = path.dirname(databasePath);
  // Constraint: mkdirSync returns undefined when it created nothing; an existing directory's mode is never changed.
  const created = fs.mkdirSync(directory, { recursive: true, mode: DATA_DIR_MODE });
  if (created !== undefined) {
    applyMode(directory, DATA_DIR_MODE);
  }
};

const restrictDatabaseFiles = (databasePath) => {
  if (databasePath === ":memory:") {
    return;
  }
  applyMode(databasePath, DATABASE_FILE_MODE);
  for (const suffix of DATABASE_SIDECAR_SUFFIXES) {
    applyMode(`${databasePath}${suffix}`, DATABASE_FILE_MODE);
  }
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
  const previousUmask = process.umask(0o077);
  try {
    db = new Database(config.databasePath);
  } finally {
    process.umask(previousUmask);
  }
  db.pragma("journal_mode = WAL");
  db.pragma("foreign_keys = ON");
  restrictDatabaseFiles(config.databasePath);

  const applied = runMigrations(db);
  restrictDatabaseFiles(config.databasePath);
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
