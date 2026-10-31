"use strict";

const express = require("express");

const config = require("../../config");
const { getDb } = require("../../db");
const { discordConfigured } = require("../../lib/discord");

const router = express.Router();

router.get("/", (req, res) => {
  let database = "ok";
  try {
    getDb().prepare("SELECT 1").get();
  } catch (error) {
    database = `error: ${error.message}`;
  }

  const healthy = database === "ok";

  res.status(healthy ? 200 : 503).json({
    status: healthy ? "ok" : "degraded",
    environment: config.env,
    uptimeSeconds: Math.round(process.uptime()),
    time: new Date().toISOString(),
    checks: {
      database,
      discordBot: discordConfigured() ? "configured" : "not configured",
    },
  });
});

module.exports = router;
