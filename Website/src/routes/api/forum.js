"use strict";

/**
 * Forum API - read-only scaffolding.
 *
 * Categories come from the database so the schema is wired end to end. Thread
 * and post endpoints land with the feature itself.
 */

const express = require("express");

const { getDb } = require("../../db");

const router = express.Router();

const toCategory = (row) => ({
  id: row.id,
  slug: row.slug,
  name: row.name,
  description: row.description,
  position: row.position,
  isLocked: Boolean(row.is_locked),
  threadCount: row.thread_count ?? 0,
});

/** GET /api/v1/forum/categories */
router.get("/categories", (req, res) => {
  const rows = getDb()
    .prepare(
      `SELECT c.*, (
         SELECT COUNT(*) FROM forum_threads t WHERE t.category_id = c.id
       ) AS thread_count
       FROM forum_categories c
       ORDER BY c.position ASC, c.id ASC`
    )
    .all();

  res.set("Cache-Control", "public, max-age=60");
  res.json({ categories: rows.map(toCategory) });
});

module.exports = router;
