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

const router = express.Router();

router.get("/", (req, res) => {
  res.render("pages/forum", {
    page: {
      title: "Forum - Commonwealth Online",
      description: "Community discussion for the Commonwealth Online multiplayer framework.",
      bodyClass: "co-forum-page",
      activeKey: "forum",
      scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js"],
    },
    enabled: config.features.forum,
  });
});

module.exports = router;
