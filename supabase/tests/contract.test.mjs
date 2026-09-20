import assert from "node:assert/strict";
import fs from "node:fs";
import { createRequire } from "node:module";
import path from "node:path";
import { fileURLToPath } from "node:url";

import {
  buildDiscordPayload,
  scheduleDiscordNotification,
} from "../functions/submit-application/discord.mjs";
import { validateApplication } from "../functions/submit-application/validation.mjs";

const require = createRequire(import.meta.url);
const { TYPES, TYPE_IDS } = require("../../Website/src/lib/applications.js");

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const migration = fs.readFileSync(path.join(root, "migrations", "20260920032942_forum.sql"), "utf8");
const applicationMigration = fs.readFileSync(
  path.join(root, "migrations", "20260920032951_applications.sql"),
  "utf8"
);
const securityMigration = fs.readFileSync(
  path.join(root, "migrations", "20260920033039_security_hardening.sql"),
  "utf8"
);
const submitApplication = fs.readFileSync(
  path.join(root, "functions", "submit-application", "index.ts"),
  "utf8"
);
const reviewApplication = fs.readFileSync(
  path.join(root, "functions", "review-application", "index.ts"),
  "utf8"
);
const intakeControlsMigration = fs.readFileSync(
  path.join(root, "migrations", "20260920052401_application_intake_controls.sql"),
  "utf8"
);
const config = fs.readFileSync(path.join(root, "config.toml"), "utf8");
const envExample = fs.readFileSync(path.join(root, ".env.example"), "utf8");

for (const table of [
  "profiles",
  "forum_categories",
  "forum_threads",
  "forum_posts",
  "forum_reactions",
  "forum_reports",
]) {
  assert.match(
    migration,
    new RegExp(`alter table public\\.${table} enable row level security;`, "i"),
    `RLS must be enabled on ${table}`
  );
}

for (const fragment of [
  "grant select on public.profiles to anon, authenticated;",
  "grant select on public.forum_categories to anon, authenticated;",
  "grant select on public.forum_threads to anon, authenticated;",
  "grant select on public.forum_posts to anon, authenticated;",
  "grant select on public.forum_reactions to anon, authenticated;",
  "grant insert (post_id, reason) on public.forum_reports to authenticated;",
  "security definer",
  "set search_path = ''",
]) {
  assert.ok(migration.toLowerCase().includes(fragment.toLowerCase()), `missing security contract: ${fragment}`);
}

assert.match(config, /\[auth\.external\.discord\]/);
assert.match(config, /client_id = "env\(SUPABASE_AUTH_EXTERNAL_DISCORD_CLIENT_ID\)"/);
assert.match(config, /secret = "env\(SUPABASE_AUTH_EXTERNAL_DISCORD_SECRET\)"/);
assert.match(config, /\[functions\.submit-application\]/);
assert.match(config, /verify_jwt = false/);
assert.match(applicationMigration, /alter table public\.applications enable row level security;/i);
assert.match(applicationMigration, /revoke all on public\.applications from anon, authenticated;/i);
assert.match(submitApplication, /auth: "publishable"/);
assert.match(submitApplication, /ctx\.supabaseAdmin/);
assert.match(submitApplication, /DISCORD_BOT_TOKEN/);
assert.match(submitApplication, /DISCORD_GUILD_ID/);
assert.match(submitApplication, /method === "OPTIONS"/);
assert.match(submitApplication, /Access-Control-Allow-Origin/);
assert.match(submitApplication, /DISCORD_APPLICATION_WEBHOOK_URL/);
assert.match(submitApplication, /scheduleDiscordNotification/);
assert.match(submitApplication, /consume_application_rate_limit/);
assert.match(submitApplication, /select\("public_id,type,discord_handle/);
assert.doesNotMatch(submitApplication, /\.select\("\*"/);
assert.doesNotMatch(reviewApplication, /\.select\("\*"/);
assert.match(reviewApplication, /APPLICATION_REVIEW_TOKEN/);
assert.match(reviewApplication, /review_note/);
assert.match(reviewApplication, /more_info_requested/);
assert.match(intakeControlsMigration, /enable row level security/i);
assert.match(intakeControlsMigration, /consume_application_rate_limit/);
assert.match(intakeControlsMigration, /grant execute on function public\.consume_application_rate_limit.*service_role/i);
assert.match(envExample, /^SUPABASE_AUTH_EXTERNAL_DISCORD_CLIENT_ID=$/m);
assert.match(envExample, /^SUPABASE_AUTH_EXTERNAL_DISCORD_SECRET=$/m);
assert.match(envExample, /^DISCORD_APPLICATION_WEBHOOK_URL=$/m);
assert.match(envExample, /^APPLICATION_REVIEW_TOKEN=$/m);
assert.match(envExample, /^APPLICATION_CORS_ORIGINS=/m);
for (const index of [
  "forum_threads_author_idx",
  "forum_posts_author_idx",
  "forum_reactions_user_idx",
  "forum_reports_post_idx",
  "forum_reports_reporter_idx",
]) {
  assert.match(securityMigration, new RegExp(`create index if not exists ${index}`, "i"));
}
assert.match(securityMigration, /revoke all on function public\.handle_new_user\(\) from public/i);
assert.match(securityMigration, /grant execute on function public\.handle_new_user\(\) to service_role/i);
assert.match(securityMigration, /grant execute on function public\.is_forum_moderator\(\) to authenticated, service_role/i);

const mentionPayload = buildDiscordPayload({
  type: "team",
  status: "pending",
  display_name: "@everyone",
  public_id: "application-reference",
});
assert.equal(mentionPayload.embeds[0].title.includes("@everyone"), true);
assert.equal(mentionPayload.content.includes("@everyone"), false);
assert.equal(mentionPayload.thread_name, "Pending - ＠everyone - application-reference");
assert.deepEqual(mentionPayload.allowed_mentions, { parse: [] });

const fieldValue = (field) => {
  if (field.type === "checkbox") return true;
  if (field.type === "select") return field.options[0].value;
  if (field.type === "email") return "applicant@example.com";
  if (field.type === "url") return "https://example.com";
  return "x".repeat(Math.max(field.min || 2, 2));
};

const applicationFor = (type) => ({
  type,
  source: "web",
  ...Object.fromEntries(TYPES[type].fields.map((field) => [field.key, fieldValue(field)])),
});

for (const type of TYPE_IDS) {
  const application = applicationFor(type);
  assert.equal(validateApplication(application).ok, true, `${type} schema accepts its valid form`);
  for (const field of TYPES[type].fields.filter((field) => field.required)) {
    const invalid = { ...application, [field.key]: field.type === "checkbox" ? false : "" };
    const result = validateApplication(invalid);
    assert.equal(result.ok, false, `${type}.${field.key} is required by the Edge validator`);
    assert.ok(result.errors.some((error) => error.field === field.key));
  }
  for (const field of TYPES[type].fields.filter((field) => field.type === "select")) {
    const result = validateApplication({ ...application, [field.key]: "invalid-option" });
    assert.equal(result.ok, false, `${type}.${field.key} rejects unknown options`);
  }
}

const shortBeta = applicationFor("beta");
shortBeta.availability = "short";
assert.equal(validateApplication(shortBeta).ok, false);
assert.ok(validateApplication(shortBeta).errors.some((error) => error.field === "availability"));

let releaseFetch;
const delayedFetch = new Promise((resolve) => {
  releaseFetch = resolve;
});
let trackedNotification;
let notificationStarted = false;
scheduleDiscordNotification(
  { type: "team", display_name: "Applicant", public_id: "application-reference" },
  "https://discord.example/webhook",
  (promise) => {
    trackedNotification = promise;
  },
  () => {
    notificationStarted = true;
    return delayedFetch;
  },
  () => {},
);
assert.equal(notificationStarted, true);
assert.equal(trackedNotification instanceof Promise, true);
releaseFetch({ ok: true });
await trackedNotification;

for (const [name, content] of [
  ["forum migration", migration],
  ["application migration", applicationMigration],
  ["submit function", submitApplication],
  ["security migration", securityMigration],
  ["config", config],
  ["env example", envExample],
]) {
  assert.doesNotMatch(content, /sb_secret_[A-Za-z0-9_-]+/, `${name} contains a Supabase secret key`);
  assert.doesNotMatch(content, /postgres(?:ql)?:\/\/[^\s"']+:[^\s"']+@/i, `${name} contains a database URI`);
  assert.doesNotMatch(content, /SUPABASE_ACCESS_TOKEN\s*=\s*\S+/i, `${name} contains an access token`);
}

console.log("supabase contract checks passed");
