const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

test("map pan writes the transform on the map world, not CSS variables", () => {
  const source = fs.readFileSync(path.join(__dirname, "script.js"), "utf8");
  const start = source.indexOf("function applyMapTransform() {");
  const end = source.indexOf("function getPanLimits()", start);
  assert.ok(start >= 0 && end > start);
  assert.doesNotMatch(source.slice(start, end), /setProperty\(\s*"--map-pan/);
});

test("map world rule has no filter", () => {
  const css = fs.readFileSync(path.join(__dirname, "styles.css"), "utf8");
  const rule = css.match(/\.map-world\s*\{[^}]*\}/);
  assert.ok(rule);
  assert.doesNotMatch(rule[0], /filter:/);
});

test("fog explored cells are smoothed instead of drawn as hard blocks", () => {
  const source = fs.readFileSync(path.join(__dirname, "script.js"), "utf8");
  const start = source.indexOf("function clearExploredFog(");
  const end = source.indexOf("function drawMapFog()", start);
  assert.ok(start >= 0 && end > start);
  const body = source.slice(start, end);
  assert.match(body, /imageSmoothingEnabled = true/);
  assert.match(body, /drawImage\(fogMask/);
  assert.doesNotMatch(body, /Math\.floor\(runStart \* cellWidth\)/);
});

test("screen effects scanlines do not use a blend mode", () => {
  const css = fs.readFileSync(path.join(__dirname, "styles.css"), "utf8");
  assert.doesNotMatch(css, /mix-blend-mode/);
});
