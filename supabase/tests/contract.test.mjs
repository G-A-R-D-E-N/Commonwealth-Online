import assert from "node:assert/strict";
import fs from "node:fs";
import { createRequire } from "node:module";
import path from "node:path";
import { fileURLToPath } from "node:url";

import {
  buildDiscordPayload,
  buildReviewUpdatePayload,
  scheduleDiscordNotification,
} from "../functions/submit-application/discord.mjs";
import { createCors } from "../functions/submit-application/cors.mjs";
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
const corsImplementation = fs.readFileSync(
  path.join(root, "functions", "submit-application", "cors.mjs"),
  "utf8"
);
const intakeControlsMigration = fs.readFileSync(
  path.join(root, "migrations", "20260920052401_application_intake_controls.sql"),
  "utf8"
);
const accountProfilesMigration = fs.readFileSync(
  path.join(root, "migrations", "20260921031102_account_profiles.sql"),
  "utf8"
);
const realProfileIconsMigration = fs.readFileSync(
  path.join(root, "migrations", "20260921035009_real_profile_icons.sql"),
  "utf8"
);
const localProfileIconsMigration = fs.readFileSync(
  path.join(root, "migrations", "20260921040009_local_profile_icons.sql"),
  "utf8"
);
const factionProfileIconsMigration = fs.readFileSync(
  path.join(root, "migrations", "20260921050000_faction_profile_icons.sql"),
  "utf8"
);
const userAccountsMigration = fs.readFileSync(
  path.join(root, "migrations", "20260921143413_user_accounts_admin_table.sql"),
  "utf8"
);
const userAccountsLockMigration = fs.readFileSync(
  path.join(root, "migrations", "20260921143532_lock_user_account_sync_functions.sql"),
  "utf8"
);
const signupAbuseMigration = fs.readFileSync(
  path.join(root, "migrations", "20260921145547_signup_abuse_controls.sql"),
  "utf8"
);
const signupCleanupFixMigration = fs.readFileSync(
  path.join(root, "migrations", "20260921151049_fix_signup_cleanup_return_types.sql"),
  "utf8"
);
const socialFoundationMigration = fs.readFileSync(
  path.join(root, "migrations", "20260921181152_user_social_foundation.sql"),
  "utf8"
);
const socialPrivacyMigration = fs.readFileSync(
  path.join(root, "migrations", "20260921181157_social_privacy_hardening.sql"),
  "utf8"
);
const factionFoundationMigration = fs.readFileSync(
  path.join(root, "migrations", "20260921181243_faction_foundation.sql"),
  "utf8"
);
const factionMembershipMigration = fs.readFileSync(
  path.join(root, "migrations", "20260921181247_faction_membership.sql"),
  "utf8"
);
const usernameHistoryMigration = fs.readFileSync(
  path.join(root, "migrations", "20260921181222_username_history.sql"),
  "utf8"
);
const userBadgesMigration = fs.readFileSync(
  path.join(root, "migrations", "20260921181227_user_badges.sql"),
  "utf8"
);
const userServerIdentityMigration = fs.readFileSync(
  path.join(root, "migrations", "20260921181232_user_server_identity.sql"),
  "utf8"
);
const userCharactersMigration = fs.readFileSync(
  path.join(root, "migrations", "20260921181238_user_characters.sql"),
  "utf8"
);
const rpcHardeningMigration = fs.readFileSync(
  path.join(root, "migrations", "20260921194035_harden_public_rpc_execution_and_backend_rls.sql"),
  "utf8"
);
const factionPolicyExecutionMigration = fs.readFileSync(
  path.join(root, "migrations", "20260921194214_fix_faction_manager_policy_execution.sql"),
  "utf8"
);
const factionReviewGuardsMigration = fs.readFileSync(
  path.join(root, "migrations", "20260921204500_admin_faction_review_guards.sql"),
  "utf8"
);
const registerAccount = fs.readFileSync(
  path.join(root, "functions", "register-account", "index.ts"),
  "utf8"
);
const config = fs.readFileSync(path.join(root, "config.toml"), "utf8");
const confirmationTemplate = fs.readFileSync(path.join(root, "templates", "confirmation.html"), "utf8");
const envExample = fs.readFileSync(path.join(root, ".env.example"), "utf8");
const deployWorkflow = fs.readFileSync(
  path.join(root, "..", ".github", "workflows", "supabase-deploy.yml"),
  "utf8",
);

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

assert.match(config, /^enable_manual_linking = true$/m);
assert.match(config, /\[auth\.email\]/);
assert.match(config, /^enable_signup = true$/m);
assert.match(config, /^enable_confirmations = true$/m);
assert.match(config, /\[auth\.email\.template\.confirmation\]/);
assert.match(config, /subject = "Confirm your Commonwealth Online account"/);
assert.match(config, /content_path = "\.\/supabase\/templates\/confirmation\.html"/);
assert.match(confirmationTemplate, /Commonwealth Online/);
assert.match(confirmationTemplate, /\{\{ \.ConfirmationURL \}\}/);
assert.match(confirmationTemplate, /\{\{ if \.Data\.display_name \}\}/);
assert.match(config, /http:\/\/127\.0\.0\.1:3000\/account\//);
assert.match(config, /https:\/\/g-a-r-d-e-n\.github\.io\/Commonwealth-Online\/account\//);
assert.match(config, /https:\/\/commonwealth-online\.com\/account\//);
assert.match(config, /\[auth\.external\.discord\]/);
assert.match(config, /client_id = "env\(SUPABASE_AUTH_EXTERNAL_DISCORD_CLIENT_ID\)"/);
assert.match(config, /secret = "env\(SUPABASE_AUTH_EXTERNAL_DISCORD_SECRET\)"/);
assert.match(config, /\[functions\.submit-application\]/);
assert.match(config, /\[functions\.register-account\]/);
assert.match(config, /verify_jwt = false/);
assert.match(applicationMigration, /alter table public\.applications enable row level security;/i);
assert.match(applicationMigration, /revoke all on public\.applications from anon, authenticated;/i);
assert.match(submitApplication, /auth: "publishable"/);
assert.match(submitApplication, /ctx\.supabaseAdmin/);
assert.match(submitApplication, /DISCORD_BOT_TOKEN/);
assert.match(submitApplication, /DISCORD_GUILD_ID/);
assert.match(corsImplementation, /Access-Control-Allow-Origin/);
assert.match(corsImplementation, /request.method === "OPTIONS"/);
assert.match(submitApplication, /cors: "disabled"/);
assert.match(submitApplication, /fetch: withCors\(handler\)/);
assert.match(submitApplication, /DISCORD_APPLICATION_WEBHOOK_URL/);
assert.match(submitApplication, /scheduleDiscordNotification/);
assert.match(submitApplication, /consume_application_rate_limit/);
assert.match(submitApplication, /select\("public_id,type,discord_handle/);
assert.doesNotMatch(submitApplication, /\.select\("\*"/);
assert.doesNotMatch(reviewApplication, /\.select\("\*"/);
assert.match(reviewApplication, /APPLICATION_REVIEW_TOKEN/);
assert.match(reviewApplication, /review_note/);
assert.match(reviewApplication, /more_info_requested/);
assert.match(reviewApplication, /buildReviewUpdatePayload/);
assert.match(reviewApplication, /cors: "disabled"/);
assert.match(reviewApplication, /fetch: withCors\(handler\)/);
assert.match(deployWorkflow, /APPLICATION_REVIEW_TOKEN: \$\{\{ secrets\.APPLICATION_REVIEW_TOKEN \}\}/);
assert.match(deployWorkflow, /supabase secrets set APPLICATION_REVIEW_TOKEN=/);
assert.doesNotMatch(deployWorkflow, /APPLICATION_REVIEW_TOKEN: \$\{\{ vars\./);
assert.match(intakeControlsMigration, /enable row level security/i);
assert.match(intakeControlsMigration, /consume_application_rate_limit/);
assert.match(intakeControlsMigration, /grant execute on function public\.consume_application_rate_limit.*service_role/i);
assert.match(envExample, /^SUPABASE_AUTH_EXTERNAL_DISCORD_CLIENT_ID=$/m);
assert.match(envExample, /^SUPABASE_AUTH_EXTERNAL_DISCORD_SECRET=$/m);
assert.match(envExample, /^DISCORD_APPLICATION_WEBHOOK_URL=$/m);
assert.match(envExample, /^APPLICATION_REVIEW_TOKEN=$/m);
assert.match(envExample, /^APPLICATION_CORS_ORIGINS=/m);
assert.match(envExample, /^ACCOUNT_CORS_ORIGINS=/m);
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
assert.match(accountProfilesMigration, /profiles_avatar_url_allowed/i);
assert.match(accountProfilesMigration, /vault-dweller\.svg/i);
assert.match(accountProfilesMigration, /atom-cat\.svg/i);
assert.match(accountProfilesMigration, /new\.raw_user_meta_data ->> 'display_name'/i);
assert.doesNotMatch(accountProfilesMigration, /nullif\(new\.raw_user_meta_data ->> 'picture'/i);
assert.match(realProfileIconsMigration, /profiles_avatar_url_allowed/i);
assert.match(realProfileIconsMigration, /918547cc872c3288122f9d15ed0416cf33aa8bbf/i);
for (const icon of ["armorer.png", "hacker.png", "rifleman.png", "medic.png", "scrapper.png", "cap_collector.png"]) {
  assert.ok(realProfileIconsMigration.includes(icon), `missing real profile icon contract: ${icon}`);
}
assert.doesNotMatch(realProfileIconsMigration, /nullif\(new\.raw_user_meta_data ->> 'picture'/i);
assert.match(localProfileIconsMigration, /profiles_avatar_url_allowed/i);
for (const icon of ["armorer.png", "hacker.png", "rifleman.png", "medic.png", "scrapper.png", "cap_collector.png"]) {
  assert.ok(localProfileIconsMigration.includes(`/assets/profile-icons/${icon}`), `missing local profile icon contract: ${icon}`);
}
assert.doesNotMatch(localProfileIconsMigration, /nullif\(new\.raw_user_meta_data ->> 'picture'/i);
assert.match(factionProfileIconsMigration, /profiles_avatar_url_allowed/i);
for (const icon of ["armorer.png", "hacker.png", "rifleman.png", "medic.png", "scrapper.png", "cap_collector.png"]) {
  assert.ok(
    factionProfileIconsMigration.includes(`/assets/profile-icons/${icon}`),
    `faction profile icons must preserve existing icon: ${icon}`,
  );
}
for (const icon of ["Brotherhood", "Institute", "Minutemen", "Railroad"]) {
  assert.ok(
    factionProfileIconsMigration.includes(`/assets/profile-images/Icon__${icon}.png`),
    `missing faction profile icon contract: ${icon}`,
  );
}
assert.doesNotMatch(factionProfileIconsMigration, /nullif\(new\.raw_user_meta_data ->> 'picture'/i);

assert.match(userAccountsMigration, /create table if not exists public\.user_accounts/i);
assert.match(userAccountsMigration, /alter table public\.user_accounts enable row level security/i);
assert.match(userAccountsMigration, /revoke all on table public\.user_accounts from anon, authenticated/i);
assert.match(userAccountsMigration, /grant all on table public\.user_accounts to service_role/i);
assert.match(userAccountsMigration, /create trigger sync_user_account_auth/i);
assert.match(userAccountsMigration, /create trigger sync_user_account_profile/i);
assert.match(userAccountsMigration, /insert into public\.user_accounts/i);
assert.match(userAccountsLockMigration, /revoke execute on function public\.sync_user_account_from_auth\(\) from public, anon, authenticated/i);
assert.match(userAccountsLockMigration, /revoke execute on function public\.sync_user_account_from_profile\(\) from public, anon, authenticated/i);
assert.match(signupAbuseMigration, /create table if not exists public\.signup_rate_limits/i);
assert.match(signupAbuseMigration, /consume_signup_rate_limit/i);
assert.match(signupAbuseMigration, /risk_score/i);
assert.match(signupAbuseMigration, /review_status/i);
assert.match(signupAbuseMigration, /prune_flagged_unconfirmed_accounts/i);
assert.match(signupAbuseMigration, /p_dry_run boolean default true/i);
assert.match(signupCleanupFixMigration, /u\.email::text/i);
assert.match(signupCleanupFixMigration, /p_older_than < interval '24 hours'/i);
for (const table of [
  "user_profile_details",
  "user_friendships",
  "user_blocks",
  "user_presence",
  "user_notifications",
]) {
  assert.match(
    socialFoundationMigration,
    new RegExp(`create table public\\.${table}|create table if not exists public\\.${table}`, "i"),
    `missing social table: ${table}`
  );
  assert.match(
    socialFoundationMigration,
    new RegExp(`alter table public\\.${table} enable row level security`, "i"),
    `RLS must be enabled on ${table}`
  );
}
assert.match(socialFoundationMigration, /is_public boolean not null default false/i);
assert.match(socialFoundationMigration, /show_presence boolean not null default true/i);
assert.match(socialFoundationMigration, /private\.users_blocked/i);
assert.match(socialFoundationMigration, /create trigger create_social_profile_rows/i);
assert.match(socialFoundationMigration, /create trigger notify_friendship_change/i);
assert.match(socialFoundationMigration, /'\/member\/\?id=' \|\| new\.requester_id::text/i);
assert.match(socialFoundationMigration, /'\/member\/\?id=' \|\| new\.addressee_id::text/i);
assert.match(socialFoundationMigration, /create trigger remove_friendship_on_block/i);
assert.match(socialFoundationMigration, /grant update \(read_at\) on public\.user_notifications/i);
assert.match(socialFoundationMigration, /grant usage, select on sequence public\.user_friendships_id_seq to authenticated/i);
assert.match(socialFoundationMigration, /grant all on public\.user_presence to service_role/i);
assert.doesNotMatch(socialFoundationMigration, /grant insert, update, delete on public\.user_presence to authenticated/i);
assert.doesNotMatch(socialFoundationMigration, /grant insert on public\.user_notifications to authenticated/i);
assert.match(socialPrivacyMigration, /revoke select on public\.profiles from anon, authenticated/i);
assert.doesNotMatch(socialPrivacyMigration, /grant select on public\.profiles to anon, authenticated/i);
assert.match(
  socialPrivacyMigration,
  /grant select \(id, display_name, avatar_url, role\)\s+on public\.profiles\s+to anon, authenticated/i
);
assert.match(socialPrivacyMigration, /create or replace function public\.get_public_member_profile\(p_user_id uuid\)/i);
assert.match(socialPrivacyMigration, /when d\.show_joined_at then p\.created_at/i);
assert.match(socialPrivacyMigration, /when d\.show_presence[\s\S]*private\.users_blocked\(p\.id, auth\.uid\(\)\)[\s\S]*then pr\.status/i);
assert.match(socialPrivacyMigration, /create or replace function public\.get_public_member_friends\(p_user_id uuid\)/i);
assert.match(socialPrivacyMigration, /owner_details\.show_friends/i);
assert.match(socialPrivacyMigration, /friend_details\.is_public/i);
assert.match(socialPrivacyMigration, /revoke all on function public\.get_public_member_profile\(uuid\) from public/i);
assert.match(socialPrivacyMigration, /revoke all on function public\.get_public_member_friends\(uuid\) from public/i);
assert.match(factionMembershipMigration, /drop function if exists public\.get_public_member_profile\(uuid\)[\s\S]*create or replace function public\.get_public_member_profile/i);
assert.match(usernameHistoryMigration, /add column if not exists show_username_history boolean not null default false/i);
assert.match(usernameHistoryMigration, /create table if not exists public\.user_username_history/i);
assert.match(usernameHistoryMigration, /alter table public\.user_username_history enable row level security/i);
assert.match(usernameHistoryMigration, /auth\.uid\(\) = user_id/i);
assert.match(usernameHistoryMigration, /viewer\.role = 'admin'/i);
assert.match(usernameHistoryMigration, /create trigger capture_username_change/i);
assert.match(usernameHistoryMigration, /values \(old\.id, old\.display_name, now\(\)\)/i);
assert.match(usernameHistoryMigration, /create or replace function public\.get_public_username_history\(p_user_id uuid\)/i);
assert.match(usernameHistoryMigration, /d\.show_username_history/i);
assert.match(usernameHistoryMigration, /limit 10/i);
assert.match(usernameHistoryMigration, /revoke all on function public\.get_public_username_history\(uuid\) from public/i);
assert.match(userBadgesMigration, /create table if not exists public\.user_badges/i);
assert.match(userBadgesMigration, /create table if not exists public\.user_badge_assignments/i);
assert.match(userBadgesMigration, /alter table public\.user_badges enable row level security/i);
assert.match(userBadgesMigration, /alter table public\.user_badge_assignments enable row level security/i);
assert.match(userBadgesMigration, /grant select \(user_id, badge_id, is_displayed, display_order\)[\s\S]*to anon, authenticated/i);
assert.doesNotMatch(userBadgesMigration, /grant select on public\.user_badge_assignments to anon, authenticated/i);
assert.match(userBadgesMigration, /grant update \(is_displayed, display_order\)[\s\S]*to authenticated/i);
assert.match(userBadgesMigration, /grant all on public\.user_badge_assignments to service_role/i);
assert.match(userBadgesMigration, /create trigger guard_badge_display_limit/i);
assert.match(userBadgesMigration, /displayed_count >= 3/i);
assert.match(userBadgesMigration, /auth\.uid\(\) = user_id/i);
for (const slug of ["beta-tester", "contributor", "mod-author", "server-host", "moderator", "developer", "founder"]) {
  assert.ok(userBadgesMigration.includes(`'${slug}'`), `missing seeded badge: ${slug}`);
}
assert.match(userServerIdentityMigration, /add column if not exists show_recent_servers boolean not null default false/i);
assert.match(userServerIdentityMigration, /create table if not exists public\.user_server_favorites/i);
assert.match(userServerIdentityMigration, /create table if not exists public\.user_server_history/i);
assert.match(userServerIdentityMigration, /alter table public\.user_server_favorites enable row level security/i);
assert.match(userServerIdentityMigration, /alter table public\.user_server_history enable row level security/i);
assert.match(userServerIdentityMigration, /auth\.uid\(\) = user_id/i);
assert.match(userServerIdentityMigration, /create or replace function public\.record_user_server_session/i);
assert.match(userServerIdentityMigration, /server session exceeds seven days/i);
assert.match(userServerIdentityMigration, /revoke all on function public\.record_user_server_session[\s\S]*from public, anon, authenticated/i);
assert.match(userServerIdentityMigration, /grant execute on function public\.record_user_server_session[\s\S]*to service_role/i);
assert.match(userServerIdentityMigration, /create or replace function public\.get_public_recent_servers\(p_user_id uuid\)/i);
assert.match(userServerIdentityMigration, /d\.show_recent_servers/i);
assert.match(userServerIdentityMigration, /private\.users_blocked\(h\.user_id, auth\.uid\(\)\)/i);
assert.match(userServerIdentityMigration, /limit 5/i);
assert.match(userCharactersMigration, /add column if not exists show_characters boolean not null default false/i);
assert.match(userCharactersMigration, /create table if not exists public\.user_characters/i);
assert.match(userCharactersMigration, /alter table public\.user_characters enable row level security/i);
assert.match(userCharactersMigration, /grant select on public\.user_characters to authenticated/i);
assert.match(userCharactersMigration, /grant all on public\.user_characters to service_role/i);
assert.doesNotMatch(userCharactersMigration, /grant (insert|update|delete).*public\.user_characters to authenticated/i);
assert.match(userCharactersMigration, /create or replace function public\.upsert_user_character/i);
assert.match(userCharactersMigration, /revoke all on function public\.upsert_user_character[\s\S]*from public, anon, authenticated/i);
assert.match(userCharactersMigration, /grant execute on function public\.upsert_user_character[\s\S]*to service_role/i);
assert.match(userCharactersMigration, /create or replace function public\.get_public_user_characters\(p_user_id uuid\)/i);
assert.match(userCharactersMigration, /d\.show_characters/i);
assert.match(userCharactersMigration, /private\.users_blocked\(c\.user_id, auth\.uid\(\)\)/i);
assert.match(userCharactersMigration, /limit 10/i);
for (const table of [
  "factions",
  "faction_roles",
  "faction_members",
  "faction_applications",
]) {
  assert.match(
    factionFoundationMigration,
    new RegExp(`create table public\\.${table}`, "i"),
    `missing faction table: ${table}`
  );
  assert.match(
    factionFoundationMigration,
    new RegExp(`alter table public\\.${table} enable row level security`, "i"),
    `RLS must be enabled on ${table}`
  );
}
assert.match(factionFoundationMigration, /create unique index faction_applications_open_per_user/i);
assert.match(factionFoundationMigration, /create or replace function public\.review_faction_application/i);
assert.match(factionFoundationMigration, /role in \('moderator', 'admin'\)/i);
assert.match(factionFoundationMigration, /insert into public\.faction_roles/i);
assert.match(factionFoundationMigration, /insert into public\.faction_members/i);
assert.match(factionFoundationMigration, /grant select on public\.factions to anon, authenticated/i);
assert.match(factionFoundationMigration, /grant select on public\.faction_applications to authenticated/i);
assert.match(factionFoundationMigration, /grant insert \([\s\S]*proposed_name[\s\S]*status[\s\S]*\) on public\.faction_applications to authenticated/i);
assert.match(factionFoundationMigration, /grant update \([\s\S]*proposed_name[\s\S]*updated_at[\s\S]*\) on public\.faction_applications to authenticated/i);
assert.doesNotMatch(factionFoundationMigration, /grant (?:insert|update) on public\.faction_applications to authenticated/i);
assert.match(factionFoundationMigration, /revoke all on function public\.review_faction_application\(uuid, text, text\) from public, anon/i);
assert.match(
  factionFoundationMigration,
  /create policy "active factions are public"\s+on public\.factions\s+for select\s+using \(status = 'active'\);/i
);

for (const table of [
  "application_rate_limits",
  "applications",
  "signup_rate_limits",
  "user_accounts",
]) {
  assert.match(
    rpcHardeningMigration,
    new RegExp(`create policy "backend only"\\s+on public\\.${table}\\s+for all\\s+to anon, authenticated\\s+using \\(false\\)\\s+with check \\(false\\)`, "i"),
    `missing backend-only policy: ${table}`
  );
}

for (const signature of [
  "get_public_member_friends\\(uuid\\)",
  "get_public_member_profile\\(uuid\\)",
  "get_public_recent_servers\\(uuid\\)",
  "get_public_user_characters\\(uuid\\)",
  "cancel_faction_membership_request\\(uuid\\)",
  "invite_faction_member\\(uuid, uuid\\)",
  "is_forum_moderator\\(\\)",
  "leave_faction\\(uuid\\)",
  "remove_faction_member\\(uuid, uuid\\)",
  "request_faction_membership\\(uuid\\)",
  "respond_faction_invite\\(uuid, boolean\\)",
  "respond_faction_membership\\(uuid, uuid, boolean\\)",
  "review_faction_application\\(uuid, text, text\\)",
  "set_primary_faction\\(uuid\\)",
]) {
  assert.match(
    rpcHardeningMigration,
    new RegExp(`alter function public\\.${signature} set schema private`, "i"),
    `privileged RPC must move to private: ${signature}`
  );
}

for (const name of [
  "get_public_member_friends",
  "get_public_member_profile",
  "get_public_recent_servers",
  "get_public_user_characters",
  "get_public_username_history",
  "cancel_faction_membership_request",
  "invite_faction_member",
  "is_forum_moderator",
  "leave_faction",
  "remove_faction_member",
  "request_faction_membership",
  "respond_faction_invite",
  "respond_faction_membership",
  "review_faction_application",
  "set_primary_faction",
]) {
  assert.match(
    rpcHardeningMigration,
    new RegExp(`create or replace function public\\.${name}[\\s\\S]*?security invoker`, "i"),
    `public RPC must be SECURITY INVOKER: ${name}`
  );
}

assert.match(
  rpcHardeningMigration,
  /create or replace function private\.get_public_username_history\(p_user_id uuid\)[\s\S]*security definer/i
);
assert.match(
  factionPolicyExecutionMigration,
  /grant execute on function private\.can_manage_faction_members\(uuid\) to authenticated, service_role/i
);

assert.match(
  factionReviewGuardsMigration,
  /create policy "users and admins read faction applications"[\s\S]*p\.role = 'admin'/i
);
assert.match(
  factionReviewGuardsMigration,
  /create or replace function private\.review_faction_application\([\s\S]*role = 'admin'/i
);
assert.doesNotMatch(
  factionReviewGuardsMigration,
  /role in \('moderator', 'admin'\)/i
);
assert.match(
  factionReviewGuardsMigration,
  /application_row\.status not in \('submitted', 'reviewing'\)/i
);
assert.match(
  factionReviewGuardsMigration,
  /p_decision in \('changes_requested', 'rejected'\)[\s\S]*review note required/i
);

assert.match(registerAccount, /consume_signup_rate_limit/);
assert.match(registerAccount, /captchaToken/);
assert.match(registerAccount, /website/);
assert.match(registerAccount, /IP_LIMIT = 5/);
assert.match(registerAccount, /EMAIL_LIMIT = 3/);
assert.match(registerAccount, /allowedRedirects/);
assert.doesNotMatch(registerAccount, /SUPABASE_SERVICE_ROLE_KEY\s*[:=]\s*["'][^"']+/);

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

const reviewPayload = buildReviewUpdatePayload({
  public_id: "application-reference",
  status: "more_info_requested",
  review_note: "Please provide your mod list and preferred testing schedule.",
});
assert.match(reviewPayload.content, /Please provide your mod list/);
assert.deepEqual(reviewPayload.allowed_mentions, { parse: [] });

const submitCors = createCors(
  "https://commonwealth-online.com,http://localhost:3000",
  "POST, OPTIONS",
  "apikey, authorization, content-type",
);
let authenticatedHandlerCalls = 0;
const submitHandler = submitCors.wrap(async (request) => {
  authenticatedHandlerCalls += 1;
  return Response.json({ method: request.method });
});

const allowedPreflight = await submitHandler(new Request("https://example.test", {
  method: "OPTIONS",
  headers: { Origin: "http://localhost:3000" },
}));
assert.equal(allowedPreflight.status, 204);
assert.equal(allowedPreflight.headers.get("Access-Control-Allow-Origin"), "http://localhost:3000");
assert.equal(authenticatedHandlerCalls, 0);

const blockedPreflight = await submitHandler(new Request("https://example.test", {
  method: "OPTIONS",
  headers: { Origin: "https://unapproved.example" },
}));
assert.equal(blockedPreflight.status, 204);
assert.equal(blockedPreflight.headers.has("Access-Control-Allow-Origin"), false);
assert.equal(authenticatedHandlerCalls, 0);

const allowedPost = await submitHandler(new Request("https://example.test", {
  method: "POST",
  headers: { Origin: "http://localhost:3000" },
}));
assert.equal(allowedPost.headers.get("Access-Control-Allow-Origin"), "http://localhost:3000");

const blockedPost = await submitHandler(new Request("https://example.test", {
  method: "POST",
  headers: { Origin: "https://unapproved.example" },
}));
assert.equal(blockedPost.headers.has("Access-Control-Allow-Origin"), false);

const reviewCors = createCors(
  "http://localhost:3000",
  "GET, PATCH, OPTIONS",
  "apikey, authorization, content-type, x-application-review-token",
);
const reviewHandler = reviewCors.wrap(async (request) => Response.json({ method: request.method }));
const allowedPatch = await reviewHandler(new Request("https://example.test", {
  method: "PATCH",
  headers: { Origin: "http://localhost:3000" },
}));
assert.equal(allowedPatch.headers.get("Access-Control-Allow-Origin"), "http://localhost:3000");

const blockedPatch = await reviewHandler(new Request("https://example.test", {
  method: "PATCH",
  headers: { Origin: "https://unapproved.example" },
}));
assert.equal(blockedPatch.headers.has("Access-Control-Allow-Origin"), false);

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
  ["account profiles migration", accountProfilesMigration],
  ["real profile icons migration", realProfileIconsMigration],
  ["local profile icons migration", localProfileIconsMigration],
  ["faction profile icons migration", factionProfileIconsMigration],
  ["user accounts migration", userAccountsMigration],
  ["user accounts lock migration", userAccountsLockMigration],
  ["signup abuse migration", signupAbuseMigration],
  ["signup cleanup fix migration", signupCleanupFixMigration],
  ["social foundation migration", socialFoundationMigration],
  ["faction foundation migration", factionFoundationMigration],
  ["RPC hardening migration", rpcHardeningMigration],
  ["faction policy execution migration", factionPolicyExecutionMigration],
  ["faction review guards migration", factionReviewGuardsMigration],
  ["register account function", registerAccount],
  ["config", config],
  ["confirmation template", confirmationTemplate],
  ["env example", envExample],
]) {
  assert.doesNotMatch(content, /sb_secret_[A-Za-z0-9_-]+/, `${name} contains a Supabase secret key`);
  assert.doesNotMatch(content, /postgres(?:ql)?:\/\/[^\s"']+:[^\s"']+@/i, `${name} contains a database URI`);
  assert.doesNotMatch(content, /SUPABASE_ACCESS_TOKEN\s*=\s*\S+/i, `${name} contains an access token`);
}

console.log("supabase contract checks passed");
