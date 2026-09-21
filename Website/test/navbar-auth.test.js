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
const accountAttributes = new Map();
const badgeAttributes = new Map();
const accountLink = element({
  setAttribute(name, value) {
    accountAttributes.set(name, value);
  },
  removeAttribute(name) {
    accountAttributes.delete(name);
  },
});
const accountIcon = element({ hidden: false });
const accountAvatar = element({ hidden: true });
const notificationBadge = element({
  hidden: true,
  setAttribute(name, value) {
    badgeAttributes.set(name, value);
  },
});

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
      ["[data-account-nav-icon]", accountIcon],
      ["[data-account-nav-avatar]", accountAvatar],
      ["[data-account-nav-notifications]", notificationBadge],
    ]).get(selector) || null;
  },
  querySelectorAll(selector) {
    if (selector === "[data-nav-group]") {
      return [];
    }
    return [];
  },
});

let authHandler;
let notificationQueries = 0;
const documentHandlers = new Map();
const signedInSession = {
  user: {
    id: "user-1",
    user_metadata: {
      display_name: "Nomad",
      avatar_url: "/assets/profile-icons/rifleman.png",
    },
  },
};

const client = {
  auth: {
    async getSession() {
      return {
        data: {
          session: signedInSession,
        },
      };
    },
    onAuthStateChange(callback) {
      authHandler = callback;
      return { data: { subscription: { unsubscribe() {} } } };
    },
  },
  from(table) {
    assert.equal(table, "user_notifications");
    notificationQueries += 1;
    return {
      select(columns, options) {
        assert.equal(columns, "id");
        assert.equal(options.count, "exact");
        assert.equal(options.head, true);
        return {
          async is(column, value) {
            assert.equal(column, "read_at");
            assert.equal(value, null);
            return { count: 3, error: null };
          },
        };
      },
    };
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
    addEventListener(name, callback) {
      documentHandlers.set(name, callback);
    },
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
  assert.equal(accountIcon.hidden, true);
  assert.equal(accountAvatar.hidden, false);
  assert.equal(accountAvatar.src, "/assets/profile-icons/rifleman.png");
  assert.equal(accountAttributes.get("aria-label"), "Nomad profile");
  assert.equal(accountLink.title, "Profile");
  assert.equal(classes.has("site-nav__profile"), true);
  assert.equal(notificationQueries, 1);
  assert.equal(notificationBadge.hidden, false);
  assert.equal(notificationBadge.textContent, "3");
  assert.equal(badgeAttributes.get("aria-label"), "3 unread notifications");

  authHandler("TOKEN_REFRESHED", signedInSession);
  await flush();
  assert.equal(notificationQueries, 1, "auth refresh must not repeat notification query");

  documentHandlers.get("co:notifications-cleared")();
  assert.equal(notificationBadge.hidden, true);
  assert.equal(notificationBadge.textContent, "");

  authHandler("SIGNED_OUT", null);
  await flush();
  assert.equal(accountIcon.hidden, false);
  assert.equal(accountAvatar.hidden, true);
  assert.equal(accountAttributes.get("aria-label"), "Login or sign up");
  assert.equal(notificationBadge.hidden, true);
  assert.equal(notificationBadge.textContent, "");

  console.log("navbar profile and notification checks passed");
};

run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
