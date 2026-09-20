# Commonwealth Online website

The project site uses EJS templates for both Express rendering and the static
public build. The design, CSS and front-end behaviour are unchanged.

The website includes team and beta tester applications, with SQLite storage and
a Discord forum review bot.

## Requirements

- Node.js 20.12 or newer (tested on 24)
- No other services; storage is a local SQLite file

## Quick start

```bash
npm install
npm start          # http://localhost:3000
```

Other scripts:

```bash
npm run dev        # start with the Node file watcher
npm run migrate    # apply pending database migrations and exit
npm run build:static  # render the public pages to dist/
npm run test:static   # verify generated pages and the backend boundary
```

Copy `.env.example` to `.env` to override anything. Every value has a working
default, so the site runs with no `.env` at all.

## Project structure

```
server.js                     entry point, graceful shutdown
src/
  app.js                      Express assembly: view engine, middleware, route mounts
  config.js                   env-driven configuration
  db/
    index.js                  SQLite connection + migration runner
    migrations/*.sql          schema, applied in filename order
  lib/
    nav.js                    nav + footer link model (feature gated)
    servers.js                reads servers/server.json for the API
    applications.js           application validation + queries
    discord.js                Discord forum posts, review actions and roles
    mailcow.js                sends More Information emails through Mailcow SMTP
  middleware/
    auth.js                   shared-secret auth for bot/admin endpoints
    rate-limit.js             in-memory limiter for submission endpoints
    errors.js                 404 + error handler (HTML and JSON)
  routes/
    pages.js                  /, /media, /roadmap, /servers, /updates, /repo
    forum.js                  /forum          (scaffold)
    applications.js           /apply, /apply/team, /apply/beta
    api/                      /api/v1/*
views/
  partials/                   head, navbar, footer, script tags
  pages/                      one view per page, plus forum/apply/error
assets/                       images, fonts, branding (served at /assets)
static/                       css + js (served at /static)
servers/server.json           curated public server list (data, not a page)
data/                         SQLite database (gitignored, created on first run)
```

## Routes

| Page | URL | View |
| --- | --- | --- |
| Home | `/` | `views/pages/home.ejs` |
| Media | `/media` | `views/pages/media.ejs` |
| Roadmap | `/roadmap` | `views/pages/roadmap.ejs` |
| Servers | `/servers` | `views/pages/servers.ejs` |
| Updates | `/updates` | `views/pages/updates.ejs` |
| Repository | `/repo` | `views/pages/repo.ejs` |
| Forum | `/forum` | `views/pages/forum.ejs` (scaffold) |
| Applications | `/apply` | `views/pages/applications.ejs` |
| Join the team | `/apply/team` | `views/pages/apply-form.ejs` |
| Beta testers | `/apply/beta` | `views/pages/apply-form.ejs` |
| Application received | `/apply/thanks` | `views/pages/apply-thanks.ejs` |

The old static URLs (`/index.html`, `/media/index.html`, and so on) redirect to
the new paths with a 301.

Asset URLs are now root-absolute (`/static/...`, `/assets/...`), and the header
is rendered server-side instead of being injected by JavaScript. `navbar.js` now
only wires the mobile menu toggle.

## API

Base path `/api/v1`. `GET /api/v1` lists the endpoints.

| Method | Endpoint | Notes |
| --- | --- | --- |
| GET | `/api/v1/health` | Uptime, environment, database and Discord bot status |
| GET | `/api/v1/servers` | Public server list from `servers/server.json`, normalized for the browser and returned with `count` |
| GET | `/api/v1/forum/categories` | Categories from the database (read-only scaffold) |
| POST | `/api/v1/applications` | Submit; body must include `type`: `team` or `beta` |
| POST | `/api/v1/applications/team` | Submit a team application (`type` is set for you) |
| POST | `/api/v1/applications/beta` | Submit a beta tester application |
| GET | `/api/v1/applications` | List submissions — requires `ADMIN_TOKEN` |
| GET | `/api/v1/applications/team` | List team submissions — requires `ADMIN_TOKEN` |
| GET | `/api/v1/applications/beta` | List beta submissions — requires `ADMIN_TOKEN` |
| GET | `/api/v1/applications/:publicId` | Fetch one — requires `ADMIN_TOKEN` |
| PATCH | `/api/v1/applications/:publicId` | Set `pending`/`reviewing`/`accepted`/`rejected` — requires `ADMIN_TOKEN` |

List query parameters: `type`, `status`, `since` (ISO 8601, exclusive), `limit` (max 200), `offset`.

```bash
curl -X POST http://localhost:3000/api/v1/applications \
  -H "Content-Type: application/json" \
  -d '{"type":"beta","displayName":"Vault Dweller","email":"dweller@example.com",
       "discordHandle":"dweller","gameEdition":"both","availability":"Evenings UTC+12",
       "motivation":"I would like to help test multiplayer sessions.",
       "confidentialAgreed":true,"feedbackAgreed":true,"ageConfirmed":true}'

curl -H "Authorization: Bearer $ADMIN_TOKEN" \
  "http://localhost:3000/api/v1/applications?status=pending&limit=50"

curl -H "Authorization: Bearer $ADMIN_TOKEN" \
  http://localhost:3000/api/v1/applications/beta
```

Responses use `{ "error": { "code", "message", "details?" } }` for failures.
`POST /api/v1/applications` is rate limited (5 submissions per 10 minutes per IP).
Both JSON and `application/x-www-form-urlencoded` bodies are accepted.

## Configuration

| Variable | Default | Purpose |
| --- | --- | --- |
| `PORT` / `HOST` | `3000` / `127.0.0.1` | Listen address |
| `SITE_URL` | `http://HOST:PORT` | Public origin used in generated content |
| `DATABASE_PATH` | `data/commonwealth-online.sqlite` | SQLite file location |
| `FEATURE_FORUM` | `false` | Show the Forum link in the nav |
| `FEATURE_APPLICATIONS` | `true` | Show the Apply link in the nav |
| `DISCORD_BOT_TOKEN` | empty | Discord bot token |
| `DISCORD_GUILD_ID` | empty | Server the bot checks before accepting applications |
| `DISCORD_APPLICATIONS_FORUM_CHANNEL_ID` | empty | Forum channel used for all applications |
| `DISCORD_TEAM_APPLICATIONS_FORUM_CHANNEL_ID` / `DISCORD_BETA_APPLICATIONS_FORUM_CHANNEL_ID` | empty | Optional per-type forum channel overrides |
| `DISCORD_APPLICATION_REVIEWER_ROLE_ID` | empty | Only this role can review applications |
| `DISCORD_TEAM_ROLE_ID` / `DISCORD_BETA_ROLE_ID` | empty | Role assigned when that application type is accepted |
| `MAILCOW_SMTP_HOST` | empty | Mailcow SMTP host |
| `MAILCOW_SMTP_PORT` | `465` | Mailcow SMTP port |
| `MAILCOW_SMTP_SECURE` | `true` | Use TLS from connection start (`false` for STARTTLS, usually port 587) |
| `MAILCOW_SMTP_USER` / `MAILCOW_SMTP_PASSWORD` | empty | Mailcow mailbox credentials |
| `MAILCOW_FROM` | SMTP user | Sender address |
| `ADMIN_TOKEN` | empty | Enables the admin/bot read endpoints |
| `TRUST_PROXY` | `false` | Set when running behind nginx/Cloudflare so client IPs are read correctly |
| `GITEA_BASE` / `GITEA_OWNER` / `GITEA_REPO` | project defaults | Gitea location used for links and the CSP |

`FEATURE_FORUM` keeps the unfinished forum out of the navigation. Application
pages stay in the nav unless you set `FEATURE_APPLICATIONS=false`.

## Applications, Discord and Mailcow

Public forms live at `/apply/team` and `/apply/beta`. Each submission is stored
in SQLite with every field the applicant filled in.

When `DISCORD_BOT_TOKEN` and an application forum channel are set, the site
creates one Discord forum post per submission. The configured reviewer role can
approve, reject, or request more information. Approval assigns the matching
team or beta role. More Information opens a Discord text box and sends its
contents to the applicant from `MAILCOW_FROM`.

Administrators can also poll with `ADMIN_TOKEN`:

1. `GET /api/v1/applications?status=pending&since=...` (or `/team` / `/beta`)
2. For each row, `email` is the address to write to, and `fields` is the full
   form as `{ key, label, type, value, displayValue }`
3. `PATCH /api/v1/applications/:publicId` with `{ "status": "reviewing", "reviewedBy": "admin" }`
   after reviewing it, so the next poll does not repeat it

List responses look like `{ applications, total, limit, offset }`. Paginate with
`offset` if `total` is greater than `limit` (max 200 per page).

- Submissions are written to SQLite first, so Discord or Mailcow outages never
  lose them. Pending submissions are posted when the bot next connects.
- The bot needs the **Guilds** OAuth2 bot scope plus permission to create posts
  in the target forum, Manage Roles for roles below its own highest role, and
  the **Server Members Intent** enabled in the Developer Portal.

## Database

SQLite via `better-sqlite3`. The connection opens and migrates automatically on
start; `npm run migrate` does the same without serving.

Migrations live in `src/db/migrations` and run in filename order, tracked in the
`schema_migrations` table. Add a new file (for example `002_thread_replies.sql`)
rather than editing an applied one.

The schema covers `accounts`, `forum_categories`, `forum_threads`,
`forum_posts` and `applications` (with `type` `team` | `beta`). The forum and
account tables are intentionally unused by the current routes — they are the
data model for the next phase.

## Deploying

1. `npm ci --omit=dev`
2. Set `NODE_ENV=production`, `SITE_URL`, `HOST=0.0.0.0`, plus `TRUST_PROXY=true`
   when behind a proxy
3. `npm start`, with `data/` on a persistent volume and a process manager
   (systemd, pm2 or similar) keeping it alive
4. Reverse-proxy `/` to the Node port. Static assets under `/static` and
   `/assets` can also be served directly by nginx for speed.

`GET /api/v1/health` is safe to point a load balancer or uptime check at.

### Static hosting

`npm run build:static` writes the public site to `dist/`:

```
dist/index.html
dist/media/index.html
dist/roadmap/index.html
dist/servers/index.html
dist/updates/index.html
dist/repo/index.html
dist/apply/index.html
dist/forum/index.html
dist/assets/
dist/static/
dist/data/servers.json
```

The static host must serve directory indexes, redirect `/media`, `/servers`,
`/updates`, `/repo`, `/roadmap`, `/apply`, and `/forum` to their slash-terminated
directory URLs, and redirect the legacy
`/index.html`, `/media/index.html`, `/servers/index.html`, `/updates/index.html`,
and `/repo/index.html` URLs to `/`, `/media/`, `/servers/`, `/updates/`, and
`/repo/`. It must also preserve the Express security headers at the static
host. Applications, Discord verification,
SQLite, Mailcow, admin APIs, health, and future forum/account writes still
require the Express backend.

Static and backend traffic share the public origin as follows: the static host
serves the generated pages, `/assets/*`, `/static/*`, and `/data/servers.json`;
`/apply/team`, `/apply/beta`, `/apply/thanks`, `/api/v1/*`, and future dynamic
`/forum/*` routes must be reverse-proxied to Express before the static fallback.
The copied server registry is a deployment snapshot, so changes to
`servers/server.json` require `npm run build:static` and a static redeploy.

## Next steps

- **Forum**: thread list, thread view and posting on top of the existing tables
  and `/api/v1/forum/*`; add Discord OAuth sessions in `src/middleware/auth.js`
  next to the bot token.
- **API**: the Updates and Repository pages still call Gitea directly from the
  browser. Proxying those through `/api/v1` would add server-side caching and
  keep the Gitea instance out of the client.
- **Rate limiting**: the limiter is per-process; move it to a shared store if the
  site ever runs on more than one instance.
