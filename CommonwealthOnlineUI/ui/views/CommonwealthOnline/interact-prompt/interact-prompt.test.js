const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const source = fs.readFileSync(__dirname + "/interact-prompt.js", "utf8");
const markup = fs.readFileSync(__dirname + "/index.html", "utf8");
const preview = fs.readFileSync(__dirname + "/preview.html", "utf8");
const ids = ["coInteractPrompt", "coInteractKey", "coInteractLabel"];

function fakeElement(id) {
  return {
    id,
    hidden: false,
    textContent: "",
    attributes: {},
    setAttribute(name, value) {
      this.attributes[name] = String(value);
    },
    getAttribute(name) {
      return this.attributes[name];
    }
  };
}

function load() {
  const elements = {};
  for (const id of ids) {
    elements[id] = fakeElement(id);
  }
  const window = {};
  const document = {
    getElementById: (id) => elements[id] || null
  };
  vm.runInNewContext(source, { document, window });
  return { window, elements };
}

const plain = (value) => JSON.parse(JSON.stringify(value));
const { window, elements } = load();
const model = window.coInteractPromptModel;

const cases = [];
function test(name, fn) {
  cases.push([name, fn]);
}

test("a visible state shows the prompt with the player's name", () => {
  window.coInteractPromptSetState(JSON.stringify({ visible: true, name: "Sole Survivor" }));
  assert.equal(elements.coInteractPrompt.hidden, false);
  assert.equal(elements.coInteractLabel.textContent, "Interact with Sole Survivor");
});

test("a hidden state hides the prompt", () => {
  window.coInteractPromptSetState(JSON.stringify({ visible: true, name: "Sole Survivor" }));
  window.coInteractPromptSetState(JSON.stringify({ visible: false }));
  assert.equal(elements.coInteractPrompt.hidden, true);
});

test("malformed input hides the prompt", () => {
  const inputs = ["{not json", "null", "[]", "\"text\"", null, 7];
  for (const input of inputs) {
    window.coInteractPromptSetState(JSON.stringify({ visible: true, name: "A" }));
    window.coInteractPromptSetState(input);
    assert.equal(elements.coInteractPrompt.hidden, true, JSON.stringify(input));
  }
});

test("a missing or empty name falls back to Unknown player", () => {
  window.coInteractPromptSetState(JSON.stringify({ visible: true }));
  assert.equal(elements.coInteractLabel.textContent, "Interact with Unknown player");
  window.coInteractPromptSetState(JSON.stringify({ visible: true, name: "" }));
  assert.equal(elements.coInteractLabel.textContent, "Interact with Unknown player");
});

test("a name is written as text, never as markup", () => {
  const hostile = "<img src=x onerror=alert(1)>";
  window.coInteractPromptSetState(JSON.stringify({ visible: true, name: hostile }));
  assert.equal(elements.coInteractLabel.textContent, "Interact with " + hostile);
  assert.equal(source.includes("innerHTML"), false);
});

test("an object argument is accepted as well as a JSON string", () => {
  window.coInteractPromptSetState({ visible: true, name: "Minuteman" });
  assert.equal(elements.coInteractPrompt.hidden, false);
  assert.equal(elements.coInteractLabel.textContent, "Interact with Minuteman");
});

test("visible must be exactly true", () => {
  window.coInteractPromptSetState(JSON.stringify({ visible: "yes", name: "A" }));
  assert.equal(elements.coInteractPrompt.hidden, true);
  window.coInteractPromptSetState(JSON.stringify({ visible: 1, name: "A" }));
  assert.equal(elements.coInteractPrompt.hidden, true);
});

test("the model reports visibility and the display name", () => {
  assert.deepEqual(plain(model.describePrompt({ visible: true, name: "Preston" })), { visible: true, name: "Preston" });
  assert.deepEqual(plain(model.describePrompt("text")), { visible: false, name: "Unknown player" });
});

test("the key cap reads ACTIVATE and the script never rewrites it", () => {
  assert.ok(markup.includes('id="coInteractKey">ACTIVATE</span>'));
  assert.equal(source.includes("coInteractKey"), false);
});

test("index.html carries every element the script paints and loads the script and theme", () => {
  for (const id of ids) {
    assert.ok(markup.includes(`id="${id}"`), `index.html has #${id}`);
  }
  const looked = [...source.matchAll(/getElementById\("(\w+)"\)/g)].map((match) => match[1]);
  assert.deepEqual([...new Set(looked)].sort(), ["coInteractLabel", "coInteractPrompt"]);
  assert.ok(markup.includes('<script src="interact-prompt.js"></script>'));
  assert.ok(markup.includes('href="styles.css"'));
  assert.ok(markup.includes('href="../pipboy-theme.css"'));
});

test("preview.html loads the same view files and pushes samples through the entry point", () => {
  assert.ok(preview.includes('<script src="interact-prompt.js"></script>'));
  assert.ok(preview.includes('href="styles.css"'));
  assert.ok(preview.includes("window.coInteractPromptSetState"));
});

let failed = 0;
for (const [name, fn] of cases) {
  try {
    fn();
    console.log(`PASS ${name}`);
  } catch (error) {
    failed += 1;
    console.log(`FAIL ${name}`);
    console.log(error.message);
  }
}
if (failed > 0) {
  console.log(`interact prompt tests FAILED: ${failed} of ${cases.length}`);
  process.exitCode = 1;
} else {
  console.log(`interact prompt tests passed: ${cases.length} cases`);
}
