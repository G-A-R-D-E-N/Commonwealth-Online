const assert = require("node:assert/strict");
const fs = require("node:fs");

const source = fs.readFileSync(__dirname + "/nameplates.js", "utf8");
const css = fs.readFileSync(__dirname + "/styles.css", "utf8");
const html = fs.readFileSync(__dirname + "/index.html", "utf8");

function fakeElement(tagName) {
  return {
    tagName: tagName,
    className: "",
    textContent: "",
    style: {},
    children: [],
    appendChild(child) {
      this.children.push(child);
      return child;
    },
    removeChild(child) {
      this.children = this.children.filter((item) => item !== child);
      return child;
    }
  };
}

function load() {
  const root = fakeElement("div");
  const window = {};
  const document = {
    getElementById(id) {
      return id === "coNameplates" ? root : null;
    },
    createElement(tagName) {
      return fakeElement(tagName);
    }
  };
  new Function("window", "document", source)(window, document);
  return { window, root };
}

const label = (id, text, x, y, opacity) => ({ id, text, x, y, opacity });

{
  const { window } = load();
  const model = window.coNameplatesModel;
  assert.deepEqual(model.parseLabels("not json"), [], "malformed JSON yields no labels");
  assert.deepEqual(model.parseLabels('{"id":1}'), [], "a non-array payload yields no labels");
  assert.deepEqual(model.parseLabels("[]"), [], "an empty list yields no labels");
  const kept = model.parseLabels(
    JSON.stringify([
      label(1, "[AT] Alice", 0.25, 0.5, 1),
      label(2, "Bob", 1.5, 0.5, 1),
      label(3, 42, 0.5, 0.5, 1),
      { id: 4, text: "Carol", x: 0.5, y: 0.5 },
      label(5, "Dave", 0.5, 0.5, 0.5)
    ])
  );
  assert.deepEqual(
    kept.map((entry) => entry.id),
    [1, 5],
    "entries with out-of-range position, bad text or missing opacity are dropped"
  );
}

{
  const { window, root } = load();
  window.coNameplatesSet(JSON.stringify([label(7, "[AT] Alice", 0.25, 0.5, 1)]));
  assert.equal(root.children.length, 1, "one label is created per player");
  const first = root.children[0];
  assert.equal(first.className, "co-nameplate");
  assert.equal(first.textContent, "[AT] Alice", "the text is set as plain text");
  assert.equal(first.style.left, "25%", "x is a fraction of the width");
  assert.equal(first.style.top, "50%", "y is already top-down from the plugin");
  assert.equal(first.style.opacity, "1");
}

{
  const { window, root } = load();
  window.coNameplatesSet(JSON.stringify([label(7, "Alice", 0.1, 0.1, 1)]));
  const first = root.children[0];
  window.coNameplatesSet(JSON.stringify([label(7, "Alice", 0.75, 0.125, 0.5)]));
  assert.equal(root.children.length, 1, "a repeated id reuses its element");
  assert.equal(root.children[0], first, "the same element is moved, not recreated");
  assert.equal(first.style.left, "75%");
  assert.equal(first.style.top, "12.5%");
  assert.equal(first.style.opacity, "0.5");
}

{
  const { window, root } = load();
  window.coNameplatesSet(JSON.stringify([label(1, "A", 0.1, 0.1, 1), label(2, "B", 0.2, 0.2, 1)]));
  assert.equal(root.children.length, 2);
  window.coNameplatesSet(JSON.stringify([label(2, "B", 0.2, 0.2, 1)]));
  assert.equal(root.children.length, 1, "a player that left the list loses its label");
  assert.equal(root.children[0].textContent, "B");
  window.coNameplatesSet("[]");
  assert.equal(root.children.length, 0, "an empty list clears every label");
  window.coNameplatesSet("garbage");
  assert.equal(root.children.length, 0, "malformed JSON clears every label");
}

{
  const { window, root } = load();
  window.coNameplatesSet(JSON.stringify([label(1, "<b>x</b>", 0.5, 0.5, 1)]));
  assert.equal(root.children[0].textContent, "<b>x</b>", "markup in a name is never interpreted");
}

assert.match(css, /\.co-nameplate\s*\{[^}]*pointer-events:\s*none/, "labels are click-through");
assert.match(css, /\.co-nameplates\s*\{[^}]*pointer-events:\s*none/, "the layer is click-through");
assert.match(html, /id="coNameplates"/, "the index page has the label layer");
assert.match(html, /<script src="nameplates\.js"><\/script>/, "the index page loads the script");
assert.doesNotMatch(html + css, /https?:\/\//, "the view has no external dependencies");
