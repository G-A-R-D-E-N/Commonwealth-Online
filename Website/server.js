"use strict";

/**
 * Commonwealth Online website server.
 *
 *   npm start        run the site
 *   npm run dev      run with the Node watcher
 *   npm run migrate  apply database migrations and exit
 */

const config = require("./src/config");
const { closeDb } = require("./src/db");
const { createApp } = require("./src/app");
const { startDiscordBot, stopDiscordBot } = require("./src/lib/discord");

const app = createApp();
void startDiscordBot();

const server = app.listen(config.port, config.host, () => {
  console.log(`[site] Commonwealth Online listening on ${config.siteUrl}`);
  console.log(`[site] environment: ${config.env}`);
  console.log(`[site] database: ${config.databasePath}`);
  if (!config.isProduction) {
    console.log(`[site] try http://localhost:${config.port}/`);
  }
});

const shutdown = (signal) => {
  console.log(`\n[site] ${signal} received, shutting down`);
  server.close(() => {
    stopDiscordBot();
    closeDb();
    process.exit(0);
  });

  // Don't hang forever on lingering keep-alive connections.
  setTimeout(() => process.exit(0), 10_000).unref();
};

for (const signal of ["SIGINT", "SIGTERM"]) {
  process.on(signal, () => shutdown(signal));
}

process.on("unhandledRejection", (reason) => {
  console.error("[site] unhandled rejection:", reason);
});

module.exports = server;
