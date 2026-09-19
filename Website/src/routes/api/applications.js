"use strict";

/**
 * Applications API.
 *
 * POST /api/v1/applications            submit (type: team | beta)
 * POST /api/v1/applications/team       submit a team application
 * POST /api/v1/applications/beta       submit a beta tester application
 * GET  /api/v1/applications            list stored applications   [admin token]
 * GET  /api/v1/applications/team       list team applications     [admin token]
 * GET  /api/v1/applications/beta       list beta applications     [admin token]
 * GET  /api/v1/applications/:id        fetch one by public id     [admin token]
 * PATCH /api/v1/applications/:id       update status              [admin token]
 *
 * List query: type, status, since (ISO 8601), limit (max 200), offset.
 * Each listed row includes every submitted field plus a `fields` array of
 * { key, label, type, value, displayValue } for Discord formatting.
 *
 * Submissions are written to SQLite first. The Discord bot posts each stored
 * application to the configured private forum when it is available.
 */

const crypto = require("node:crypto");
const express = require("express");

const { isGuildMemberByUsername, postApplication, syncApplicationPost } = require("../../lib/discord");
const {
  STATUSES,
  TYPE_IDS,
  isHoneypot,
  validateApplication,
  createApplication,
  getApplicationByPublicId,
  listApplications,
  setApplicationStatus,
} = require("../../lib/applications");
const { requireAdminToken } = require("../../middleware/auth");
const { clientIp, rateLimit } = require("../../middleware/rate-limit");

const router = express.Router();

const submitLimiter = rateLimit({
  windowMs: 10 * 60 * 1000,
  max: 5,
  message: "Too many application submissions from this address. Please wait and try again.",
});

const sendValidationError = (res, errors) =>
  res.status(422).json({
    error: {
      code: "validation_failed",
      message: "Some fields need attention.",
      details: errors,
    },
  });

const fakeSuccess = () => ({
  application: {
    publicId: crypto.randomUUID(),
    status: "pending",
    createdAt: new Date().toISOString().slice(0, 19).replace("T", " "),
  },
});

const membershipLimiter = rateLimit({
  windowMs: 60 * 1000,
  max: 30,
  message: "Too many Discord checks. Please wait and try again.",
});

const intake = async (req, res, forcedType) => {
  const body = { ...(req.body || {}) };
  if (forcedType) {
    body.type = forcedType;
  }
  if (!body.source) {
    body.source = "web";
  }

  if (isHoneypot(body)) {
    return res.status(201).json(fakeSuccess());
  }

  const result = validateApplication(body);
  if (!result.ok) {
    return sendValidationError(res, result.errors);
  }

  const membership = await isGuildMemberByUsername(result.value.discordHandle);
  if (!membership.ok) {
    return res.status(503).json({
      error: { code: "discord_verification_unavailable", message: "Discord verification is temporarily unavailable." },
    });
  }
  if (!membership.member) {
    return sendValidationError(res, [{ field: "discordHandle", message: "Join the Discord server before applying." }]);
  }

  const application = createApplication(result.value, {
    ip: clientIp(req),
    userAgent: req.get("user-agent"),
  });
  const posted = await postApplication(application);
  if (!posted.ok && !posted.skipped) {
    console.error(`[discord] Application post failed: ${posted.error}`);
  }

  return res.status(201).json({
    application: {
      publicId: application.publicId,
      type: application.type,
      status: application.status,
      createdAt: application.createdAt,
    },
    postedToDiscord: posted.ok,
  });
};

router.get("/discord-membership", membershipLimiter, async (req, res) => {
  const membership = await isGuildMemberByUsername(String(req.query.username || ""));
  if (!membership.ok) {
    return res.status(503).json({
      error: { code: "discord_verification_unavailable", message: "Discord verification is temporarily unavailable." },
    });
  }
  return res.json({ member: membership.member });
});

const listForType = (req, res, forcedType) => {
  const type = forcedType || req.query.type;
  if (type && !TYPE_IDS.includes(String(type))) {
    return sendValidationError(res, [
      { field: "type", message: `type must be one of: ${TYPE_IDS.join(", ")}.` },
    ]);
  }

  const listed = listApplications({
    type: type || undefined,
    status: req.query.status,
    since: req.query.since,
    limit: req.query.limit,
    offset: req.query.offset,
  });

  if (!listed.ok) {
    return sendValidationError(res, [{ field: "since", message: listed.error }]);
  }

  return res.json({
    applications: listed.applications,
    total: listed.total,
    limit: listed.limit,
    offset: listed.offset,
  });
};

router.post("/team", submitLimiter, (req, res) => intake(req, res, "team"));
router.post("/beta", submitLimiter, (req, res) => intake(req, res, "beta"));
router.post("/", submitLimiter, (req, res) => intake(req, res));

router.get("/team", requireAdminToken, (req, res) => listForType(req, res, "team"));
router.get("/beta", requireAdminToken, (req, res) => listForType(req, res, "beta"));
router.get("/", requireAdminToken, (req, res) => listForType(req, res));

router.get("/:publicId", requireAdminToken, (req, res) => {
  const application = getApplicationByPublicId(req.params.publicId);

  if (!application) {
    return res.status(404).json({
      error: { code: "not_found", message: "No application with that reference." },
    });
  }

  return res.json({ application });
});

router.patch("/:publicId", requireAdminToken, async (req, res) => {
  const status = String(req.body?.status || "").trim();

  if (!STATUSES.includes(status)) {
    return res.status(422).json({
      error: {
        code: "validation_failed",
        message: `status must be one of: ${STATUSES.join(", ")}`,
      },
    });
  }

  const existing = getApplicationByPublicId(req.params.publicId);
  if (!existing) {
    return res.status(404).json({
      error: { code: "not_found", message: "No application with that reference." },
    });
  }

  const updated = setApplicationStatus(existing.id, status, req.body?.reviewedBy || null);
  const synced = await syncApplicationPost(updated);
  if (!synced.ok && !synced.skipped) {
    console.error(`[discord] Application post update failed: ${synced.error}`);
  }
  return res.json({ application: updated });
});

module.exports = router;
