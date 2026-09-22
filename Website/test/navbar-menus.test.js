"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const source = fs.readFileSync(path.join(__dirname, "..", "static", "js", "navbar.js"), "utf8");

const listenersOf = () => {
  const listeners = new Map();
  return {
    listeners,
    addEventListener(name, callback) {
      listeners.set(name, callback);
    },
  };
};

const makeTrigger = () => {
  const attributes = new Map([["aria-expanded", "false"]]);
  const state = { open: false, blurred: false };
  const triggerListeners = listenersOf();
  return {
    element: {
      getAttribute(name) {
        return attributes.get(name) ?? null;
      },
      setAttribute(name, value) {
        attributes.set(name, value);
        if (name === "aria-expanded") {
          state.open = value === "true";
        }
      },
      addEventListener: triggerListeners.addEventListener,
      blur() {
        state.blurred = true;
      },
    },
    state,
    listeners: triggerListeners.listeners,
  };
};

const makeGroup = (trigger) => {
  const state = { open: false };
  return {
    group: {
      classList: {
        add(name) {
          if (name === "is-open") {
            state.open = true;
          }
        },
        remove(name) {
          if (name === "is-open") {
            state.open = false;
          }
        },
        toggle(name, force) {
          if (name === "is-open") {
            state.open = force === undefined ? !state.open : force;
          }
        },
        contains(name) {
          return name === "is-open" && state.open;
        },
      },
      querySelector(selector) {
        return selector === "[data-nav-trigger]" ? trigger.element : null;
      },
    },
    state,
  };
};

class FakeElement {
  closest() {
    return null;
  }
}

const toggleAttributes = new Map([["aria-expanded", "false"]]);
const toggleListeners = listenersOf();
const toggle = {
  getAttribute(name) {
    return toggleAttributes.get(name) ?? null;
  },
  setAttribute(name, value) {
    toggleAttributes.set(name, value);
  },
  addEventListener: toggleListeners.addEventListener,
  classList: { add() {}, remove() {}, toggle() {} },
};

const nav = {
  addEventListener: listenersOf().addEventListener,
  classList: { add() {}, remove() {}, toggle() {} },
};

const triggerA = makeTrigger();
const triggerB = makeTrigger();
const groupA = makeGroup(triggerA);
const groupB = makeGroup(triggerB);

const mount = {
  dataset: {},
  querySelector(selector) {
    if (selector === ".site-nav-toggle") {
      return toggle;
    }
    if (selector === ".site-nav") {
      return nav;
    }
    return null;
  },
  querySelectorAll(selector) {
    return selector === "[data-nav-group]" ? [groupA.group, groupB.group] : [];
  },
};

const documentHandlers = new Map();
const documentObject = {
  querySelector(selector) {
    if (selector === "[data-co-navbar]") {
      return mount;
    }
    if (selector === ".site-brand__logo") {
      return { src: "https://example.com/assets/branding/logo.svg" };
    }
    return null;
  },
  addEventListener(name, callback) {
    documentHandlers.set(name, callback);
  },
};

const context = {
  URL,
  HTMLElement: FakeElement,
  document: documentObject,
  window: {
    matchMedia() {
      return { matches: true, addEventListener() {} };
    },
  },
};

const run = async () => {
  vm.runInNewContext(source, context, { filename: "navbar.js" });

  const openGroup = (trigger) => {
    trigger.listeners.get("click")({ stopPropagation() {} });
  };

  // Opening a group expands the trigger and adds .is-open.
  openGroup(triggerA);
  assert.equal(triggerA.state.open, true);
  assert.equal(groupA.state.open, true);
  assert.equal(groupB.state.open, false);

  // A second trigger click closes the first group before opening the next.
  openGroup(triggerA);
  assert.equal(triggerA.state.open, false);
  assert.equal(groupA.state.open, false);

  // Escape closes every open group and drops focus from its trigger so the
  // desktop :focus-within rule cannot keep the menu visible.
  openGroup(triggerA);
  openGroup(triggerB);
  documentHandlers.get("keydown")({ key: "Escape" });
  assert.equal(triggerA.state.open, false);
  assert.equal(triggerB.state.open, false);
  assert.equal(groupA.state.open, false);
  assert.equal(groupB.state.open, false);
  assert.equal(triggerA.state.blurred, true, "Escape must blur the focused trigger");
  assert.equal(triggerB.state.blurred, true, "Escape must blur the focused trigger");

  // Other keys must not close menus.
  openGroup(triggerA);
  documentHandlers.get("keydown")({ key: "Enter" });
  assert.equal(groupA.state.open, true);

  // Clicking outside the navigation closes all groups.
  documentHandlers.get("click")({ target: new FakeElement() });
  assert.equal(groupA.state.open, false);

  // Clicks inside a group must not close it.
  openGroup(triggerA);
  const insideTarget = new FakeElement();
  insideTarget.closest = () => ({ match: true });
  documentHandlers.get("click")({ target: insideTarget });
  assert.equal(groupA.state.open, true);

  console.log("navbar menu and Escape checks passed");
};

run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});