"use strict";

const express = require("express");

const { getServers } = require("../../lib/servers");

const router = express.Router();

/**
 * GET /api/v1/servers
 * Shape matches servers/server.json so the Servers page can render it directly.
 */
router.get("/", (req, res) => {
  const payload = getServers();

  res.set("Cache-Control", "public, max-age=60");
  res.json({
    ...payload,
    count: payload.servers.length,
  });
});

module.exports = router;
