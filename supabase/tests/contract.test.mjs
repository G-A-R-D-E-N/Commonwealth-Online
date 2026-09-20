import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const migration = fs.readFileSync(path.join(root, "migrations", "20260920000000_forum.sql"), "utf8");
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
assert.match(envExample, /^SUPABASE_AUTH_EXTERNAL_DISCORD_CLIENT_ID=$/m);
assert.match(envExample, /^SUPABASE_AUTH_EXTERNAL_DISCORD_SECRET=$/m);

for (const [name, content] of [
  ["migration", migration],
  ["config", config],
  ["env example", envExample],
]) {
  assert.doesNotMatch(content, /sb_secret_[A-Za-z0-9_-]+/, `${name} contains a Supabase secret key`);
  assert.doesNotMatch(content, /postgres(?:ql)?:\/\/[^\s"']+:[^\s"']+@/i, `${name} contains a database URI`);
  assert.doesNotMatch(content, /SUPABASE_ACCESS_TOKEN\s*=\s*\S+/i, `${name} contains an access token`);
}

console.log("supabase contract checks passed");
