"use strict";

/**
 * Versioned API surface, mounted at /api/v1 by src/app.js.
 *
 * Everything the site and its administrators need should be reachable through
 * these routes rather than the page URLs.
 */

const express = require("express");

const config = require("../../config");

const router = express.Router();

router.get("/", (req, res) => {
  res.json({
    name: "Commonwealth Online API",
    version: "v1",
    environment: config.env,
    site: config.siteUrl,
    endpoints: {
      health: "/api/v1/health",
      servers: "/api/v1/servers",
      forumCategories: "/api/v1/forum/categories",
      applications: "/api/v1/applications",
      teamApplications: "/api/v1/applications/team",
      betaApplications: "/api/v1/applications/beta",
    },
  });
});

router.use("/health", require("./health"));
router.use("/servers", require("./servers"));
router.use("/forum", require("./forum"));
router.use("/applications", require("./applications"));

module.exports = router;
