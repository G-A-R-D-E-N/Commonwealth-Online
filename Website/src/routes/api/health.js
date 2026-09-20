"use strict";

const express = require("express");

const { getDb } = require("../../db");

const router = express.Router();

router.get("/", (req, res) => {
  let database = "ok";
  try {
    getDb().prepare("SELECT 1").get();
  } catch (error) {
    database = "error";
    console.error("[health] database check failed", error);
  }

  const healthy = database === "ok";

  res.status(healthy ? 200 : 503).json({
    status: healthy ? "ok" : "degraded",
    uptimeSeconds: Math.round(process.uptime()),
    time: new Date().toISOString(),
    checks: { database },
  });
});

module.exports = router;
