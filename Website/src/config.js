"use strict";

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

const trimTrailingSlashes = (value) => String(value || "").replace(/\/+$/, "");

const links = {
  github: "https://git.zambazosmedia.group/Commonwealth-Online",
  repository: "https://git.zambazosmedia.group/Commonwealth-Online/Commonwealth-Online-Public",
  discord: "https://discord.gg/GyfxYG2gzH",
};

const config = {
  siteUrl: trimTrailingSlashes(process.env.SITE_URL) || "http://localhost:3000",
  supabase: {
    url: trimTrailingSlashes(process.env.SUPABASE_URL),
    publishableKey: String(process.env.SUPABASE_PUBLISHABLE_KEY || "").trim(),
  },

  features: {
    forum: toBool(process.env.FEATURE_FORUM, false),
    applications: toBool(process.env.FEATURE_APPLICATIONS, true),
  },

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
    views: path.join(ROOT_DIR, "views"),
  },
};

module.exports = config;
