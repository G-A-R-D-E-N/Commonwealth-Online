"use strict";

/**
 * Application form pages.
 *
 *   GET  /apply           hub: choose team or beta tester
 *   GET  /apply/team      team join form
 *   POST /apply/team      HTML fallback submit
 *   GET  /apply/beta      beta tester form
 *   POST /apply/beta      HTML fallback submit
 *   GET  /apply/thanks    confirmation after a successful submit
 *
 * JavaScript posts JSON to /api/v1/applications. The POST handlers on these
 * routes exist so the forms still work without JS.
 */

const crypto = require("node:crypto");
const express = require("express");

const config = require("../config");
const { postApplication } = require("../lib/discord");
const { describeStartFailure, getDiscordVerifier, verifyForSubmission } = require("../lib/discord-verification");
const {
  getType,
  isHoneypot,
  validateApplication,
  createApplication,
} = require("../lib/applications");
const { clientIp, rateLimit } = require("../middleware/rate-limit");

const router = express.Router();

const submitLimiter = rateLimit({
  windowMs: 10 * 60 * 1000,
  max: 5,
  message: "Too many application submissions from this address. Please wait and try again.",
});

const formScripts = [
  "/static/js/links.js",
  "/static/js/navbar.js",
  "/static/js/script.js",
  "/static/js/applications.js",
];

const toErrorMap = (errors = []) =>
  Object.fromEntries(errors.map((error) => [error.field, error.message]));

const pageLocals = (form, extras = {}) => ({
  page: {
    title: `${form.title} - Commonwealth Online`,
    description: form.description,
    bodyClass: "co-apply-page",
    activeKey: "applications",
    scripts: formScripts,
  },
  form,
  values: extras.values || {},
  errors: extras.errors || [],
  errorMap: toErrorMap(extras.errors || []),
  notice: extras.notice || null,
  verification: extras.verification || null,
  enabled: config.features.applications,
});

router.get("/", (req, res) => {
  res.render("pages/applications", {
    page: {
      title: "Apply - Commonwealth Online",
      description: "Apply to join the Commonwealth Online team or become a beta tester.",
      bodyClass: "co-apply-page",
      activeKey: "applications",
      scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js"],
    },
    enabled: config.features.applications,
  });
});

router.get("/thanks", (req, res) => {
  const reference = String(req.query.ref || "").trim();
  res.render("pages/apply-thanks", {
    page: {
      title: "Application received - Commonwealth Online",
      description: "Your Commonwealth Online application has been received.",
      bodyClass: "co-apply-page",
      activeKey: "applications",
      scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js"],
    },
    reference,
  });
});

router.get("/team", (req, res) => {
  res.render("pages/apply-form", pageLocals(getType("team")));
});

router.get("/beta", (req, res) => {
  res.render("pages/apply-form", pageLocals(getType("beta")));
});

const handleHtmlSubmit = async (req, res, type) => {
  const form = getType(type);
  const body = { ...(req.body || {}), type, source: "web" };

  if (isHoneypot(body)) {
    return res.redirect(303, `/apply/thanks?ref=${encodeURIComponent(crypto.randomUUID())}`);
  }

  const result = validateApplication(body);
  if (!result.ok) {
    return res.status(422).render(
      "pages/apply-form",
      pageLocals(form, {
        values: body,
        errors: result.errors,
        notice: "Some fields need attention before the application can be sent.",
      })
    );
  }

  const verifier = getDiscordVerifier();
  if (!body.discordVerification || body.discordResend) {
    const started = await verifier.start(result.value.discordHandle);
    if (!started.ok) {
      const failure = describeStartFailure(started);
      return res.status(failure.status).render(
        "pages/apply-form",
        pageLocals(form, {
          values: body,
          errors: [{ field: "discordHandle", message: failure.message }],
          notice: failure.message,
        })
      );
    }
    return res.status(200).render(
      "pages/apply-form",
      pageLocals(form, {
        values: { ...body, discordHandle: started.username },
        verification: { id: started.verificationId },
        notice: "We sent a 6-digit code to your Discord DMs. Enter it below and send the application again.",
      })
    );
  }

  const verified = verifyForSubmission(verifier, body.discordVerification, body.discordCode);
  if (!verified.ok) {
    const keepVerification = verified.field === "discordCode";
    return res.status(422).render(
      "pages/apply-form",
      pageLocals(form, {
        values: body,
        verification: keepVerification ? { id: String(body.discordVerification) } : null,
        errors: [{ field: verified.field, message: verified.message }],
        notice: verified.message,
      })
    );
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

  return res.redirect(303, `/apply/thanks?ref=${encodeURIComponent(application.publicId)}`);
};

router.post("/team", submitLimiter, (req, res) => handleHtmlSubmit(req, res, "team"));
router.post("/beta", submitLimiter, (req, res) => handleHtmlSubmit(req, res, "beta"));

module.exports = router;
