"use strict";

/**
 * Public pages. Each view receives a `page` object with the per-page locals
 * used by the shared partials (title, description, body class, active nav key
 * and any page-specific scripts).
 */

const express = require("express");

const { LEGACY_REDIRECTS, PUBLIC_PAGES } = require("./page-config");

const router = express.Router();

for (const { route, template, page } of PUBLIC_PAGES) {
  router.get(route, (req, res) => res.render(template, { page }));
}

for (const [from, to] of Object.entries(LEGACY_REDIRECTS)) {
  router.get(from, (req, res) => res.redirect(301, to));
}

module.exports = router;
