"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const ejs = require("ejs");

const template = fs.readFileSync(path.join(__dirname, "..", "views", "partials", "scripts.ejs"), "utf8");

const render = (page, site = {}) =>
  ejs.render(template, { page: { scripts: [], ...page }, site }, { filename: "scripts.ejs" });

// Captures only the page scripts (own static JS), excluding the CDN scripts
// and the separately versioned discord-link.js include.
const capturePageScripts = (html) => {
  const tags = [];
  for (const match of html.matchAll(/<script src="([^"]+)" defer><\/script>/g)) {
    const src = match[1];
    if (src.startsWith("/static/js/") && !src.includes("discord-link.js")) {
      tags.push(src);
    }
  }
  return tags;
};

const assertVersioned = (rendered, expected) => {
  const tags = capturePageScripts(rendered);
  assert.deepEqual(tags, expected, "rendered page script srcs");
  for (const src of tags) {
    assert.equal((src.match(/\?/g) || []).length, 1, `expected exactly one ? in ${src}`);
    assert.match(src, /v=20260922-1$/, `expected cache-bust version on ${src}`);
  }
};

assertVersioned(render({ scripts: ["/static/js/navbar.js"] }), [
  "/static/js/navbar.js?v=20260922-1",
]);

// A src that already carries a query string must merge with &, not add a
// second ?, so the cache-bust version can only be served stale.
assertVersioned(render({ scripts: ["/static/js/profile.js?cache-preload=1"] }), [
  "/static/js/profile.js?cache-preload=1&v=20260922-1",
]);

assertVersioned(
  render({
    scripts: [
      "/static/js/navbar.js",
      "/static/js/links.js?v=3",
      "/static/js/script.js?x=1&y=2",
    ],
  }),
  [
    "/static/js/navbar.js?v=20260922-1",
    "/static/js/links.js?v=3&v=20260922-1",
    "/static/js/script.js?x=1&y=2&v=20260922-1",
  ]
);

assertVersioned(render({ scripts: [] }), []);

// CDN supabase script must not render when unconfigured.
const withoutSupabase = render({ scripts: ["/static/js/member.js"] }, { supabase: null });
assert.match(withoutSupabase, /<script src="\/static\/js\/member\.js\?v=20260922-1" defer>/);
assert.doesNotMatch(withoutSupabase, /supabase-js@2\.105\.0/);

console.log("scripts cache-bust query merge checks passed");