"use strict";

/**
 * Applications API.
 *
 * POST /api/v1/applications            submit (type: team | beta)
 * POST /api/v1/applications/team       submit a team application
 * POST /api/v1/applications/beta       submit a beta tester application
 * POST /api/v1/applications/discord-verification          DM a code to a guild member
 * POST /api/v1/applications/discord-verification/confirm  check that code
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

const { postApplication, syncApplicationPost } = require("../../lib/discord");
const {
  CODE_FAILURE_MESSAGE,
  describeStartFailure,
  getDiscordVerifier,
  verifyForSubmission,
} = require("../../lib/discord-verification");
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

const verificationStartLimiter = rateLimit({
  windowMs: 10 * 60 * 1000,
  max: 5,
  message: "Too many Discord codes requested from this address. Please wait and try again.",
});

const verificationConfirmLimiter = rateLimit({
  windowMs: 10 * 60 * 1000,
  max: 20,
  message: "Too many code attempts from this address. Please wait and try again.",
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

  const verified = verifyForSubmission(getDiscordVerifier(), body.discordVerification, body.discordCode);
  if (!verified.ok) {
    return sendValidationError(res, [{ field: verified.field, message: verified.message }]);
  }
  result.value.discordId = verified.discordId;
  result.value.discordHandle = verified.username;

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

router.post("/discord-verification", verificationStartLimiter, async (req, res) => {
  const started = await getDiscordVerifier().start(String(req.body?.username || "").trim());
  if (!started.ok) {
    const failure = describeStartFailure(started);
    if (started.retryAfterSeconds) {
      res.set("Retry-After", String(started.retryAfterSeconds));
    }
    if (failure.status === 422) {
      return sendValidationError(res, [{ field: "discordHandle", message: failure.message }]);
    }
    return res.status(failure.status).json({
      error: { code: `discord_verification_${started.code}`, message: failure.message },
    });
  }
  return res.json({
    verificationId: started.verificationId,
    username: started.username,
    expiresInSeconds: started.expiresInSeconds,
  });
});

router.post("/discord-verification/confirm", verificationConfirmLimiter, (req, res) => {
  const confirmed = getDiscordVerifier().confirm(req.body?.verificationId, req.body?.code);
  if (!confirmed.ok) {
    return sendValidationError(res, [{ field: "discordCode", message: CODE_FAILURE_MESSAGE }]);
  }
  return res.json({ verified: true, username: confirmed.username });
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
