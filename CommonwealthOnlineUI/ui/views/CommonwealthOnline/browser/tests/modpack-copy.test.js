const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const SOURCE = fs.readFileSync(path.join(__dirname, "..", "components", "modpack-copy.js"), "utf8");

function flush() {
  return new Promise((resolve) => setImmediate(resolve));
}

function load(options = {}) {
  const records = { written: [], execCommands: [], appended: [], removed: [] };
  const document = {
    body: {
      appendChild(node) {
        records.appended.push(node);
        node.parentNode = document.body;
      },
      removeChild(node) {
        records.removed.push(node);
        node.parentNode = null;
      }
    },
    createElement() {
      return {
        value: "",
        style: {},
        attributes: {},
        parentNode: null,
        setAttribute(name, value) {
          this.attributes[name] = value;
        },
        select() {
          this.selected = true;
        }
      };
    },
    execCommand(command) {
      records.execCommands.push(command);
      return options.execCommandResult === undefined ? true : options.execCommandResult;
    }
  };
  const sandbox = { document, navigator: options.clipboard === undefined ? {} : { clipboard: options.clipboard } };
  sandbox.window = sandbox;
  vm.createContext(sandbox);
  vm.runInContext(SOURCE, sandbox);
  return { api: sandbox.window.CO_ModpackCopy, records };
}

function overlay(onClick) {
  return {
    addEventListener(type, handler) {
      if (type === "click") onClick.handler = handler;
    }
  };
}

function mount() {
  const toggled = [];
  return {
    toggled,
    button: {
      classList: {
        toggle(name, value) {
          toggled.push([name, value]);
        }
      }
    },
    notice: { textContent: "" }
  };
}

function clickCopy(handler) {
  handler({ target: { closest: (selector) => (selector === "#coCopyModpackButton" ? {} : null) } });
}

test("writeClipboard reports success when the clipboard API accepts the text", async () => {
  const written = [];
  const { api, records } = load({
    clipboard: {
      writeText(value) {
        written.push(value);
        return Promise.resolve();
      }
    }
  });

  assert.equal(await api.writeClipboard("line one"), true);
  assert.deepEqual(written, ["line one"]);
  assert.deepEqual(records.execCommands, []);
});

test("writeClipboard falls back to execCommand when the clipboard API rejects", async () => {
  const { api, records } = load({
    clipboard: {
      writeText() {
        return Promise.reject(new Error("denied"));
      }
    },
    execCommandResult: true
  });

  assert.equal(await api.writeClipboard("line one"), true);
  assert.deepEqual(records.execCommands, ["copy"]);
  assert.equal(records.appended.length, 1);
  assert.equal(records.removed.length, 1);
  assert.equal(records.appended[0].value, "line one");
});

test("writeClipboard reports the fallback's own refusal", async () => {
  const { api } = load({
    clipboard: {
      writeText() {
        return Promise.reject(new Error("denied"));
      }
    },
    execCommandResult: false
  });

  assert.equal(await api.writeClipboard("line one"), false);
});

test("writeClipboard falls back when the clipboard API throws synchronously", async () => {
  const { api, records } = load({
    clipboard: {
      writeText() {
        throw new Error("not allowed");
      }
    }
  });

  assert.equal(await api.writeClipboard("line one"), true);
  assert.deepEqual(records.execCommands, ["copy"]);
});

test("writeClipboard uses execCommand when no clipboard API is present", async () => {
  const { api, records } = load({ execCommandResult: true });

  assert.equal(await api.writeClipboard("line one"), true);
  assert.deepEqual(records.execCommands, ["copy"]);
});

test("the Copy list button reports the real copy result", async () => {
  const handler = {};
  const parts = mount();
  const { api } = load({
    clipboard: {
      writeText() {
        return Promise.reject(new Error("denied"));
      }
    },
    execCommandResult: false
  });
  const copy = api.create({
    joinOverlay: overlay(handler),
    button: parts.button,
    notice: parts.notice,
    onDismiss() {}
  });

  copy.bind();
  copy.show(["a.sha  missing", "b.sha  differs"], true);
  clickCopy(handler.handler);
  await flush();

  assert.equal(parts.notice.textContent, "Copy failed.");
  assert.deepEqual(parts.toggled.at(-1), ["hidden", false]);
});

test("the Copy list button reports success from the clipboard API", async () => {
  const handler = {};
  const parts = mount();
  const written = [];
  const { api } = load({
    clipboard: {
      writeText(value) {
        written.push(value);
        return Promise.resolve();
      }
    }
  });
  const copy = api.create({
    joinOverlay: overlay(handler),
    button: parts.button,
    notice: parts.notice,
    onDismiss() {}
  });

  copy.bind();
  copy.show(["a.sha  missing", "b.sha  differs"], true);
  clickCopy(handler.handler);
  await flush();

  assert.deepEqual(written, ["a.sha  missing\nb.sha  differs"]);
  assert.equal(parts.notice.textContent, "Copied to clipboard.");
});

test("a dismiss click does not touch the clipboard", async () => {
  const handler = {};
  const parts = mount();
  const { api, records } = load({ execCommandResult: true });
  let dismissed = 0;
  const copy = api.create({
    joinOverlay: overlay(handler),
    button: parts.button,
    notice: parts.notice,
    onDismiss() {
      dismissed += 1;
    }
  });

  copy.bind();
  copy.show(["a.sha  missing"], true);
  handler.handler({ target: { closest: () => null } });
  await flush();

  assert.equal(dismissed, 1);
  assert.deepEqual(records.execCommands, []);
  assert.equal(parts.notice.textContent, "");
});
