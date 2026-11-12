#!/usr/bin/env python3
"""test_misc_contracts_part3: consolidated contract sections.

Consolidated from 4 single-contract scripts so the group runs one process
instead of 4. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python tests/audit/test_misc_contracts_part3.py
"""
from __future__ import annotations
import os
import sys
import traceback
from pathlib import Path

_MERGE_ROOT = Path(__file__).resolve().parents[2]


class _SectionExit(Exception):
    """Raised where a moved contract used to call sys.exit()/raise SystemExit."""

    def __init__(self, code=None):
        super().__init__(code)
        self.code = code


def _merge_exit(code=None):
    raise _SectionExit(code)


def _run_section(name, section):
    cwd = os.getcwd()
    environ = dict(os.environ)
    ok = True
    try:
        section()
    except _SectionExit as exc:
        ok = exc.code in (0, None)
        if not ok:
            print("FAIL: " + name + " exited with " + repr(exc.code))
    except BaseException as exc:  # noqa: BLE001 - a bad contract must not stop the rest
        if isinstance(exc, KeyboardInterrupt):
            raise
        traceback.print_exc()
        ok = False
    finally:
        os.chdir(cwd)
        if dict(os.environ) != environ:
            os.environ.clear()
            os.environ.update(environ)
    print(("PASS " if ok else "FAIL ") + name)
    return ok




def _section_013_test_trade_protocol_contract():
    """Contract moved verbatim from tests/audit/test_trade_protocol_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_trade_protocol_contract.py")
    from pathlib import Path
    import re
    import sys

    ROOT = Path(__file__).resolve().parents[2]
    CORE = ROOT / "co-core"
    SERVER = ROOT / "CO-SERVER/server-cpp"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def read(path):
        return path.read_text(encoding="utf-8")


    ledger_header = read(CORE / "include/F4TTradeLedger.h")
    ledger = read(CORE / "src/world/F4TTradeLedger.cpp")
    server_trade = read(SERVER / "src/ServerTrade.h")
    dispatch = read(SERVER / "src/ServerInventoryDispatch.inc")
    impl = read(SERVER / "src/ServerProtocolImpl.inc")
    maintenance = read(SERVER / "src/ServerMaintenance.inc")
    world_policy = read(SERVER / "src/ServerWorldPolicy.inc")
    protocol = read(SERVER / "src/ServerProtocol.cpp")
    store = read(SERVER / "src/DurableStateFileStore.cpp")
    store_header = read(SERVER / "include/DurableStateFileStore.h")
    inventory_doc_path = ROOT / "CO-BETA/docs/protocol/inventory.md"

    for trade_type in ("tradeRequest", "tradeRespond", "tradeOffer", "tradeConfirm", "tradeCancel"):
        check(f'"{trade_type}"' in ledger, f"the co-core parser must accept {trade_type}")
        check(f'a_type == "{trade_type}"' in dispatch, f"the server must route {trade_type}")
    check("HandleTradeIntent(" in dispatch, "trade intents must reach the server handler")
    check('a_type == "tradeOffer"' in protocol, "tradeOffer must use the bounded request size cap")
    check("EndTradesFor(playerId)" in impl, "a disconnect must end the player's trades")
    check("PollTrades(loopMonotonic)" in maintenance, "the maintenance loop must expire idle trades")
    check('"player-trade-v1"' in world_policy, "the welcome must advertise player-trade-v1")
    check("TransferTrade(" in store_header, "the store must declare the trade commit")
    check("DurableStateFileStore::TransferTrade(" in store, "the store must define the trade commit")
    check(re.search(r"kIdleExpirySeconds\s*=\s*60\.0", ledger_header) is not None, "idle trades must expire after 60 seconds")
    check("kTradeMaximumDistance" in server_trade, "the server must bound trade distance")
    if inventory_doc_path.is_file():
        check("tradeOffer" in read(inventory_doc_path) and "player-trade-v1" in read(inventory_doc_path), "the protocol doc must describe the trade types and capability")
    check(re.search(r"kRequestExpirySeconds\s*=\s*15\.0", ledger_header) is not None, "a requested trade must expire after 15 seconds")
    check(re.search(r"kPairCooldownSeconds\s*=\s*30\.0", ledger_header) is not None, "a declined, cancelled or expired pair must wait 30 seconds")
    check('"offerChanged"' in ledger and '"offerVersion"' in ledger, "a confirm for a stale offer must be refused with offerChanged")
    check(not (SERVER / "src/ServerTrade.inc").exists(), "trade code must not add .inc files")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("test_trade_protocol_contract: all checks passed")

def _section_014_test_web_xss_security_contract():
    """Contract moved verbatim from tests/audit/test_web_xss_security_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_web_xss_security_contract.py")
    import re
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]

    SERVER_DETAILS = "CommonwealthOnlineUI/ui/views/CommonwealthOnline/browser/components/server-details.js"
    BROWSER_BRIDGE = "CommonwealthOnlineUI/include/F4TServerBrowserBridgeInternal.h"
    BROWSER_DATA_HEADER = "CommonwealthOnlineUI/include/F4TServerBrowserData.h"
    REPO_JS = "Website/static/js/repo.js"
    REPO_TEMPLATE = "Website/views/pages/repo.ejs"
    PAGES_ROUTE = "Website/src/routes/pages.js"
    DOMPURIFY = "Website/static/js/vendor/dompurify.min.js"
    DOMPURIFY_VERSION = "3.4.17"
    ICON_CDN_HOST = "cdn.jsdelivr.net"


    def read(path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")


    def require(condition: bool, message: str, failures: list[str]) -> None:
        if not condition:
            failures.append(message)


    def directory_hosts() -> set[str]:
        bridge = read(BROWSER_BRIDGE)
        urls = re.findall(r'"(https://[^"/]+)', bridge)
        return {re.sub(r"^https://", "", url) for url in urls}


    def js_function_body(source: str, header: str) -> str:
        index = source.find(header)
        if index < 0:
            return ""
        depth = 0
        started = False
        for number in range(index, len(source)):
            char = source[number]
            if char == "{":
                depth += 1
                started = True
            elif char == "}":
                depth -= 1
                if started and depth == 0:
                    return source[index:number + 1]
        return source[index:]


    def main() -> int:
        failures: list[str] = []

        details = read(SERVER_DETAILS)
        hosts = directory_hosts()
        require(hosts, "L7: the server browser bridge must declare the directory hosts the JS preview allow-list mirrors", failures)
        for host in sorted(hosts):
            require(f'"{host}": true' in details, f"L7: preview image allow-list must contain the directory host {host}", failures)
        require(f'"{ICON_CDN_HOST}": true' in details, "L7: preview image allow-list must contain the project CDN host", failures)

        preview_validation = js_function_body(details, "function allowedPreviewUrl(")
        require(preview_validation, "L7: server-details.js must validate previewImage through allowedPreviewUrl", failures)
        require("new URL(raw, window.location.href)" in preview_validation, "L7: preview URL must be parsed against location.href", failures)
        require('parsed.protocol !== "https:"' in preview_validation, "L7: preview URL must be rejected unless the protocol is https:", failures)
        require("isAllowedPreviewHost(parsed.hostname)" in preview_validation, "L7: preview URL must be rejected unless the hostname is allow-listed", failures)
        require('typeof URL !== "function"' in preview_validation, "L7: the URL fallback must only run when URL is unavailable", failures)
        require("allowedPreviewUrlFromString(raw)" in preview_validation, "L7: URL-less webviews must use the plain-string parser", failures)
        require("catch (error)" in preview_validation and "return \"\";" in preview_validation, "L7: unparseable preview URLs must fail closed", failures)

        fallback = js_function_body(details, "function allowedPreviewUrlFromString(")
        require(fallback, "L7: the plain-string fallback parser must exist", failures)
        require("^https:\\/\\/" in fallback, "L7: the fallback parser must anchor the https scheme", failures)
        require("isAllowedPreviewHost(match[1])" in fallback, "L7: the fallback parser must allow-list the parsed host", failures)
        require('[^<>"\'\\\\\\s]' in fallback, "L7: the fallback parser must reject attribute-breaking characters", failures)

        require("var previewImage = server ? allowedPreviewUrl(server.previewImage) : \"\";" in details,
                "L7: render() must only carry the validated preview URL", failures)
        require('escapeAttr(server.previewImage)' not in details, "L7: server.previewImage must never reach escapeAttr unvalidated", failures)
        require("escapeAttr(previewImage)" in details and '<img src="' in details,
                "L7: the preview img src must come from the validated URL", failures)
        require(details.count("<img ") == 1, "L7: the details pane must have a single image sink", failures)

        escape_attr = js_function_body(details, "function escapeAttr(")
        for entity, name in (("&amp;", "&"), ("&lt;", "<"), ("&gt;", ">"), ("&quot;", '"'), ("&#39;", "'")):
            require(entity in escape_attr, f"L7: escapeAttr must escape {name}", failures)
        require(".replace(/\"/g, \"&quot;\")" in escape_attr and ".replace(/'/g, \"&#39;\")" in escape_attr,
                "L7: escapeAttr must escape both quote characters", failures)

        data_header = read(BROWSER_DATA_HEADER)
        require("previewImage" not in data_header, "L7: note - no C++ previewImage producer exists yet; consumer is hardenend", failures)

        repo = read(REPO_JS)
        sanitizer = js_function_body(repo, "const sanitizeReadmeHtml = (html) =>")
        require(sanitizer, "L8: sanitizeReadmeHtml must exist", failures)
        require("DOMPurify.sanitize(html" in sanitizer, "L8: README HTML must go through DOMPurify.sanitize", failures)
        require("USE_PROFILES: { html: true }" in sanitizer, "L8: DOMPurify must run with the html profile (no SVG/MathML)", failures)
        for tag in ("base", "style", "form", "input", "button"):
            require(f'"{tag}"' in sanitizer, f"L8: DOMPurify config must forbid {tag}", failures)
        for attr in ("formaction", "action", "xlink:href", "srcdoc"):
            require(f'"{attr}"' in sanitizer, f"L8: DOMPurify config must forbid the {attr} attribute", failures)
        require("ALLOW_DATA_ATTR: false" in sanitizer, "L8: README sanitizer must drop data attributes", failures)
        require("template.innerHTML = html" not in sanitizer, "L8: the hand-rolled blocklist sanitizer must be gone", failures)
        require("document.createElement(\"template\")" not in sanitizer, "L8: sanitizeReadmeHtml must not build a DOM template itself", failures)

        render_markdown = js_function_body(repo, "const renderMarkdown = (markdown, readmePath = \"README.md\") =>")
        require("marked.parse(markdown)" in render_markdown, "L8: marked() must still render the markdown", failures)
        require("rewriteReadmeUrls(sanitizeReadmeHtml(html), readmeDir)" in render_markdown,
                "L8: README URLs must still be rewritten after sanitizing", failures)
        require(render_markdown.count('typeof DOMPurify === "undefined"') >= 1,
                "L8: renderMarkdown must fail closed when DOMPurify is missing", failures)
        require('<pre>${escapeHtml(markdown)}</pre>' in render_markdown,
                "L8: a missing sanitizer must degrade to escaped text, never raw HTML", failures)

        require("insertAdjacentHTML" not in repo, "L8: repo.js must not add HTML through insertAdjacentHTML", failures)
        require('${escapeHtml(text)}</pre>' in repo, "L8: plain-text previews must stay escaped", failures)
        readme_sinks = [line.strip() for line in repo.splitlines() if "els.readme.innerHTML =" in line]
        require(readme_sinks, "L8: the README sink must be present", failures)
        for line in readme_sinks:
            safe = (
                "renderMarkdown(" in line
                or "= html;" in line
                or "loadingMarkup.readme()" in line
                or '<p class="repo-empty">' in line
            )
            require(safe, f"L8: unexpected unsanitized README sink: {line}", failures)

        dompurify = read(DOMPURIFY)
        require(len(dompurify) > 15000, "L8: the vendored DOMPurify dist looks truncated", failures)
        require("DOMPurify" in dompurify and "@license" in dompurify, "L8: vendored file must be the official DOMPurify dist", failures)
        require(f"@license DOMPurify {DOMPURIFY_VERSION} |" in dompurify,
                f"L8: vendored DOMPurify must be version {DOMPURIFY_VERSION}", failures)
        require(re.search(r"\.version\s*=\s*[`\"]" + re.escape(DOMPURIFY_VERSION) + r"[`\"]", dompurify) is not None,
                f"L8: vendored DOMPurify build must report version {DOMPURIFY_VERSION}", failures)

        template = read(REPO_TEMPLATE)
        dompurify_tag = '<script src="/static/js/vendor/dompurify.min.js" defer></script>'
        scripts_include = '<%- include("../partials/scripts") %>'
        require(dompurify_tag in template, "L8: the repo template must load DOMPurify", failures)
        require(template.find(dompurify_tag) < template.find(scripts_include),
                "L8: DOMPurify must load before the scripts partial emits repo.js", failures)

        pages = read(PAGES_ROUTE)
        repo_route = pages[pages.find('router.get("/repo"'):]
        repo_route = repo_route[:repo_route.find("});\n});") if "});\n});" in repo_route else len(repo_route)]
        require('"/static/js/vendor/marked.min.js"' in repo_route, "L8: the repo route must load marked", failures)
        require('"/static/js/repo.js"' in repo_route, "L8: the repo route must load repo.js", failures)

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}")
            return 1

        print("PASS: web XSS security contracts (L7 preview allow-list, L8 README sanitizer)")
        return 0


    if True:
        _merge_exit(main())

def _section_015_test_website_security_contract():
    """Contract moved verbatim from tests/audit/test_website_security_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_website_security_contract.py")
    import json
    import re
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]

    API_APPLICATIONS = "Website/src/routes/api/applications.js"
    HTML_APPLICATIONS = "Website/src/routes/applications.js"
    DISCORD_VERIFICATION = "Website/src/lib/discord-verification.js"
    DISCORD = "Website/src/lib/discord.js"
    RATE_LIMIT = "Website/src/middleware/rate-limit.js"
    AUTH_MIDDLEWARE = "Website/src/middleware/auth.js"
    APP = "Website/src/app.js"
    CONFIG = "Website/src/config.js"
    MAILCOW = "Website/src/lib/mailcow.js"
    DB_INDEX = "Website/src/db/index.js"
    REPO_JS = "Website/static/js/repo.js"
    UPDATES_JS = "Website/static/js/updates.js"
    PAGES_ROUTE = "Website/src/routes/pages.js"
    REPO_TEMPLATE = "Website/views/pages/repo.ejs"
    STYLES_CSS = "Website/static/css/styles.css"
    PACKAGE_JSON = "Website/package.json"
    PACKAGE_LOCK = "Website/package-lock.json"
    DOMPURIFY = "Website/static/js/vendor/dompurify.min.js"


    def read(path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")


    def require(condition: bool, message: str, failures: list[str]) -> None:
        if not condition:
            failures.append(message)


    def version_tuple(value: str) -> tuple[int, ...]:
        return tuple(int(part) for part in re.findall(r"\d+", value)[:3])


    def js_function_body(source: str, header: str) -> str:
        index = source.find(header)
        if index < 0:
            return ""
        depth = 0
        started = False
        for number in range(index, len(source)):
            char = source[number]
            if char == "{":
                depth += 1
                started = True
            elif char == "}":
                depth -= 1
                if started and depth == 0:
                    return source[index:number + 1]
        return source[index:]


    def route_block(source: str, route: str) -> str:
        start = source.find(route)
        if start < 0:
            return ""
        end = source.find("\n});", start)
        return source[start:end if end > 0 else None]


    def main() -> int:
        failures: list[str] = []

        api_routes = read(API_APPLICATIONS)
        html_routes = read(HTML_APPLICATIONS)
        verification = read(DISCORD_VERIFICATION)
        discord = read(DISCORD)

        api_verify = api_routes.find("verifyForSubmission(getDiscordVerifier(), body.discordVerification, body.discordCode,")
        api_create = api_routes.find("createApplication(result.value")
        require(api_verify >= 0, "#3: JSON intake must verify the Discord code before storing an application", failures)
        require(api_create >= 0 and 0 <= api_verify < api_create,
                "#3: JSON intake must verify the Discord account before createApplication", failures)
        require("result.value.discordId = verified.discordId;" in api_routes,
                "#3: the stored discordId must come from the verified account", failures)
        require("result.value.discordHandle = verified.username;" in api_routes,
                "#3: the stored Discord handle must be the verified account's handle", failures)
        require("body.discordId" not in api_routes and "body.discordId" not in html_routes,
                "#3: a submitted body.discordId must never be trusted", failures)

        html_verify = html_routes.find("verifyForSubmission(verifier, body.discordVerification, body.discordCode,")
        html_create = html_routes.find("createApplication(result.value")
        require(html_verify >= 0, "#3: no-JavaScript submits must verify the Discord code before storing an application", failures)
        require(html_create >= 0 and 0 <= html_verify < html_create,
                "#3: no-JavaScript submits must verify the Discord account before createApplication", failures)
        require("result.value.discordId = verified.discordId;" in html_routes,
                "#3: the no-JavaScript path must store the verified account id", failures)
        require(not re.search(r"discord-membership|discordMembership", api_routes),
                "#3: the removed username-only membership endpoint must not come back", failures)

        require("records.insert.run(id, account, username, codeHash, expiresAt)" in verification
                and "INSERT INTO discord_verifications (id, discord_id," in verification
                and "const account = lookup.member.id;" in verification,
                "#3: a verification record must be bound to the stable Discord account id", failures)
        require("if (!verificationId)" in verification, "#3: submissions without a verification id must be refused", failures)
        require("return record && record.verified ? record : null;" in verification
                and "const record = verifiedRecord(verificationId);" in verification
                and "records.remove.run(record.id);" in verification
                and "verifier.redeem(verificationId, save)" in verification,
                "#3: only a confirmed verification record may be consumed", failures)
        require("if (!interaction.guild.members.fetch(application.discordId))" not in discord, "#3: sanity", failures)
        require("application.discordId" in discord,
                "#3: role assignment must use the verified Discord account id", failures)
        require("findGuildMemberByUsername" in discord,
                "#3: the username lookup remains only the DM target lookup", failures)

        rate_limit = read(RATE_LIMIT)
        require("req.ip" in rate_limit, "#4: the limiter must derive the address from Express req.ip", failures)
        require("MAX_BUCKETS" in rate_limit and "pruneToCapacity" in rate_limit,
                "#4: the bucket map must have a hard cap with eviction", failures)
        rate_limit_code = re.sub(r"//[^\n]*", "", rate_limit).lower()
        require("split(',')[0]" not in rate_limit_code and "x-forwarded-for" not in rate_limit_code,
                "#4: the limiter must not parse raw forwarding headers itself", failures)
        app = read(APP)
        require('app.set("trust proxy", typeof config.trustProxy === "number" ? config.trustProxy : 1)' in app,
                "#4: trust proxy must use an explicit hop count", failures)
        require("trustProxyBoundary" in app and "x-forwarded-for" in app,
                "#4: untrusted forwarding headers must be stripped at the trust boundary", failures)
        config = read(CONFIG)
        require("parseTrustProxy" in config and "trustProxy: parseTrustProxy(process.env.TRUST_PROXY)" in config,
                "#4: TRUST_PROXY must accept an explicit hop count and default off", failures)

        mailcow = read(MAILCOW)
        require("requireTLS: !config.mailcow.secure" in mailcow,
                "#12: STARTTLS must be required whenever secure is false", failures)
        require("ignoreTLS: false" in mailcow, "#12: STARTTLS downgrade must be disabled", failures)
        require('minVersion: "TLSv1.2"' in mailcow, "#12: the SMTP TLS minimum version must be pinned", failures)
        require("secure: config.mailcow.secure" in mailcow, "#12: the implicit-TLS port-465 path must remain selectable", failures)

        package = json.loads(read(PACKAGE_JSON))
        lock = json.loads(read(PACKAGE_LOCK))
        declared_range = package["dependencies"]["nodemailer"]
        locked = lock["packages"]["node_modules/nodemailer"]
        locked_version = locked["version"]
        require(version_tuple(locked_version) >= (10, 0, 13),
                f"#13: nodemailer must be at least 10.0.13 (locked {locked_version})", failures)
        require(declared_range.lstrip("^~") == locked_version,
                "#13: the declared nodemailer range must match the locked version", failures)
        require(lock["packages"][""]["dependencies"]["nodemailer"] == declared_range,
                "#13: the lockfile root dependency range must match package.json", failures)
        require(locked["resolved"] == f"https://registry.npmjs.org/nodemailer/-/nodemailer-{locked_version}.tgz",
                "#13: the lockfile must resolve the published nodemailer tarball", failures)
        require(locked["integrity"].startswith("sha512-"),
                "#13: the lockfile must keep a sha512 integrity hash", failures)

        db = read(DB_INDEX)
        require("DATA_DIR_MODE = 0o700" in db, "#14: the data directory must be created 0700", failures)
        require("mode: DATA_DIR_MODE" in db, "#14: mkdir must request the private directory mode", failures)
        require("process.umask(0o077)" in db, "#14: database creation must run under a restrictive umask", failures)
        require("DATABASE_FILE_MODE = 0o600" in db, "#14: the database file must be 0600", failures)
        require('DATABASE_SIDECAR_SUFFIXES = ["-wal", "-shm"]' in db, "#14: the WAL and SHM sidecars must be covered", failures)
        require("chmodSync" in db and 'process.platform === "win32"' in db,
                "#14: permission tightening must go through chmod on POSIX only", failures)
        require("if (created !== undefined)" in db,
                "#14: the data directory must be chmodded only when this call created it", failures)
        require('error.code === "EPERM" || error.code === "EACCES"' in db,
                "#14: EPERM and EACCES on an existing file must warn and continue, not abort startup", failures)
        require(db.find("restrictDatabaseFiles(config.databasePath);") < db.find("const applied = runMigrations(db);"),
                "#14: the database file must be tightened before migrations write", failures)
        require(db.rfind("restrictDatabaseFiles(config.databasePath);") > db.find("const applied = runMigrations(db);"),
                "#14: the sidecars must be tightened again after migrations", failures)

        repo_js = read(REPO_JS)
        updates_js = read(UPDATES_JS)
        for name, source in (("repo.js", repo_js), ("updates.js", updates_js)):
            require("DOMPurify.sanitize(" in source, f"#15: {name} must sanitize through DOMPurify", failures)
            require("USE_PROFILES: { html: true }" in source, f"#15: {name} must use the DOMPurify html profile", failures)
            require("afterSanitizeAttributes" in source, f"#15: {name} must strip dangerous URI schemes through a DOMPurify hook", failures)
            require("data|vbscript|javascript" in source, f"#15: {name} must remove data:, vbscript: and javascript: URLs", failures)
            require('"style"' in source, f"#15: {name} must forbid the style attribute", failures)
            require('"base"' in source, f"#15: {name} must forbid base elements", failures)
            require('"iframe"' in source and '"object"' in source and '"embed"' in source,
                    f"#15: {name} must forbid iframe, object and embed elements", failures)
            require("querySelectorAll(\"script, iframe" not in source,
                    f"#15: {name} must not fall back to the hand-rolled blocklist", failures)
        repo_sanitizer = js_function_body(repo_js, "const sanitizeReadmeHtml = (html) =>")
        require(repo_sanitizer and "template.innerHTML = html" not in repo_sanitizer,
                "#15: repo.js sanitizer must not parse untrusted HTML itself", failures)
        require("typeof DOMPurify === \"undefined\" || typeof DOMPurify.sanitize !== \"function\"" in updates_js,
                "#15: updates.js must fail closed when DOMPurify is missing", failures)
        require('typeof marked === "undefined"' in updates_js and 'typeof marked.parse !== "function"' in updates_js,
                "#15: updates.js markdown fallback must stay escaped text", failures)

        updates_route = route_block(read(PAGES_ROUTE), 'router.get("/updates"')
        require('"/static/js/vendor/dompurify.min.js"' in updates_route,
                "#15: the updates route must load DOMPurify", failures)
        require(updates_route.find('"/static/js/vendor/dompurify.min.js"') < updates_route.find('"/static/js/updates.js"'),
                "#15: DOMPurify must load before updates.js", failures)
        repo_template = read(REPO_TEMPLATE)
        require(repo_template.find('src="/static/js/vendor/dompurify.min.js"') < repo_template.find('<%- include("../partials/scripts") %>'),
                "#15: DOMPurify must load before repo.js in the repo template", failures)
        dompurify = read(DOMPURIFY)
        require("DOMPurify" in dompurify and "@license" in dompurify and len(dompurify) > 15000,
                "#15: the vendored DOMPurify dist must be present", failures)

        require("style-src-elem 'self'" in app,
                "#15: inline style elements must be blocked by CSP", failures)
        require("style-src-attr 'none'" in app,
                "#15: inline style attributes must be blocked by CSP", failures)
        require("'unsafe-inline'" not in app,
                "#15: the CSP must not allow inline styles now that the front end uses classes and CSSOM", failures)
        require("repo-outline__item--d${depth}" in repo_js,
                "#15: outline indentation must use classes rather than an inline style attribute", failures)
        require("--outline-depth" not in repo_js and "--outline-depth" not in read(STYLES_CSS),
                "#15: the inline outline custom property must be gone", failures)
        require("segment.style.width" in repo_js,
                "#15: the language bar widths must be applied through CSSOM, not a style attribute", failures)
        require("style=" not in repo_js and "style=" not in updates_js,
                "#15: generated repository markup must not carry inline style attributes", failures)
        require("img-src 'self' data: ${gitOrigin} https://cdn.jsdelivr.net" in app and "data:image/svg+xml" in read(STYLES_CSS),
                "#15: the img-src data: allowance must remain because site CSS uses data: SVG backgrounds", failures)

        require("adminAuthLimiter" in api_routes, "#19: admin token routes need a dedicated limiter", failures)
        require(api_routes.count("adminAuthLimiter, requireAdminToken") >= 5,
                "#19: every admin-token route must run the dedicated limiter first", failures)
        require("ADMIN_TOKEN_MIN_LENGTH" in config and "ADMIN_TOKEN_MIN_UNIQUE" in config,
                "#19: the admin token must meet a minimum entropy requirement", failures)
        require("adminTokenEntropyIssue" in config and "throw new Error(message)" in config,
                "#19: weak admin tokens must fail closed at startup", failures)
        require("ADMIN_TOKEN_ALLOW_WEAK" in config, "#19: any weak-token escape hatch must be explicit", failures)
        require("ADMIN_TOKEN is set but too weak" in config and "${config.adminToken}" not in config,
                "#19: the entropy error must never include the token value", failures)
        auth = read(AUTH_MIDDLEWARE)
        require("timingSafeEqual" in auth, "#19: admin token comparison must stay constant time", failures)
        require("console." not in auth, "#19: the admin token middleware must never log", failures)

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}")
            return 1

        print("PASS: website security contracts (#3 #4 #12 #13 #14 #15 #19)")
        return 0


    if True:
        _merge_exit(main())

def _section_016_test_world_policy_generator():
    """Contract moved verbatim from tests/test_world_policy_generator.py."""
    __file__ = str(_MERGE_ROOT / "tests/test_world_policy_generator.py")
    import inspect
    import json
    import subprocess
    import sys
    import tempfile
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[1]
    GENERATOR = ROOT / "scripts/generate-world-policy.py"
    VERIFIER = ROOT / "scripts/verify-world-policy-packaging.py"

    RADICLE_POLICY = {
        "schema": "co.world-policy/1",
        "plugin": "Fallout4.esm",
        "npcs": [
            {"formkey": "0101CA6E:Fallout4.esm", "class": "story_unique"},
            {"formkey": "0101D162:Fallout4.esm", "class": "companion"},
            {"formkey": "0001CA70:Fallout4.esm", "class": "vendor"},
            {"formkey": "0101CA71:Fallout4.esm", "class": "town_npc"},
            {"formkey": "0101CA73:Fallout4.esm", "class": "enemy"},
            {"formkey": "0101CA74:Fallout4.esm", "class": "animal"},
        ],
        "doors": [{"formkey": "0101CA75:Fallout4.esm"}],
    }


    def run(script, *arguments):
        return subprocess.run(
            [sys.executable, str(script), *arguments],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )


    def test_generator_self_test_passes():
        result = run(GENERATOR, "--self-test")
        assert result.returncode == 0, result.stdout + result.stderr
        assert "self-test PASS" in result.stdout


    def test_generator_output_satisfies_packaging_verifier(tmp_path):
        source = tmp_path / "radicle.json"
        source.write_text(json.dumps(RADICLE_POLICY), encoding="utf-8")
        out = tmp_path / "world-policy.json"

        generated = run(GENERATOR, "--source", str(source), "--out", str(out))
        assert generated.returncode == 0, generated.stdout + generated.stderr

        data = json.loads(out.read_text(encoding="utf-8"))
        assert data["game"] == "Fallout4.esm"
        assert data["version"] == 1
        assert "sample" not in data
        assert data["classes"]["story"] == ["01CA6E:Fallout4.esm"]
        assert data["classes"]["companion"] == ["01D162:Fallout4.esm"]
        assert data["classes"]["vendor"] == ["01CA70:Fallout4.esm"]
        assert data["classes"]["town"] == ["01CA71:Fallout4.esm"]
        assert data["classes"]["enemy"] == ["01CA73:Fallout4.esm"]
        assert data["classes"]["animal"] == ["01CA74:Fallout4.esm"]
        assert data["classes"]["settler"] == []
        assert data["doors"] == ["01CA75:Fallout4.esm"]

        verified = run(VERIFIER, str(tmp_path))
        assert verified.returncode == 0, verified.stdout + verified.stderr


    def test_generator_rejects_a_foreign_schema(tmp_path):
        source = tmp_path / "radicle.json"
        source.write_text(json.dumps({"schema": "co.something-else/1"}), encoding="utf-8")

        result = run(GENERATOR, "--source", str(source), "--out", str(tmp_path / "world-policy.json"))
        assert result.returncode == 1
        assert "schema" in result.stderr
        assert not (tmp_path / "world-policy.json").exists()


    def test_generator_rejects_a_malformed_form_key(tmp_path):
        policy = json.loads(json.dumps(RADICLE_POLICY))
        policy["npcs"].append({"formkey": "001CA6E:Fallout4.esm", "class": "enemy"})
        source = tmp_path / "radicle.json"
        source.write_text(json.dumps(policy), encoding="utf-8")

        result = run(GENERATOR, "--source", str(source), "--out", str(tmp_path / "world-policy.json"))
        assert result.returncode == 1
        assert "001CA6E" in result.stderr


    def test_generator_rejects_a_load_order_byte_without_a_plugin(tmp_path):
        policy = json.loads(json.dumps(RADICLE_POLICY))
        policy["npcs"].append({"formkey": "051CA6E0", "class": "enemy"})
        source = tmp_path / "radicle.json"
        source.write_text(json.dumps(policy), encoding="utf-8")

        result = run(GENERATOR, "--source", str(source), "--out", str(tmp_path / "world-policy.json"))
        assert result.returncode == 1
        assert "051CA6E0" in result.stderr
        assert not (tmp_path / "world-policy.json").exists()


    def test_generator_rejects_non_object_rows(tmp_path):
        policy = json.loads(json.dumps(RADICLE_POLICY))
        policy["npcs"].append("0101CA77:Fallout4.esm")
        policy["doors"].append(7)
        source = tmp_path / "radicle.json"
        source.write_text(json.dumps(policy), encoding="utf-8")

        result = run(GENERATOR, "--source", str(source), "--out", str(tmp_path / "world-policy.json"))
        assert result.returncode == 1
        assert "Traceback" not in result.stderr
        assert "npcs: row is not an object" in result.stderr
        assert "doors: row is not an object" in result.stderr


    def test_generator_keeps_the_previous_output_when_the_verifier_fails(tmp_path):
        policy = {"schema": "co.world-policy/1", "plugin": "Fallout4.esm", "npcs": [], "doors": []}
        source = tmp_path / "radicle.json"
        source.write_text(json.dumps(policy), encoding="utf-8")
        out = tmp_path / "world-policy.json"
        out.write_text("previous", encoding="utf-8")

        result = run(GENERATOR, "--source", str(source), "--out", str(out))
        assert result.returncode == 1
        assert out.read_text(encoding="utf-8") == "previous"
        assert sorted(path.name for path in tmp_path.iterdir()) == ["radicle.json", "world-policy.json"]


    def main() -> int:
        failures: list[str] = []
        tests = [(name, value) for name, value in sorted(_merge_ns.items()) if name.startswith("test_") and callable(value)]
        for name, func in tests:
            try:
                if "tmp_path" in inspect.signature(func).parameters:
                    with tempfile.TemporaryDirectory() as directory:
                        func(Path(directory))
                else:
                    func()
            except AssertionError as exc:
                failures.append(f"{name}: {exc}")
            except Exception as exc:  # noqa: BLE001 - report, don't hide, unexpected errors
                failures.append(f"{name}: unexpected {type(exc).__name__}: {exc}")

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}")
            return 1

        print(f"{Path(__file__).stem}: {len(tests)} checks passed")
        return 0


    _merge_ns = dict(locals())
    if True:
        _merge_exit(main())



_SECTIONS = [
    ("tests/audit/test_trade_protocol_contract.py", _section_013_test_trade_protocol_contract),
    ("tests/audit/test_web_xss_security_contract.py", _section_014_test_web_xss_security_contract),
    ("tests/audit/test_website_security_contract.py", _section_015_test_website_security_contract),
    ("tests/test_world_policy_generator.py", _section_016_test_world_policy_generator),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "test_misc_contracts_part3: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
