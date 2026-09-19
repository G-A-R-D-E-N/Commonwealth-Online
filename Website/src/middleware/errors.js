"use strict";

/**
 * Shared 404 and error handling. API routes get JSON, page routes get the
 * styled error view.
 */

const config = require("../config");

const wantsJson = (req) =>
  req.path.startsWith("/api/") || req.accepts(["html", "json"]) === "json";

const renderErrorPage = (res, status, { title, description, heading, message }) =>
  res.status(status).render("pages/error", {
    page: {
      title,
      description,
      bodyClass: "co-error-page",
      activeKey: "",
      scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js"],
    },
    status,
    heading,
    message,
  });

const notFound = (req, res) => {
  if (wantsJson(req)) {
    return res.status(404).json({
      error: { code: "not_found", message: `No route for ${req.method} ${req.originalUrl}` },
    });
  }

  return renderErrorPage(res, 404, {
    title: "Not found - Commonwealth Online",
    description: "The page you requested does not exist.",
    heading: "Lost in the wastes",
    message: "That page does not exist. Check the address, or head back to the home page.",
  });
};

// eslint-disable-next-line no-unused-vars -- Express detects error handlers by arity.
const errorHandler = (error, req, res, next) => {
  const status = Number(error.status) || 500;

  if (status >= 500) {
    console.error(`[error] ${req.method} ${req.originalUrl}`, error);
  }

  const message =
    status >= 500 && config.isProduction
      ? "Something went wrong on our end."
      : error.message || "Something went wrong.";

  if (res.headersSent) {
    return res.end();
  }

  if (wantsJson(req)) {
    return res.status(status).json({ error: { code: error.code || "server_error", message } });
  }

  return renderErrorPage(res, status, {
    title: "Server error - Commonwealth Online",
    description: "An unexpected error occurred.",
    heading: status === 500 ? "Signal lost" : "Request rejected",
    message,
  });
};

module.exports = { notFound, errorHandler, wantsJson };
