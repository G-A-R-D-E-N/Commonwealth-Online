"use strict";

/**
 * Express application assembly.
 *
 * Layout:
 *   /api/v1/*   JSON API (see src/routes/api)
 *   /           public pages (see src/routes)
 *   /static     front-end CSS/JS, served straight from disk
 *   /assets     images, fonts, branding
 */

const express = require("express");

const config = require("./config");
const { initDb } = require("./db");
const { FOOTER_LINKS, getNavItems } = require("./lib/nav");
const { errorHandler, notFound } = require("./middleware/errors");

const staticOptions = {
  etag: true,
  lastModified: true,
  maxAge: config.isProduction ? "1h" : 0,
};

/**
 * Content Security Policy. The front end loads file-type icons from jsDelivr and
 * talks to the Gitea API directly, so both need to be allowed explicitly.
 */
const buildCsp = () =>
  [
    "default-src 'self'",
    "img-src 'self' data: https://cdn.jsdelivr.net",
    "style-src 'self' 'unsafe-inline'",
    "script-src 'self'",
    "font-src 'self'",
    `connect-src 'self' ${config.gitea.base}`,
    "object-src 'none'",
    "base-uri 'self'",
    "form-action 'self'",
    "frame-ancestors 'none'",
  ].join("; ");

const securityHeaders = (req, res, next) => {
  res.set("X-Content-Type-Options", "nosniff");
  res.set("Referrer-Policy", "strict-origin-when-cross-origin");
  res.set("X-Frame-Options", "DENY");
  res.set("Permissions-Policy", "geolocation=(), microphone=(), camera=()");
  res.set("Content-Security-Policy", buildCsp());
  if (config.isProduction) {
    res.set("Strict-Transport-Security", "max-age=31536000; includeSubDomains");
  }
  next();
};

const createApp = () => {
  // Opens the SQLite connection and applies pending migrations.
  initDb();

  const app = express();

  app.disable("x-powered-by");
  if (config.trustProxy) {
    app.set("trust proxy", 1);
  }

  app.set("view engine", "ejs");
  app.set("views", config.paths.views);

  app.locals.site = {
    name: "Commonwealth Online",
    url: config.siteUrl,
    links: config.links,
    gitea: config.gitea,
  };

  app.use(securityHeaders);
  app.use(express.json({ limit: "100kb" }));
  app.use(express.urlencoded({ extended: false, limit: "100kb" }));

  app.use((req, res, next) => {
    res.locals.navItems = getNavItems();
    res.locals.defaultFooterLinks = FOOTER_LINKS;
    res.locals.features = config.features;
    res.locals.year = new Date().getFullYear();
    next();
  });

  app.use("/static", express.static(config.paths.static, staticOptions));
  app.use("/assets", express.static(config.paths.assets, staticOptions));

  app.use("/api/v1", require("./routes/api"));
  app.use("/", require("./routes/pages"));
  app.use("/forum", require("./routes/forum"));
  app.use("/apply", require("./routes/applications"));

  app.use(notFound);
  app.use(errorHandler);

  return app;
};

module.exports = { createApp };
