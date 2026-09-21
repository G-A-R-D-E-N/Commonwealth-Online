# Commonwealth Online website

The website is a static EJS build. Supabase provides application persistence
and the `submit-application` Edge Function; the browser submits directly to it.

## Requirements

- Node.js 24.21.0 LTS or newer

## Build and test

```bash
npm install
npm run build:static
npm run test:static
npm test
```

The generated site is written to `dist/`. It contains only HTML, CSS,
JavaScript, assets, the curated `data/servers.json` copy, and the generated
`data/changelogs.json` release log. This package has
no production server, local database, migration runner, or mail/Discord client.

## Structure

```
scripts/build-static.js       renders the public artifact
src/config.js                 build-time public configuration
src/lib/applications.js       team and beta form schemas
src/lib/nav.js                navigation and footer links
src/routes/page-config.js     public page definitions and content
views/                        EJS page and partial templates
assets/                       images, fonts, and branding
static/                       browser JavaScript and CSS
servers/server.json           curated public server data
../changelogs/*.md            repo-level release changelog input
```

## Pages

| Page | URL |
| --- | --- |
| Home | `/` |
| Media | `/media` |
| Roadmap | `/roadmap` |
| Servers | `/servers` |
| Updates | `/updates` |
| Repository | https://github.com/G-A-R-D-E-N/Commonwealth-Online |
| Account | `/account` |
| Forum | `/forum` |
| Applications | `/apply` |
| Join the team | `/apply/team` |
| Beta testers | `/apply/beta` |
| Application received | `/apply/thanks` |

The static workflow publishes `dist/` to GitHub Pages. A hosting platform that
supports static files can serve the same directory.

## Configuration

Copy `.env.example` to `.env` when local build values are needed.

| Variable | Purpose |
| --- | --- |
| `SITE_URL` | Public site origin used in generated content |
| `SUPABASE_URL` | Public Supabase project URL embedded in application forms |
| `SUPABASE_PUBLISHABLE_KEY` | Public Supabase browser key embedded in application forms |
| `FEATURE_FORUM` | Show the forum link in navigation |
| `FEATURE_APPLICATIONS` | Show the application link in navigation |

Supabase private credentials, Discord webhook secrets, and database passwords
belong in Supabase project secrets or deployment secret storage, never in this
package or the generated browser files.

## Application flow

The team and beta forms are rendered into the static artifact. Browser-side
validation and submission live in `static/js/applications.js`. The Edge
Function validates, rate-limits, and persists the application in Supabase, then
performs Discord delivery in the background. Reviewers use the separate
`review-application` Edge Function; its token and Discord webhook remain
Supabase secrets and are not part of this website package.

## Account flow

The `/account` page uses Supabase Auth for email/password registration, sign-in, Discord OAuth and manual Discord identity linking. Profiles are stored in `public.profiles`.

Profile pictures are not uploaded. The static build fetches six real Fallout 4 perk images from the pinned public source documented in `PROFILE_ICON_ATTRIBUTION.md`, verifies their Git blob SHAs, and deploys them locally. The browser and database only accept those six local paths. The database constraint is the enforcement boundary, so direct API requests cannot store arbitrary avatar URLs.

Discord OAuth still requires provider credentials and manual identity linking to be enabled in the hosted Supabase Auth configuration.
