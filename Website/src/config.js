"use strict";

/**
 * Central runtime configuration.
 *
 * Values come from the environment, with local-development defaults so the site
 * runs with no setup. A `.env` file at the project root is loaded when present
 * (Node's built-in loader - no dotenv dependency).
 */

const fs = require("node:fs");
const path = require("node:path");

const ROOT_DIR = path.resolve(__dirname, "..");

const envFile = path.join(ROOT_DIR, ".env");
if (fs.existsSync(envFile) && typeof process.loadEnvFile === "function") {
  process.loadEnvFile(envFile);
}

const toBool = (value, fallback = false) => {
  if (value === undefined || value === null || String(value).trim() === "") {
    return fallback;
  }
  return ["1", "true", "yes", "on"].includes(String(value).trim().toLowerCase());
};

const toInt = (value, fallback) => {
  const parsed = Number.parseInt(String(value ?? ""), 10);
  return Number.isFinite(parsed) ? parsed : fallback;
};

const trimTrailingSlashes = (value) => String(value || "").replace(/\/+$/, "");

const env = process.env.NODE_ENV || "development";
const host = process.env.HOST || "127.0.0.1";
const port = toInt(process.env.PORT, 3000);

/**
 * Site links mirrored in static/js/links.js (browser side). Keep both in sync
 * until the nav is fully server driven.
 */
const links = {
  github: "https://git.zambazosmedia.group/Commonwealth-Online",
  repository: "https://git.zambazosmedia.group/Commonwealth-Online/Commonwealth-Online-Public",
  discord: "https://discord.gg/GyfxYG2gzH",
};

const config = {
  env,
  isProduction: env === "production",
  host,
  port,
  siteUrl: trimTrailingSlashes(process.env.SITE_URL) || `http://${host}:${port}`,
  databasePath: path.resolve(ROOT_DIR, process.env.DATABASE_PATH || "data/commonwealth-online.sqlite"),

  /** Toggle unfinished surfaces. Pages stay reachable, but stay out of the nav. */
  features: {
    forum: toBool(process.env.FEATURE_FORUM, false),
    applications: toBool(process.env.FEATURE_APPLICATIONS, true),
  },

  discord: {
    botToken: String(process.env.DISCORD_BOT_TOKEN || "").trim(),
    guildId: String(process.env.DISCORD_GUILD_ID || "").trim(),
    applicationsForumChannelIds: {
      default: String(process.env.DISCORD_APPLICATIONS_FORUM_CHANNEL_ID || "").trim(),
      team: String(process.env.DISCORD_TEAM_APPLICATIONS_FORUM_CHANNEL_ID || "").trim(),
      beta: String(process.env.DISCORD_BETA_APPLICATIONS_FORUM_CHANNEL_ID || "").trim(),
    },
    reviewerRoleId: String(process.env.DISCORD_APPLICATION_REVIEWER_ROLE_ID || "").trim(),
    roles: {
      team: String(process.env.DISCORD_TEAM_ROLE_ID || "").trim(),
      beta: String(process.env.DISCORD_BETA_ROLE_ID || "").trim(),
    },
  },

  mailcow: {
    host: String(process.env.MAILCOW_SMTP_HOST || "").trim(),
    port: toInt(process.env.MAILCOW_SMTP_PORT, 465),
    secure: toBool(process.env.MAILCOW_SMTP_SECURE, true),
    user: String(process.env.MAILCOW_SMTP_USER || "").trim(),
    password: String(process.env.MAILCOW_SMTP_PASSWORD || ""),
    from: String(process.env.MAILCOW_FROM || process.env.MAILCOW_SMTP_USER || "").trim(),
  },

  /** Shared secret for admin reads of stored applications. */
  adminToken: String(process.env.ADMIN_TOKEN || "").trim(),

  /** Set when running behind nginx/Cloudflare so client IPs are read correctly. */
  trustProxy: toBool(process.env.TRUST_PROXY, false),

  gitea: {
    base: trimTrailingSlashes(process.env.GITEA_BASE) || "https://git.zambazosmedia.group",
    owner: process.env.GITEA_OWNER || "Commonwealth-Online",
    repo: process.env.GITEA_REPO || "Commonwealth-Online-Public",
  },

  links,

  paths: {
    root: ROOT_DIR,
    assets: path.join(ROOT_DIR, "assets"),
    static: path.join(ROOT_DIR, "static"),
    data: path.join(ROOT_DIR, "data"),
    views: path.join(ROOT_DIR, "views"),
    serversJson: path.join(ROOT_DIR, "servers", "server.json"),
  },
};

module.exports = config;
