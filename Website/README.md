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
JavaScript, assets, and the curated `data/servers.json` copy. This package has
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
servers/server.json            curated public server data
```

## Pages

| Page | URL |
| --- | --- |
| Home | `/` |
| Media | `/media` |
| Roadmap | `/roadmap` |
| Servers | `/servers` |
| Updates | `/updates` |
| Repository | `/repo` |
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
| `GITEA_BASE`, `GITEA_OWNER`, `GITEA_REPO` | Repository and release links |

Supabase private credentials, Discord webhook secrets, and database passwords
belong in Supabase project secrets or deployment secret storage, never in this
package or the generated browser files.

## Application flow

The team and beta forms are rendered into the static artifact. Browser-side
validation and submission live in `static/js/applications.js`. The Edge
Function validates and persists the application in Supabase, then performs
Discord delivery in the background. Discord forum thread creation and review
workflow are backend concerns and are not part of this website package.
