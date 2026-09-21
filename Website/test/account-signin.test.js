"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const source = fs.readFileSync(path.join(__dirname, "..", "static", "js", "account.js"), "utf8");

const listeners = {};
const element = (overrides = {}) => ({
  hidden: false,
  disabled: false,
  textContent: "",
  value: "",
  src: "",
  classList: { toggle() {} },
  addEventListener(type, handler) {
    listeners[type] = handler;
  },
  querySelector() {
    return null;
  },
  querySelectorAll() {
    return [];
  },
  checkValidity() {
    return true;
  },
  reportValidity() {},
  ...overrides,
});

const status = element({ hidden: true });
const signedOut = element();
const signedIn = element({ hidden: true });
const signInForm = element();
const profileName = element();
const profileEmail = element();
const profileRole = element();
const profileAvatar = element();
const discordState = element();
const discordLink = element();
const discordSignIn = element();
const displayName = element();
const profileForm = element({
  elements: { display_name: displayName },
});

const elements = new Map([
  ["[data-account-status]", status],
  ["[data-account-signed-out]", signedOut],
  ["[data-account-signed-in]", signedIn],
  ["[data-signin-form]", signInForm],
  ["[data-profile-form]", profileForm],
  ["[data-profile-avatar]", profileAvatar],
  ["[data-profile-name]", profileName],
  ["[data-profile-email]", profileEmail],
  ["[data-profile-role]", profileRole],
  ["[data-discord-state]", discordState],
  ["[data-discord-link]", discordLink],
  ["[data-discord-sign-in]", discordSignIn],
]);

const root = element({
  dataset: {
    supabaseUrl: "https://project.example",
    supabaseKey: "sb_publishable_test",
  },
  querySelector(selector) {
    return elements.get(selector) || null;
  },
});

const signedInUser = {
  id: "user-1",
  email: "member@example.test",
};

const client = {
  auth: {
    async getSession() {
      return { data: { session: null } };
    },
    async signInWithPassword() {
      return {
        data: {
          session: {
            user: signedInUser,
          },
        },
        error: null,
      };
    },
    async getUserIdentities() {
      return { data: { identities: [] } };
    },
    onAuthStateChange() {
      return { data: { subscription: { unsubscribe() {} } } };
    },
  },
  from() {
    return {
      select() {
        return this;
      },
      eq() {
        return this;
      },
      async single() {
        return {
          data: {
            display_name: "Resident",
            avatar_url: "/assets/profile-icons/armorer.png",
            role: "member",
          },
          error: null,
        };
      },
    };
  },
};

const context = {
  URL,
  FormData: class {
    get(name) {
      return name === "email" ? "member@example.test" : "password123";
    }
  },
  document: {
    querySelector(selector) {
      if (selector === "[data-account]") {
        return root;
      }
      if (selector === ".site-brand__logo") {
        return { src: "https://commonwealth-online.com/assets/branding/logo.svg" };
      }
      return null;
    },
  },
  fetch: async () => ({
    ok: true,
    async json() {
      return {
        disable_signup: false,
        external: { email: true, discord: false },
      };
    },
  }),
  window: {
    location: { origin: "https://commonwealth-online.com" },
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
  vm.runInNewContext(source, context, { filename: "account.js" });
  await flush();
  await flush();

  assert.equal(typeof listeners.submit, "function");

  await listeners.submit({
    preventDefault() {},
  });

  assert.equal(signedIn.hidden, false);
  assert.equal(signedOut.hidden, true);
  assert.equal(profileName.textContent, "Resident");
  assert.equal(profileEmail.textContent, "member@example.test");
  assert.equal(status.hidden, true);
  assert.equal(status.textContent, "");

  console.log("account sign-in handoff checks passed");
};

run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
