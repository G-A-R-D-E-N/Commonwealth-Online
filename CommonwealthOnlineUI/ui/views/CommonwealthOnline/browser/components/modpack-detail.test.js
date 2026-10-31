"use strict";

const assert = require("node:assert");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

function makeList() {
  const classes = new Set();
  const node = {
    items: [],
    innerHTML: "",
    className: "",
    ownerDocument: {
      createElement() {
        return { className: "", textContent: "" };
      }
    },
    classList: {
      toggle(name, on) {
        if (on) classes.add(name);
        else classes.delete(name);
      },
      contains(name) {
        return classes.has(name);
      }
    },
    appendChild(child) {
      node.items.push(child);
    }
  };
  Object.defineProperty(node, "hidden", {
    get() {
      return classes.has("hidden");
    }
  });
  return node;
}

function loadModule() {
  const source = fs.readFileSync(path.join(__dirname, "modpack-detail.js"), "utf8");
  const window = {};
  vm.runInNewContext(source, { window: window }, { filename: "modpack-detail.js" });
  return window.CO_ModpackDetail;
}

test("lists order and extra-plugin differences as entries", () => {
  const detail = loadModule();
  const result = detail.split(
    "Your modpack does not match this server.\n" +
    "Differences from this server:\n" +
    "  Unexpected: ExtraMod.esp\n" +
    "  Wrong load order: WorkshopFramework.esm\n" +
    "  Missing: Munitions.esp\n" +
    "  ...and 2 more"
  );
  assert.equal(result.headline, "Your modpack does not match this server.");
  assert.deepEqual(Array.from(result.lines), [
    "Unexpected: ExtraMod.esp",
    "Wrong load order: WorkshopFramework.esm",
    "Missing: Munitions.esp",
    "...and 2 more"
  ]);
  assert.deepEqual(Array.from(result.warnings), []);
});

test("keeps the extra F4SE warning list separate from the differences", () => {
  const detail = loadModule();
  const result = detail.split(
    "Differences from this server:\n" +
    "  Missing: Munitions.esp\n" +
    "Extra F4SE plugins (allowed, not required):\n" +
    "  AddressLibrary.dll"
  );
  assert.deepEqual(Array.from(result.lines), ["Missing: Munitions.esp"]);
  assert.deepEqual(Array.from(result.warnings), ["AddressLibrary.dll"]);
  assert.equal(result.headline, detail.headline);
});

test("keeps the required modlist line in the headline above the differences", () => {
  const detail = loadModule();
  const result = detail.split(
    "This server requires the Main List modlist.\n" +
    "Your modpack does not match this server. The server manifest is in requiredModpack.\n" +
    "Differences from this server:\n" +
    "  Missing: Munitions.esp"
  );
  assert.equal(
    result.headline,
    "This server requires the Main List modlist. Your modpack does not match this server. The server manifest is in requiredModpack."
  );
  assert.deepEqual(Array.from(result.lines), ["Missing: Munitions.esp"]);
});

test("keeps a plain failure text as the headline and shows no list", () => {
  const detail = loadModule();
  const result = detail.split("Wrong password.");
  assert.equal(result.headline, "Wrong password.");
  assert.deepEqual(Array.from(result.lines), []);
  assert.deepEqual(Array.from(result.warnings), []);
});

test("show() fills the difference and warning lists and hides the warning title when empty", () => {
  const detail = loadModule();
  const containers = {};
  const scope = {
    querySelector(selector) {
      return containers[selector] || (containers[selector] = makeList());
    }
  };
  detail.show(scope, detail.split("Differences from this server:\n  Missing: A.esp"));
  assert.equal(containers["#coModpackDetail"].items.length, 1);
  assert.equal(containers["#coModpackWarnings"].items.length, 0);
  assert.equal(containers["#coModpackWarningsTitle"].hidden, true);
  detail.show(scope, detail.split("Extra F4SE plugins (allowed, not required):\n  AddressLibrary.dll"));
  assert.equal(containers["#coModpackWarnings"].items.length, 1);
  assert.equal(containers["#coModpackWarningsTitle"].hidden, false);
});

test("maps each difference label to a visible severity class", () => {
  const detail = loadModule();
  assert.equal(detail.splitLabel("Wrong load order: B.esp").className, "co-modpack-detail-entry--order");
  assert.equal(detail.splitLabel("Unexpected: Extra.esp").className, "co-modpack-detail-entry--unexpected");
  assert.equal(detail.splitLabel("Missing: A.esp").className, "co-modpack-detail-entry--missing");
  assert.equal(detail.splitLabel("Different archive: a.ba2").className, "co-modpack-detail-entry--content");
  assert.equal(detail.splitLabel("Alpha.esp: required by server"), null);
});
