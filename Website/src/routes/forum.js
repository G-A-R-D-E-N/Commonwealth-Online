"use strict";

/**
 * Forum pages - scaffolding only.
 *
 * The data model lives in src/db/migrations/001_init.sql (accounts,
 * forum_categories, forum_threads, forum_posts) and the read-only category list
 * is already exposed at GET /api/v1/forum/categories.
 *
 * TODO(forum): thread list, thread view, posting and moderation. Set
 * FEATURE_FORUM=true once the first slice is ready to show in the nav.
 */

const express = require("express");

const config = require("../config");
const { FORUM_PAGE } = require("./page-config");

const router = express.Router();

router.get("/", (req, res) => {
  res.render(FORUM_PAGE.template, {
    page: FORUM_PAGE.page,
    enabled: config.features.forum,
  });
});

module.exports = router;
