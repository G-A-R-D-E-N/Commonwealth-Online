"use strict";

/**
 * Server-side reader for servers/server.json.
 *
 * The file stays the curated source of truth (edited by hand or by the
 * deployment pipeline); this module normalises it into the shape served by
 * GET /api/v1/servers, which is also what the Servers page consumes.
 */

const fs = require("node:fs");

const config = require("../config");

const EMPTY_PAYLOAD = { version: 1, updatedAt: null, servers: [] };

let cache = null;

const normalizeServer = (raw, index) => {
  if (!raw || typeof raw !== "object") {
    return null;
  }

  const host = String(raw.host ?? raw.address ?? "").trim();
  const port = Number(raw.port);
  const name = String(raw.name ?? "").trim();

  if (!host || !name || !Number.isFinite(port) || port <= 0) {
    return null;
  }

  const tags = Array.isArray(raw.tags)
    ? raw.tags.map((tag) => String(tag).trim()).filter(Boolean)
    : [];

  return {
    id: String(raw.id ?? `server-${index + 1}`).trim() || `server-${index + 1}`,
    name,
    host,
    port,
    address: `${host}:${port}`,
    description: String(raw.description ?? "").trim(),
    region: String(raw.region ?? "").trim(),
    tags,
    password: Boolean(raw.password),
    modlist: String(raw.modlist ?? "").trim(),
    discord: String(raw.discord ?? "").trim(),
  };
};

/**
 * Reads the server list, refreshing the cache whenever the file's mtime
 * changes. A missing or malformed file yields an empty payload instead of
 * failing the request - the page already handles an empty list.
 */
const getServers = () => {
  const file = config.paths.serversJson;

  try {
    const stats = fs.statSync(file);
    if (cache && cache.mtimeMs === stats.mtimeMs) {
      return cache.payload;
    }

    const parsed = JSON.parse(fs.readFileSync(file, "utf8"));
    const servers = Array.isArray(parsed?.servers)
      ? parsed.servers.map(normalizeServer).filter(Boolean)
      : [];

    cache = {
      mtimeMs: stats.mtimeMs,
      payload: {
        version: Number(parsed?.version) || 1,
        updatedAt: parsed?.updatedAt ?? null,
        servers,
      },
    };

    return cache.payload;
  } catch (error) {
    if (error.code !== "ENOENT") {
      console.warn(`[servers] could not read ${file}: ${error.message}`);
    }
    return EMPTY_PAYLOAD;
  }
};

module.exports = { getServers };
