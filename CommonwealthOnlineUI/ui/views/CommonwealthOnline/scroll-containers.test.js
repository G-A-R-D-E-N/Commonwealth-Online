const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const viewsRoot = __dirname;
const rules = fs.readdirSync(viewsRoot, { recursive: true })
  .filter(name => name.endsWith(".css"))
  .flatMap(name => {
    const css = fs.readFileSync(path.join(viewsRoot, name), "utf8").replace(/\/\*[\s\S]*?\*\//g, "");
    return [...css.matchAll(/([^{}]+)\{([^{}]*)\}/g)].map(match => ({
      selectors: match[1].split(",").map(selector => selector.trim()).filter(Boolean),
      body: match[2],
    }));
  });

const keyOf = selector => selector.replace(/::?[\w-]+(\([^)]*\))?/g, "").split(/\s+/).pop();
const keysWhere = pattern => new Set(
  rules.filter(rule => pattern.test(rule.body)).flatMap(rule => rule.selectors.map(keyOf))
);
const themed = new Set(
  rules.flatMap(rule => rule.selectors.filter(selector => selector.includes("::-webkit-scrollbar")).map(keyOf))
);
const scrollers = keysWhere(/overflow(-y)?\s*:\s*(auto|scroll)\b/);
const clipped = keysWhere(/overflow-x\s*:\s*hidden\b/);
const gutter = keysWhere(/scrollbar-gutter\s*:\s*stable\b/);

test("every CO scroll container clips horizontally, reserves its gutter and is themed", () => {
  assert.ok(scrollers.has(".settings-rows"), "the scan must see the settings rows");
  const gaps = [...scrollers]
    .filter(key => !clipped.has(key) || !gutter.has(key) || !themed.has(key))
    .map(key => `${key} clipped=${clipped.has(key)} gutter=${gutter.has(key)} themed=${themed.has(key)}`);
  assert.deepEqual(gaps, []);
});
