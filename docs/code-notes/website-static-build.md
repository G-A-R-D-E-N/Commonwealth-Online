# Commonwealth Online website static build

## Pre-change flow trace

### `Website/src/routes/pages.js:router.get`

The pre-change public routes `/`, `/media`, `/servers`, `/updates`, and `/repo`
only selected an EJS page and passed page metadata. The shared `head`, `navbar`, `footer`, and
`scripts` partials consume `page`, `site`, `navItems`, `defaultFooterLinks`, and
`year`. The page templates do not require a request, session, database, or secret
to render.

The same router owns 301 redirects from `/index.html`, `/media/index.html`,
`/servers/index.html`, `/updates/index.html`, and `/repo/index.html` to their
clean URLs. Static hosting must preserve these legacy redirects explicitly and
redirect `/media`, `/servers`, `/updates`, `/repo`, `/roadmap`, `/apply`, and `/forum`
to their slash-terminated directory URLs before resolving the generated
`index.html` files.

### `Website/src/app.js:createApp` and `Website/src/app.js:securityHeaders`

`Website/src/app.js:createApp` serves `/static` and `/assets`, renders EJS, and
mounts `/api/v1`, `/forum`, and `/apply`. It also serves the public source file
at `/data/servers.json` so the shared Servers script behaves the same under the
Express renderer and the static build. The existing response protections are
`Content-Security-Policy`, `X-Content-Type-Options`, `Referrer-Policy`,
`X-Frame-Options`, `Permissions-Policy`, and production-only
`Strict-Transport-Security`. Static hosting must configure equivalent response
headers at the CDN or web-server layer.

### Browser requests — `Website/static/js/servers.js:load`, `Website/static/js/updates.js:loadReleases`, and `Website/static/js/repo.js:bootstrap`

| Page or script | Request | Boundary |
| --- | --- | --- |
| Servers, `Website/static/js/servers.js:load` | `/data/servers.json` | The shared browser script always reads the raw `Website/servers/server.json` registry: the static build copies it to `dist/data/servers.json`, and Express serves the source file at the same public path |
| Updates, `Website/static/js/updates.js:loadReleases` | Gitea `/api/v1/repos/Commonwealth-Online/Commonwealth-Online-Public/releases` | External Gitea |
| Repository, `Website/static/js/repo.js:bootstrap` and helpers | Gitea repository, contents, commits, languages, raw-file APIs | External Gitea |
| Repository, `Website/static/js/repo.js:loadIconTheme` | jsDelivr `material-icon-theme` JSON and SVGs | External CDN |
| Applications, `Website/static/js/applications.js` | `/api/v1/applications` and `/api/v1/applications/discord-membership` | Backend trust boundary |

`links.js`, `navbar.js`, and `script.js` only wire navigation, menus, and page
interactions. No browser script exposes Discord, SMTP, admin, or database
credentials.

## Static and dynamic split: `Website/scripts/build-static.js`

### `Website/scripts/build-static.js:PUBLIC_PAGES`

`Website/scripts/build-static.js:pages` renders the six public pages plus the
static `/apply` and `/forum` shells from the existing EJS templates,
renders their shared partials, copies `assets/` and `static/`, and copies the
curated server data to `dist/data/servers.json`. The Servers script reads that
file directly and keeps its existing rendering and copy-address behavior. The
static file is a build-time snapshot: changes to `servers/server.json` require
rebuilding and redeploying the static site. Live server state or player counts
should remain behind a future dynamic endpoint.

The Express API retains its `count` field and derives each server `address`, so
the proof compares the source fields and derived addresses rather than claiming
the raw JSON and API responses are byte-identical.

The backend remains required for application submission and validation, Discord
membership verification and bot review, SQLite storage, Mailcow SMTP, admin
application APIs, health, forum/account work, rate limiting, and authentication.

Updates and Repository remain browser-side Gitea integrations; static hosting
does not proxy or cache those requests.

## Additional static shells

### `Website/src/routes/forum.js:router.get` and `Website/src/routes/applications.js:router.get("/")`

The current `/forum` page is a scaffold with no database or API request; its
`enabled` local is not consumed by `views/pages/forum.ejs`. The `/apply` page is
also only a chooser; its `enabled` local is not consumed by
`views/pages/applications.ejs`. Both can be rendered into static directory
indexes while their future forum work and application submission endpoints stay
backend-owned.

`/apply/team` and `/apply/beta` remain Express-rendered because
`views/pages/apply-form.ejs` consumes schema, submitted values, validation
errors, and notices, and the routes retain server-side POST fallback. The
`/apply/thanks` page remains Express-rendered because its reference comes from
the request query string.

## Static-to-Express routing boundary: `Website/src/app.js:createApp`

The static host serves the generated directory pages, `/assets/*`,
`/static/*`, and `/data/servers.json`. The public origin must route
`/apply/team`, `/apply/beta`, `/apply/thanks`, `/api/v1/*`, and future dynamic
`/forum/*` requests to Express before the static fallback. The static `/apply/`
chooser therefore links into the backend application routes; a static-only
deployment does not provide application submission functionality.

### `Website/test/static.test.js:run`

The website proof builds the expected eight pages, rejects unresolved EJS,
serves the generated directory URLs with a small local static server, compares
their HTML with Express output, checks raw server data and normalized API
fields, and exercises the dynamic application and forum API boundaries.

## Static-host security configuration

### `Website/src/app.js:securityHeaders`

Configure the static host to send these headers on HTML, asset, and data
responses:

```text
Content-Security-Policy: default-src 'self'; img-src 'self' data: https://cdn.jsdelivr.net; style-src 'self' 'unsafe-inline'; script-src 'self'; font-src 'self'; connect-src 'self' https://git.zambazosmedia.group; object-src 'none'; base-uri 'self'; form-action 'self'; frame-ancestors 'none'
X-Content-Type-Options: nosniff
Referrer-Policy: strict-origin-when-cross-origin
X-Frame-Options: DENY
Permissions-Policy: geolocation=(), microphone=(), camera=()
Strict-Transport-Security: max-age=31536000; includeSubDomains
```

The HSTS header is appropriate only when the production site is HTTPS-only.
Legacy `.html` URLs need permanent redirects to `/`, `/media/`, `/servers/`,
`/updates/`, and `/repo/`; the non-slash clean paths should also redirect to
those directory URLs. The generated files themselves remain available at the
legacy paths only when the host's redirect rules are not applied.
