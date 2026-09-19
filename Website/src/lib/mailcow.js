"use strict";

const nodemailer = require("nodemailer");

const config = require("../config");

const mailConfigured = () =>
  Boolean(config.mailcow.host && config.mailcow.user && config.mailcow.password && config.mailcow.from);

const buildMoreInfoText = (application, requirements) =>
  [
    `Hello ${application.displayName},`,
    "",
    `We need some more information for your ${application.typeLabel || application.type}.`,
    "",
    requirements,
    "",
    "Please reply to this email with the requested information.",
    "",
    "Commonwealth Online",
  ].join("\n");

let transport;

const sendMoreInfoRequest = async (application, requirements) => {
  if (!mailConfigured()) {
    return { ok: false, skipped: true };
  }

  try {
    transport ??= nodemailer.createTransport({
      host: config.mailcow.host,
      port: config.mailcow.port,
      secure: config.mailcow.secure,
      auth: { user: config.mailcow.user, pass: config.mailcow.password },
    });

    const info = await transport.sendMail({
      from: config.mailcow.from,
      to: application.email,
      subject: `More information needed for your Commonwealth Online application`,
      text: buildMoreInfoText(application, requirements),
    });

    return { ok: true, messageId: info.messageId };
  } catch (error) {
    return { ok: false, error: error.message };
  }
};

module.exports = { buildMoreInfoText, mailConfigured, sendMoreInfoRequest };
