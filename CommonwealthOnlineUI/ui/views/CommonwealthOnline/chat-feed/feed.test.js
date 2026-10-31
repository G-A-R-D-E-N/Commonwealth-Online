const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const source = fs.readFileSync(__dirname + "/feed.js", "utf8");
const styles = fs.readFileSync(__dirname + "/styles.css", "utf8");
const windowStyles = fs.readFileSync(__dirname + "/../chat/styles.css", "utf8");
const pauseStyles = fs.readFileSync(__dirname + "/../pause-menu/styles.css", "utf8");

const window = {};
const document = {
  readyState: "complete",
  getElementById() {
    return null;
  },
  addEventListener() {}
};
vm.runInNewContext(source, { document, window, setInterval() {}, Date, String, Array, Math, JSON });

const { createFeed, MAX_VISIBLE, FADE_MS } = window.coChatFeedModel;
const plain = (value) => JSON.parse(JSON.stringify(value));
const texts = (rows) => plain(rows.map((row) => row.message.text));

assert.equal(MAX_VISIBLE, 6, "the feed shows six messages");
assert.equal(FADE_MS, 10000, "messages fade out over ten seconds");

{
  const feed = createFeed();
  for (let index = 0; index < 8; index += 1) {
    feed.add({ text: "m" + index }, 0);
  }
  assert.deepEqual(texts(feed.view(0)), ["m2", "m3", "m4", "m5", "m6", "m7"], "the cap keeps the newest six");
  assert.ok(feed.view(0).every((row) => row.opacity === 1), "fresh messages are fully opaque");
}

{
  const feed = createFeed();
  feed.add({ text: "a" }, 0);
  assert.equal(feed.view(5000)[0].opacity, 0.5, "opacity falls linearly to half at five seconds");
  assert.ok(feed.view(9999)[0].opacity < 0.01, "opacity is nearly zero just before ten seconds");
  assert.equal(feed.view(10000).length, 0, "a message is dropped at ten seconds");
}

{
  const feed = createFeed();
  feed.add({ text: "a" }, 0);
  feed.add({ text: "b" }, 4000);
  assert.deepEqual(texts(feed.view(10000)), ["b"], "older messages drop while newer ones stay");
}

{
  const pointerRules = [...styles.matchAll(/([^{}]+)\{([^{}]*)\}/g)]
    .map(([, selectors, body]) => ({
      selectors: selectors.trim(),
      value: (body.match(/pointer-events\s*:\s*([^;]+);/) || [])[1]
    }))
    .filter((rule) => rule.value !== undefined)
    .map((rule) => ({ selectors: rule.selectors, value: rule.value.trim() }));

  assert.ok(pointerRules.length > 0, "the feed stylesheet must state its pointer policy");
  assert.deepEqual(
    pointerRules.filter((rule) => rule.value !== "none").map((rule) => rule.selectors + " -> " + rule.value),
    [],
    "every pointer-events rule in the feed must be none, so nothing in the feed can swallow a click"
  );
  assert.match(styles, /html,\s*body\s*\{[^}]*pointer-events:\s*none/,
    "the feed page must not take the pointer over the whole screen");

  assert.doesNotMatch(source, /addEventListener\(\s*["'](click|mousedown|pointerdown)["']/,
    "the feed must not register a pointer handler that could block a click");
  assert.doesNotMatch(source, /pointerEvents|pointer-events/,
    "the feed must not re-enable pointer events per line at runtime");

  assert.match(pauseStyles, /\.marker--location\s*\{[^}]*pointer-events:\s*auto/,
    "a map marker stays clickable, which is what a click through the feed has to reach");
  assert.match(pauseStyles, /\.marker--ally\s*\{[^}]*pointer-events:\s*auto/,
    "an ally marker stays clickable behind the feed too");
}

{
  const body = (css, selector) => {
    const start = css.indexOf("\n" + selector + " {");
    assert.ok(start >= 0, selector + " rule must exist");
    return css.slice(start, css.indexOf("}", start));
  };
  const feedRule = body(styles, ".chat-feed");
  const windowRule = body(windowStyles, ".co-chat");
  const decl = (rule, property) => (rule.match(new RegExp("\\n\\s*" + property + ":\\s*([^;]+);")) || [])[1];
  const norm = (value) => value.replace(/\s+/g, "").replace(",1)", ")").replace("var(--co-ui-scale)", "S");

  assert.match(feedRule, /position:\s*fixed/, "the feed is pinned to the screen, not to a menu");
  assert.equal(decl(feedRule, "left"), undefined, "the feed is not anchored to the left edge");
  assert.equal(norm(decl(feedRule, "right")), norm(decl(windowRule, "right")),
    "the feed sits on the chat window's right inset");
  assert.equal(norm(decl(feedRule, "bottom")), norm(decl(windowRule, "bottom")),
    "the feed sits on the chat window's bottom inset, so opening the window grows the same corner");
}

console.log("Chat feed regression passed");
