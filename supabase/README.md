# Supabase forum

This directory is the Git-tracked contract for the future Commonwealth Online forum.

## Public repository boundary

Safe to commit:

- `config.toml`
- database migrations
- Row Level Security policies
- browser-facing forum code
- placeholder environment variable names

Never commit:

- Supabase secret keys
- database passwords or connection strings
- `SUPABASE_ACCESS_TOKEN`
- Discord client secrets
- production exports or user data

The website only needs `SUPABASE_URL` and `SUPABASE_PUBLISHABLE_KEY`. Both are public browser configuration and remain constrained by Postgres grants and Row Level Security.

## When the Supabase project exists

1. Create the managed Supabase project.
2. Connect this GitHub repository through Supabase's GitHub integration, or use the Supabase CLI from CI.
3. Apply the migrations in `supabase/migrations/`.
4. Configure Discord as an Auth provider.
5. Set the Discord callback to `https://<project-ref>.supabase.co/auth/v1/callback`.
6. Add the production site URL and redirect URLs in Supabase Auth.
7. Add GitHub Actions variables:
   - `SUPABASE_PROJECT_REF`
   - `SUPABASE_URL`
   - `SUPABASE_PUBLISHABLE_KEY`
8. Only if CLI deployment is used, add GitHub Actions secrets:
   - `SUPABASE_ACCESS_TOKEN`
   - `SUPABASE_DB_PASSWORD`
9. Keep the Discord client secret in Supabase Auth/provider configuration unless a deployment workflow specifically needs it.

The committed Discord provider configuration references environment variables rather than literal credentials.

## Current forum contract

Anonymous visitors may read public profiles, categories, threads, posts and reactions.

Authenticated users may create threads, reply, edit or delete their own unlocked content, react and file reports.

Moderator/admin status is stored in `profiles.role`. RLS gives moderators access to reports and moderation-level row operations. Browser clients are not granted permission to change their own role, thread lock/pin state, category configuration or report ownership.

The first migration creates the default board list and an `auth.users` trigger that creates a public profile for new Discord-authenticated users.
