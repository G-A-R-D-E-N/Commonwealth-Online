"use strict";

const express = require("express");

const { getServers } = require("../../lib/servers");

const router = express.Router();

router.get("/", (req, res) => {
  const payload = getServers();

  res.set("Cache-Control", "public, max-age=60");
  res.json({
    ...payload,
    count: payload.servers.length,
  });
});

module.exports = router;
