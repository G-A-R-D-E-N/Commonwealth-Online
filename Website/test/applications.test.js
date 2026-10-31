"use strict";

const assert = require("node:assert/strict");
const dbPath = require.resolve("../src/db");
require.cache[dbPath] = { exports: { getDb: () => null } };
const { TYPES, validateApplication } = require("../src/lib/applications");
const { applicationEmbed, applicationTitle, forumTagName } = require("../src/lib/discord");
const { buildMoreInfoText } = require("../src/lib/mailcow");

const application = { type: "team", source: "web" };
for (const field of TYPES.team.fields) {
  if (field.type === "checkbox") {
    application[field.key] = true;
  } else if (field.type === "select") {
    application[field.key] = field.options[0].value;
  } else if (field.type === "email") {
    application[field.key] = "applicant@example.com";
  } else if (field.type === "url") {
    application[field.key] = "https://example.com";
  } else {
    application[field.key] = "Applicant information that meets the required minimum length.";
  }
}
assert.equal(validateApplication(application).ok, true);
assert.equal(TYPES.team.fields.some((field) => field.key === "discordId"), false);
assert.equal(TYPES.beta.fields.some((field) => field.key === "discordId"), false);
assert.equal(validateApplication({ ...application, discordId: "not-a-discord-id" }).ok, true);
const betaApplication = { type: "beta", source: "web" };
for (const field of TYPES.beta.fields) {
  if (field.type === "checkbox") {
    betaApplication[field.key] = true;
  } else if (field.type === "select") {
    betaApplication[field.key] = field.options[0].value;
  } else if (field.type === "email") {
    betaApplication[field.key] = "applicant@example.com";
  } else {
    betaApplication[field.key] = "Applicant information that meets the required minimum length. ".repeat(2);
  }
}
assert.equal(validateApplication(betaApplication).ok, true);
assert.equal(
  validateApplication({ ...betaApplication, logTroubleshootingConfirmed: false }).ok,
  false
);
assert.match(
  buildMoreInfoText({
    ...application,
    publicId: "test-reference",
    typeLabel: TYPES.team.label,
    createdAt: "2026-09-16 00:00:00",
    fields: [{ label: "Display name", displayValue: application.displayName }],
  }, "Please send your playtest availability."),
  /We need some more information for your Team application\.\n\nPlease send your playtest availability\./
);

assert.equal(
  applicationEmbed({
    ...application,
    publicId: "test-reference",
    typeLabel: TYPES.team.label,
    status: "pending",
    createdAt: "2026-09-16 00:00:00",
    fields: [{ key: "displayName", label: "Display name", displayValue: application.displayName }],
  }).data.fields[0].value,
  "test-reference"
);
assert.equal(
  applicationTitle({ status: "pending", displayName: "Vault Dweller", publicId: "test-reference" }),
  "Pending - Vault Dweller - test-reference"
);
assert.equal(forumTagName("accepted"), "accepted");
assert.equal(forumTagName("rejected"), "rejected");
assert.equal(forumTagName("more_info_requested"), "pending");
