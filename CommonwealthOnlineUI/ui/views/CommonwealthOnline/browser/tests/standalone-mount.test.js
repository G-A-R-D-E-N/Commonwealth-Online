const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const browserDir = path.join(__dirname, "..");
const html = fs.readFileSync(path.join(browserDir, "index.html"), "utf8");
const appJs = fs.readFileSync(path.join(browserDir, "app.js"), "utf8");

test("standalone browser mounts into a dedicated element, not document.body", () => {
  assert.match(html, /id="coBrowserMount"/);
  assert.doesNotMatch(appJs, /document\.body/);
  assert.match(appJs, /getElementById\("coBrowserMount"\)/);
});

test("standalone browser links every stylesheet the markup needs", () => {
  for (const sheet of ["./styles.css", "./modpack-detail.css", "./components/keyboard.css", "./browser-modal.css", "../settings/settings.css", "./components/server-settings.css"]) {
    assert.ok(html.includes(`<link rel="stylesheet" href="${sheet}">`), sheet);
  }
});

test("standalone browser provides a full-screen pointer surface", () => {
  const styles = fs.readFileSync(path.join(browserDir, "styles.css"), "utf8");
  assert.match(styles, /\.co-root\.co-browser--standalone\s*\{[^}]*position:\s*fixed;/);
});
