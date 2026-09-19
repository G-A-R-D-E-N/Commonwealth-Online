"use strict";

/**
 * Shared-secret auth for machine-to-machine endpoints (an administrator reading
 * applications, moderation actions, and so on).
 *
 * Browser sessions for the forum are a separate concern and will add their own
 * middleware (Discord OAuth) later - this file is deliberately only about the
 * bot/admin token.
 */

const crypto = require("node:crypto");

const config = require("../config");

const timingSafeEqual = (a, b) => {
  const left = Buffer.from(String(a));
  const right = Buffer.from(String(b));
  if (left.length !== right.length) {
    return false;
  }
  return crypto.timingSafeEqual(left, right);
};

const readToken = (req) => {
  const header = req.get("authorization") || "";
  if (header.toLowerCase().startsWith("bearer ")) {
    return header.slice(7).trim();
  }
  return (req.get("x-admin-token") || "").trim();
};

const requireAdminToken = (req, res, next) => {
  if (!config.adminToken) {
    return res.status(503).json({
      error: {
        code: "admin_token_not_configured",
        message: "Set ADMIN_TOKEN to enable this endpoint.",
      },
    });
  }

  const token = readToken(req);
  if (!token || !timingSafeEqual(token, config.adminToken)) {
    return res.status(401).json({
      error: { code: "unauthorized", message: "A valid admin token is required." },
    });
  }

  return next();
};

module.exports = { requireAdminToken };
