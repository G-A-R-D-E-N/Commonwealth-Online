"use strict";

/**
 * Application submissions: form schemas, validation, and SQLite helpers.
 *
 * Two public forms share one table, distinguished by `type`:
 *   - team  join the development / support team
 *   - beta  apply as a playtester
 *
 * POST /api/v1/applications (and the HTML forms) both validate through this
 * module. Administrators can read stored rows with GET /api/v1/applications.
 */

const crypto = require("node:crypto");

const { getDb } = require("../db");

const STATUSES = ["pending", "reviewing", "more_info_requested", "accepted", "rejected"];
const SOURCES = ["web", "discord-bot", "api"];
const TYPE_IDS = ["team", "beta"];

const TEAM_ROLES = [
  { value: "development", label: "Development / engineering" },
  { value: "art", label: "Art / UI" },
  { value: "writing", label: "Writing / lore" },
  { value: "community", label: "Community / moderation" },
  { value: "qa", label: "QA / testing" },
  { value: "other", label: "Other" },
];

const GAME_EDITIONS = [
  { value: "anniversary", label: "Fallout 4 Anniversary Edition" },
  { value: "original", label: "Fallout 4 (original release)" },
  { value: "both", label: "Both editions" },
];

const COMMON_COLUMNS = new Set([
  "displayName",
  "email",
  "discordId",
  "discordHandle",
  "timezone",
  "availability",
  "experience",
  "motivation",
  "source",
  "type",
]);

const TYPES = {
  team: {
    id: "team",
    label: "Team application",
    navLabel: "Join the team",
    title: "Join the Commonwealth Online team",
    kicker: "Team",
    description:
      "Help build dedicated servers, player sync, UI, writing or community support. Tell us what you can contribute and how to reach you.",
    submitLabel: "Submit team application",
    fields: [
      {
        key: "displayName",
        label: "Display name",
        type: "text",
        required: true,
        min: 2,
        max: 80,
        autocomplete: "nickname",
        help: "How the team should refer to you.",
      },
      {
        key: "email",
        label: "Email",
        type: "email",
        required: true,
        max: 254,
        autocomplete: "email",
        help: "Used to send application updates and, if you are accepted, onboarding mail.",
      },
      {
        key: "discordHandle",
        label: "Discord username",
        type: "text",
        required: true,
        min: 2,
        max: 64,
        autocomplete: "off",
        help: "Enter your Discord username, not your server display name. We check it against the community server.",
      },
      {
        key: "timezone",
        label: "Timezone",
        type: "text",
        required: false,
        max: 64,
        autocomplete: "off",
        help: "e.g. Pacific Time, UTC+12. Helps when scheduling work sessions.",
      },
      {
        key: "role",
        label: "What would you like to help with?",
        type: "select",
        required: true,
        options: TEAM_ROLES,
      },
      {
        key: "roleOther",
        label: "If other, what would you like to do?",
        type: "text",
        required: false,
        max: 200,
        help: "Only needed if you chose Other above.",
      },
      {
        key: "portfolioUrl",
        label: "Portfolio or links",
        type: "url",
        required: false,
        max: 500,
        autocomplete: "url",
        help: "GitHub, ArtStation, a document — anything that shows your work.",
      },
      {
        key: "experience",
        label: "Relevant experience",
        type: "textarea",
        required: true,
        min: 20,
        max: 2000,
        rows: 6,
        help: "Modding, engineering, art, QA, hosting, community work — whatever applies.",
      },
      {
        key: "availability",
        label: "Availability",
        type: "textarea",
        required: false,
        max: 500,
        rows: 3,
        help: "Roughly when you can contribute, and how many hours a week.",
      },
      {
        key: "motivation",
        label: "Why do you want to join?",
        type: "textarea",
        required: true,
        min: 20,
        max: 4000,
        rows: 7,
        help: "What you want to work on and why Commonwealth Online.",
      },
      {
        key: "ageConfirmed",
        label: "I confirm I am 16 or older.",
        type: "checkbox",
        required: true,
      },
    ],
  },
  beta: {
    id: "beta",
    label: "Beta tester application",
    navLabel: "Become a beta tester",
    title: "Apply as a Commonwealth Online beta tester",
    kicker: "Playtest",
    description:
      "Help us find bugs, break builds and report what actually happens in multiplayer sessions. Testers need Fallout 4, Discord, practical mod-installation experience, and time to write useful reports.",
    submitLabel: "Submit tester application",
    fields: [
      {
        key: "displayName",
        label: "Display name",
        type: "text",
        required: true,
        min: 2,
        max: 80,
        autocomplete: "nickname",
        help: "How we should refer to you.",
      },
      {
        key: "email",
        label: "Email",
        type: "email",
        required: true,
        max: 254,
        autocomplete: "email",
        help: "Used to send tester updates, build notices and related mail.",
      },
      {
        key: "discordHandle",
        label: "Discord username",
        type: "text",
        required: true,
        min: 2,
        max: 64,
        autocomplete: "off",
        help: "Enter your Discord username, not your server display name. We check it against the community server.",
      },
      {
        key: "timezone",
        label: "Timezone",
        type: "text",
        required: false,
        max: 64,
        autocomplete: "off",
        help: "e.g. Pacific Time, UTC+12. Helps when scheduling sessions.",
      },
      {
        key: "gameEdition",
        label: "Which Fallout 4 edition can you run?",
        type: "select",
        required: true,
        options: GAME_EDITIONS,
      },
      {
        key: "manualModInstallConfirmed",
        label: "I have installed Fallout 4 mods manually and can follow manual installation instructions.",
        type: "checkbox",
        required: true,
      },
      {
        key: "modManagerConfirmed",
        label: "I have installed and managed Fallout 4 mods with a mod manager.",
        type: "checkbox",
        required: true,
      },
      {
        key: "logTroubleshootingConfirmed",
        label: "I know where to find Fallout 4 and F4SE logs and can use them to investigate problems.",
        type: "checkbox",
        required: true,
      },
      {
        key: "hardware",
        label: "Hardware notes",
        type: "textarea",
        required: false,
        max: 1000,
        rows: 3,
        help: "Optional. GPU, CPU, or anything that might affect how the game runs.",
      },
      {
        key: "experience",
        label: "Modding and troubleshooting experience",
        type: "textarea",
        required: true,
        min: 80,
        max: 2000,
        rows: 6,
        help: "Describe your manual and mod-manager installs, which manager you use, and an example of using Fallout 4 or F4SE logs to investigate an issue.",
      },
      {
        key: "availability",
        label: "Availability",
        type: "textarea",
        required: true,
        min: 10,
        max: 500,
        rows: 3,
        help: "When you can join playtests, and how often.",
      },
      {
        key: "motivation",
        label: "Why do you want to test?",
        type: "textarea",
        required: true,
        min: 20,
        max: 4000,
        rows: 6,
        help: "What you can contribute as a tester — bug reports, edge cases, regular sessions.",
      },
      {
        key: "confidentialAgreed",
        label:
          "I will keep unreleased builds, bugs and playtest details private until the team says otherwise.",
        type: "checkbox",
        required: true,
      },
      {
        key: "feedbackAgreed",
        label: "I will write useful bug reports and feedback rather than only playing the sessions.",
        type: "checkbox",
        required: true,
      },
      {
        key: "ageConfirmed",
        label: "I confirm I am 16 or older.",
        type: "checkbox",
        required: true,
      },
    ],
  },
};

const asText = (value, max) => {
  const text = String(value ?? "")
    .replace(/\r\n/g, "\n")
    .trim();
  if (!max) {
    return text;
  }
  return text.length > max ? text.slice(0, max) : text;
};

const isEmail = (value) => /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(value);

const isChecked = (value) =>
  value === true || ["true", "on", "1", "yes"].includes(String(value ?? "").trim().toLowerCase());

const isHttpUrl = (value) => {
  try {
    const url = new URL(String(value).includes("://") ? String(value) : `https://${value}`);
    return url.protocol === "http:" || url.protocol === "https:";
  } catch {
    return false;
  }
};

const formatFieldValue = (field, value) => {
  if (value === null || value === undefined || value === "") {
    return null;
  }
  if (field.type === "checkbox") {
    return value ? "Yes" : "No";
  }
  if (field.type === "select") {
    const option = (field.options || []).find((entry) => entry.value === value);
    return option ? option.label : value;
  }
  return value;
};

const buildFields = (type, record) => {
  const typeDef = TYPES[type];
  if (!typeDef) {
    return [];
  }
  return typeDef.fields
    .map((field) => {
      const raw = record[field.key];
      if (raw === null || raw === undefined || raw === "" || raw === false) {
        return null;
      }
      return {
        key: field.key,
        label: field.label,
        type: field.type,
        value: raw,
        displayValue: formatFieldValue(field, raw),
      };
    })
    .filter(Boolean);
};

const HONEYPOT_KEYS = ["website", "fax", "company"];

const isHoneypot = (input = {}) =>
  HONEYPOT_KEYS.some((key) => asText(input[key], 200).length > 0);

const parseAnswers = (raw) => {
  if (!raw) {
    return {};
  }
  try {
    const parsed = JSON.parse(raw);
    return parsed && typeof parsed === "object" && !Array.isArray(parsed) ? parsed : {};
  } catch {
    return {};
  }
};

/**
 * Validates a submission payload against the team or beta schema.
 * @returns {{ok: true, value: object} | {ok: false, errors: Array<{field: string, message: string}>}}
 */
const validateApplication = (input = {}) => {
  const errors = [];
  const type = asText(input.type, 32);
  const typeDef = TYPES[type];

  if (!typeDef) {
    errors.push({
      field: "type",
      message: `type must be one of: ${TYPE_IDS.join(", ")}.`,
    });
    return { ok: false, errors };
  }

  const source = asText(input.source, 32) || "web";
  if (!SOURCES.includes(source)) {
    errors.push({ field: "source", message: `Unknown source "${source}".` });
  }

  const value = { type, source };

  for (const field of typeDef.fields) {
    const raw = input[field.key];

    if (field.type === "checkbox") {
      const checked = isChecked(raw);
      if (field.required && !checked) {
        errors.push({
          field: field.key,
          message: `Please confirm: ${field.label.replace(/\.$/, "")}.`,
        });
      }
      value[field.key] = checked;
      continue;
    }

    const text = asText(raw, field.max || 4000);
    const minLength = field.required ? field.min || 1 : 0;

    if (text.length < minLength) {
      errors.push({
        field: field.key,
        message: field.min
          ? `Please write at least ${field.min} characters.`
          : `${field.label} is required.`,
      });
      continue;
    }

    if (!text) {
      value[field.key] = null;
      continue;
    }

    if (field.type === "email" && !isEmail(text)) {
      errors.push({ field: field.key, message: "That email address does not look valid." });
    }

    if (field.type === "url" && !isHttpUrl(text)) {
      errors.push({ field: field.key, message: "That link does not look valid." });
    }

    if (field.type === "select") {
      const allowed = (field.options || []).map((option) => option.value);
      if (!allowed.includes(text)) {
        errors.push({ field: field.key, message: "Please choose an option." });
      }
    }

    value[field.key] = text;
  }

  if (type === "team" && value.role === "other" && !value.roleOther) {
    errors.push({
      field: "roleOther",
      message: "Please say what you would like to help with.",
    });
  }

  if (errors.length) {
    return { ok: false, errors };
  }

  return { ok: true, value };
};

const toApplication = (row) => {
  const answers = parseAnswers(row.answers);
  const type = TYPE_IDS.includes(row.type) ? row.type : "team";
  const record = {
    id: row.id,
    publicId: row.public_id,
    type,
    typeLabel: TYPES[type].label,
    discordId: row.discord_id,
    discordHandle: row.discord_handle,
    displayName: row.display_name,
    email: row.email,
    timezone: row.timezone,
    availability: row.availability,
    experience: row.experience,
    motivation: row.motivation,
    status: row.status,
    source: row.source,
    discordMessageId: row.discord_message_id,
    reviewedBy: row.reviewed_by,
    reviewedAt: row.reviewed_at,
    createdAt: row.created_at,
    updatedAt: row.updated_at,
  };

  for (const field of TYPES[type].fields) {
    if (COMMON_COLUMNS.has(field.key)) {
      continue;
    }
    if (Object.prototype.hasOwnProperty.call(answers, field.key)) {
      record[field.key] = answers[field.key];
    }
  }

  record.fields = buildFields(type, record);
  return record;
};

const splitValue = (value) => {
  const columns = {
    type: value.type,
    displayName: value.displayName,
    email: value.email,
    discordId: value.discordId || null,
    discordHandle: value.discordHandle,
    timezone: value.timezone || null,
    availability: value.availability || null,
    experience: value.experience || null,
    motivation: value.motivation,
    source: value.source,
  };
  const answers = {};
  for (const [key, entry] of Object.entries(value)) {
    if (COMMON_COLUMNS.has(key) || key === "discordId") {
      continue;
    }
    if (entry === null || entry === undefined || entry === "") {
      continue;
    }
    answers[key] = entry;
  }
  return { columns, answers };
};

const createApplication = (value, meta = {}) => {
  const db = getDb();
  const publicId = crypto.randomUUID();
  const userAgent = asText(meta.userAgent, 300) || null;
  const { columns, answers } = splitValue(value);

  const result = db
    .prepare(
      `INSERT INTO applications (
         public_id, type, discord_id, discord_handle, display_name, email, timezone,
         availability, experience, motivation, answers, source, user_agent
       ) VALUES (
         @publicId, @type, @discordId, @discordHandle, @displayName, @email, @timezone,
         @availability, @experience, @motivation, @answers, @source, @userAgent
       )`
    )
    .run({
      publicId,
      type: columns.type,
      discordId: columns.discordId,
      discordHandle: columns.discordHandle,
      displayName: columns.displayName,
      email: columns.email,
      timezone: columns.timezone,
      availability: columns.availability,
      experience: columns.experience,
      motivation: columns.motivation,
      answers: JSON.stringify(answers),
      source: columns.source,
      userAgent,
    });

  return getApplicationById(result.lastInsertRowid);
};

const getApplicationById = (id) => {
  const row = getDb().prepare("SELECT * FROM applications WHERE id = ?").get(id);
  return row ? toApplication(row) : null;
};

const getApplicationByPublicId = (publicId) => {
  const row = getDb().prepare("SELECT * FROM applications WHERE public_id = ?").get(publicId);
  return row ? toApplication(row) : null;
};

const parseSince = (value) => {
  if (value === undefined || value === null || String(value).trim() === "") {
    return { ok: true, value: null };
  }
  const date = new Date(String(value).trim());
  if (Number.isNaN(date.getTime())) {
    return { ok: false };
  }
  return { ok: true, value: date.toISOString().slice(0, 19).replace("T", " ") };
};

const listApplications = ({ type, status, since, limit = 50, offset = 0 } = {}) => {
  const db = getDb();
  const safeLimit = Math.min(Math.max(Number(limit) || 50, 1), 200);
  const safeOffset = Math.max(Number(offset) || 0, 0);
  const where = [];
  const params = {};

  if (TYPE_IDS.includes(type)) {
    where.push("type = @type");
    params.type = type;
  }

  if (STATUSES.includes(status)) {
    where.push("status = @status");
    params.status = status;
  }

  const sinceResult = parseSince(since);
  if (!sinceResult.ok) {
    return { ok: false, error: "since must be an ISO 8601 timestamp." };
  }
  if (sinceResult.value) {
    where.push("created_at > @since");
    params.since = sinceResult.value;
  }

  const whereSql = where.length ? `WHERE ${where.join(" AND ")}` : "";
  const rows = db
    .prepare(
      `SELECT * FROM applications ${whereSql}
       ORDER BY created_at DESC, id DESC LIMIT @limit OFFSET @offset`
    )
    .all({ ...params, limit: safeLimit, offset: safeOffset });

  const total =
    db.prepare(`SELECT COUNT(*) AS count FROM applications ${whereSql}`).get(params)?.count ?? 0;

  return { ok: true, applications: rows.map(toApplication), total, limit: safeLimit, offset: safeOffset };
};

const setDiscordMessageId = (id, messageId) => {
  if (!messageId) {
    return;
  }
  getDb()
    .prepare("UPDATE applications SET discord_message_id = ?, updated_at = datetime('now') WHERE id = ?")
    .run(messageId, id);
};

const setApplicationStatus = (id, status, reviewedBy = null) => {
  if (!STATUSES.includes(status)) {
    return null;
  }
  getDb()
    .prepare(
      `UPDATE applications
         SET status = ?, reviewed_by = ?, reviewed_at = datetime('now'), updated_at = datetime('now')
       WHERE id = ?`
    )
    .run(status, reviewedBy, id);
  return getApplicationById(id);
};

const getType = (id) => TYPES[id] || null;

module.exports = {
  TYPES,
  TYPE_IDS,
  TEAM_ROLES,
  GAME_EDITIONS,
  STATUSES,
  SOURCES,
  isHoneypot,
  getType,
  validateApplication,
  createApplication,
  getApplicationById,
  getApplicationByPublicId,
  listApplications,
  setApplicationStatus,
  setDiscordMessageId,
  parseSince,
};
