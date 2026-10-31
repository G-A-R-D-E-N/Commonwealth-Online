#!/usr/bin/env node
"use strict";

/**
 * Applies pending database migrations and exits.
 * Usage: npm run migrate
 */

const config = require("../src/config");
const { initDb, closeDb } = require("../src/db");

const main = () => {
  console.log(`[db] ${config.databasePath}`);
  initDb();
  console.log("[db] up to date");
  closeDb();
};

try {
  main();
} catch (error) {
  console.error("[db] migration failed:", error.message);
  process.exitCode = 1;
}
