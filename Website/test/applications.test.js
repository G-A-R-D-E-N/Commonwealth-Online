"use strict";

const assert = require("node:assert/strict");
const dbPath = require.resolve("../src/db");
require.cache[dbPath] = { exports: { getDb: () => null } };
const { TYPES, validateApplication } = require("../src/lib/applications");
const { answerMessages, applicationEmbed, applicationTitle, forumTagName } = require("../src/lib/discord");
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
const withField = (key, value) => ({ ...application, [key]: value });
const overlong = validateApplication(withField("displayName", "a".repeat(81)));
assert.equal(overlong.ok, false, "an overlong name is refused, not cut");
assert.deepEqual(overlong.errors.map((error) => error.field), ["displayName"]);
assert.equal(validateApplication(withField("displayName", "a".repeat(80))).ok, true, "the exact maximum is accepted");
assert.equal(validateApplication(withField("displayName", "😀".repeat(80))).ok, true, "length counts code points");
assert.equal(validateApplication(withField("displayName", "😀".repeat(81))).ok, false);
assert.equal(validateApplication(withField("email", `${"a".repeat(243)}@example.com`)).ok, false, "an overlong email is refused");
assert.equal(validateApplication(withField("portfolioUrl", `https://example.com/${"a".repeat(500)}`)).ok, false, "an overlong link is refused");
const betaApplication = { type: "beta", source: "web" };
for (const field of TYPES.beta.fields) {
  if (field.type === "checkbox") {
    betaApplication[field.key] = true;
  } else if (field.type === "select") {
    betaApplication[field.key] = field.options[0].value;
  } else if (field.type === "email") {
    betaApplication[field.key] = "applicant@example.com";
  } else {
    betaApplication[field.key] = "Applicant information that meets the required minimum length. ".repeat(2).slice(0, field.max);
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
const longAnswer = "Paragraph one of a long answer. ".repeat(50);
const answerFields = [
  { key: "experience", label: "Relevant experience", displayValue: longAnswer },
  { key: "availability", label: "Availability", displayValue: "A".repeat(1024) },
];
const embedFields = applicationEmbed({
  ...application,
  publicId: "test-reference",
  typeLabel: TYPES.team.label,
  status: "pending",
  createdAt: "2026-09-16 00:00:00",
  fields: answerFields,
}).data.fields;
assert.equal(embedFields.find((field) => field.name === "Relevant experience").value, "The full answer is in the thread below.");
assert.equal(embedFields.find((field) => field.name === "Availability").value.length, 1024, "a 1024-character answer still fits the embed");
const messages = answerMessages({ fields: answerFields });
assert.equal(messages.length, Math.ceil(longAnswer.length / 900));
assert.match(messages[0], /^\*\*Relevant experience\*\* \(1\/2\)\n/);
assert.ok(messages.every((message) => message.length <= 2000), "each follow-up message fits Discord's limit");
assert.equal(messages.map((message) => message.slice(message.indexOf("\n") + 1)).join(""), longAnswer, "the thread gets the whole answer");
assert.deepEqual(answerMessages({ fields: [{ key: "availability", label: "Availability", displayValue: "A".repeat(1024) }] }), []);
assert.equal(forumTagName("accepted"), "accepted");
assert.equal(forumTagName("rejected"), "rejected");
assert.equal(forumTagName("more_info_requested"), "pending");
