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

const MAX_SERVERS = 512;

let cache = null;

const within = (value, maxBytes) => Buffer.byteLength(value, "utf8") <= maxBytes;

const optional = (value, maxBytes) => (within(value, maxBytes) ? value : "");

const endpointText = (host, port) => (host.includes(":") && !host.startsWith("[") ? `[${host}]:${port}` : `${host}:${port}`);

const normalizeServer = (raw, index) => {
  if (!raw || typeof raw !== "object") {
    return null;
  }

  const host = String(raw.host ?? raw.address ?? "").trim();
  const port = Number(raw.port);
  const name = String(raw.name ?? "").trim();

  if (!host || !name || !within(host, 255) || !within(name, 128)) {
    return null;
  }
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    return null;
  }

  const id = String(raw.id ?? "").trim();
  const tags = Array.isArray(raw.tags)
    ? raw.tags
        .map((tag) => String(tag).trim())
        .filter((tag) => tag && within(tag, 64))
        .slice(0, 32)
    : [];

  return {
    id: id && within(id, 256) ? id : `server-${index + 1}`,
    name,
    host,
    port,
    address: endpointText(host, port),
    description: optional(String(raw.description ?? "").trim(), 2048),
    region: optional(String(raw.region ?? "").trim(), 64),
    tags,
    password: Boolean(raw.password),
    modlist: optional(String(raw.modlist ?? "").trim(), 4096),
    discord: optional(String(raw.discord ?? "").trim(), 512),
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
    if (cache && cache.file === file && cache.mtimeMs === stats.mtimeMs) {
      return cache.payload;
    }

    const parsed = JSON.parse(fs.readFileSync(file, "utf8"));
    const servers = Array.isArray(parsed?.servers)
      ? parsed.servers.slice(0, MAX_SERVERS).map(normalizeServer).filter(Boolean)
      : [];

    cache = {
      file,
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
