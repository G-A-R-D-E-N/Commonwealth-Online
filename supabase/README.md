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

Application submissions are rate limited to five attempts per address per ten minutes by the `consume_application_rate_limit` function. The address is hashed with the service key before storage, and the rate-limit table is inaccessible to browser roles.

Reviewers use the `review-application` Edge Function with the `x-application-review-token` header. The token is stored as the `APPLICATION_REVIEW_TOKEN` Supabase secret. `GET` lists or retrieves applications, and `PATCH /<public_id>` accepts `pending`, `reviewing`, `more_info_requested`, `accepted`, or `rejected`; a review note is required for `more_info_requested`. Status updates are recorded in Supabase and posted to the application Discord thread when one exists.

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
8. Add GitHub Actions secrets:
   - `APPLICATION_REVIEW_TOKEN`
9. Only if CLI deployment is used, add these additional GitHub Actions secrets:
   - `SUPABASE_ACCESS_TOKEN`
   - `SUPABASE_DB_PASSWORD`
10. Keep the Discord client secret in Supabase Auth/provider configuration unless a deployment workflow specifically needs it.

The Supabase deployment workflow copies `APPLICATION_REVIEW_TOKEN` into the Edge Function secrets before deploying the functions.

The committed Discord provider configuration references environment variables rather than literal credentials.

## Current forum contract

Anonymous visitors may read public profiles, categories, threads, posts and reactions.

Authenticated users may create threads, reply, edit or delete their own unlocked content, react and file reports.

Moderator/admin status is stored in `profiles.role`. RLS gives moderators access to reports and moderation-level row operations. Browser clients are not granted permission to change their own role, thread lock/pin state, category configuration or report ownership.

The first migration creates the default board list and an `auth.users` trigger that creates a public profile for new Discord-authenticated users.

## Account registration and Discord linking

The website account page uses Supabase Auth for email/password registration and Discord OAuth. Discord can be used as the initial sign-in method or manually linked from an authenticated profile.

Hosted Supabase must have these Auth settings enabled:

- new user sign-up
- Discord provider with the project callback URL
- manual identity linking
- the production and GitHub Pages account URLs in the redirect allow list

Profile avatars are intentionally not backed by Supabase Storage. `public.profiles.avatar_url` is restricted by a database check constraint to six local `/assets/profile-icons/*.png` paths generated from verified Fallout 4 perk images during the website build, and `handle_new_user()` ignores provider avatar URLs outside that allow list. Source attribution is documented in `Website/PROFILE_ICON_ATTRIBUTION.md`.
