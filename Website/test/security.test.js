"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const { isExternalHttp, isSafeImage, isSafeLink, protocolOf } = require("../static/js/url-policy");

for (const unsafe of [
  "javascript:alert(1)",
  "java\tscript:alert(1)",
  "java\nscript:alert(1)",
  "java\rscript:alert(1)",
  "data:text/html,<script>alert(1)</script>",
  "vbscript:msgbox(1)",
]) {
  assert.equal(isSafeLink(unsafe, "https://example.com/"), false, unsafe);
}
assert.equal(protocolOf("java\nscript:alert(1)", "https://example.com/"), "javascript:");
assert.equal(isSafeLink("/relative", "https://example.com/"), true);
assert.equal(isSafeLink("mailto:test@example.com", "https://example.com/"), true);
assert.equal(isExternalHttp("//attacker.example/path", "https://example.com/"), true);
assert.equal(isExternalHttp("/local/path", "https://example.com/"), false);
assert.equal(isSafeImage("data:image/svg+xml,<svg></svg>", "https://example.com/"), false);
assert.equal(isSafeImage("data:image/png;base64,AA==", "https://example.com/"), true);

const updatesSource = fs.readFileSync(path.join(__dirname, "../static/js/updates.js"), "utf8");
for (const source of [updatesSource]) {
  assert.match(source, /allowedTags/);
  assert.match(source, /CoUrlPolicy/);
  assert.match(source, /removeAttribute\("target"\)/);
  assert.match(source, /removeAttribute\("rel"\)/);
  assert.match(source, /isExternalHttp/);
  assert.doesNotMatch(source, /a: new Set\(\["href", "rel", "target"/);
  assert.doesNotMatch(source, /\^javascript:/);
}

const workflow = fs.readFileSync(
  path.join(__dirname, "../../.github/workflows/co-website-static.yml"),
  "utf8"
);
assert.match(workflow, /actions\/configure-pages@983d7736d9b0ae728b81ab479565c72886d7745b/);
assert.match(workflow, /actions\/upload-pages-artifact@7b1f4a764d45c48632c6b24a0339c27f5614fb0b/);
assert.match(workflow, /actions\/deploy-pages@d6db90164ac5ed86f2b6aed7e0febac5b3c0c03e/);
assert.match(workflow, /sb_publishable_/);

console.log("security checks passed");
