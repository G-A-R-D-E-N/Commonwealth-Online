const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

function run(source, location) {
  const classes = [];
  const stack = {
    addEventListener() {},
    querySelectorAll() {
      return [];
    },
    querySelector() {
      return null;
    },
    set innerHTML(value) {}
  };
  const document = {
    readyState: "complete",
    documentElement: {
      classList: {
        add(value) {
          classes.push(value);
        }
      },
      style: { setProperty() {} }
    },
    getElementById() {
      return stack;
    },
    addEventListener() {}
  };
  const window = { location, innerWidth: 1920, addEventListener() {} };

  vm.runInNewContext(source, {
    document,
    window,
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
    Date,
    Math,
    JSON,
    String,
    Array,
    Object
  });

  return new Promise(resolve => {
    setTimeout(() => resolve({
      previewClass: classes.includes("is-preview"),
      queue: window.getCoAlertQueue()
    }), 700);
  });
}

async function main() {
  const alerts = fs.readFileSync(__dirname + "/alerts.js", "utf8");
  const production = await run(alerts, { protocol: "file:", search: "" });
  const explicitDemo = await run(alerts, { protocol: "file:", search: "?demo=1" });
  const previewHtml = fs.readFileSync(__dirname + "/preview.html", "utf8");
  const previewScript = [...previewHtml.matchAll(/<script(?:\s+src="[^"]+")?>([\s\S]*?)<\/script>/g)]
    .map(match => match[1])
    .filter(Boolean)
    .pop();
  const preview = await run(alerts + "\n" + previewScript, { protocol: "file:", search: "" });

  assert.equal(production.previewClass, false);
  assert.equal(production.queue.length, 0);
  assert.equal(explicitDemo.previewClass, true);
  assert.equal(explicitDemo.queue.length, 4);
  assert.equal(preview.previewClass, true);
  assert.equal(preview.queue.length, 4);
}

main().then(() => {
  console.log("Alert overlay regression passed");
});
