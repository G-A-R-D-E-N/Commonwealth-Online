"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const source = fs.readFileSync(path.join(__dirname, "..", "static", "js", "navbar.js"), "utf8");

const classes = new Set();
const attributes = new Map();
const element = (overrides = {}) => ({
  hidden: false,
  textContent: "",
  src: "",
  href: "",
  title: "",
  classList: {
    add(name) {
      classes.add(name);
    },
    remove(name) {
      classes.delete(name);
    },
    toggle() {},
  },
  setAttribute(name, value) {
    attributes.set(name, value);
  },
  removeAttribute(name) {
    attributes.delete(name);
  },
  getAttribute() {
    return "false";
  },
  addEventListener() {},
  ...overrides,
});

const toggle = element();
const nav = element();
const accountLink = element();
const accountLabel = element({ textContent: "Login / Sign Up" });
const accountAvatar = element({ hidden: true });

const mount = element({
  dataset: {
    supabaseUrl: "https://project.example",
    supabaseKey: "sb_publishable_test",
  },
  querySelector(selector) {
    return new Map([
      [".site-nav-toggle", toggle],
      [".site-nav", nav],
      ["[data-account-nav]", accountLink],
      ["[data-account-nav-label]", accountLabel],
      ["[data-account-nav-avatar]", accountAvatar],
    ]).get(selector) || null;
  },
});

const client = {
  auth: {
    async getSession() {
      return {
        data: {
          session: {
            user: {
              id: "user-1",
              user_metadata: {
                display_name: "Nomad",
                avatar_url: "/assets/profile-icons/rifleman.png",
              },
            },
          },
        },
      };
    },
    onAuthStateChange() {
      return { data: { subscription: { unsubscribe() {} } } };
    },
  },
  from() {
    throw new Error("navbar must not query profile storage");
  },
};

const context = {
  URL,
  HTMLElement: class {},
  document: {
    querySelector(selector) {
      if (selector === "[data-co-navbar]") {
        return mount;
      }
      if (selector === ".site-brand__logo") {
        return { src: "https://commonwealth-online.com/assets/branding/logo.svg" };
      }
      return null;
    },
    addEventListener() {},
  },
  window: {
    matchMedia() {
      return {
        matches: true,
        addEventListener() {},
      };
    },
    setTimeout(callback) {
      callback();
    },
    supabase: {
      createClient() {
        return client;
      },
    },
  },
};

const flush = () => new Promise((resolve) => setImmediate(resolve));

const run = async () => {
  vm.runInNewContext(source, context, { filename: "navbar.js" });
  await flush();
  await flush();

  assert.equal(accountLink.href, "/profile/");
  assert.equal(accountLabel.hidden, true);
  assert.equal(accountAvatar.hidden, false);
  assert.equal(accountAvatar.src, "/assets/profile-icons/rifleman.png");
  assert.equal(attributes.get("aria-label"), "Nomad profile");
  assert.equal(accountLink.title, "Profile");
  assert.equal(classes.has("site-nav__profile"), true);

  console.log("navbar profile icon checks passed");
};

run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
