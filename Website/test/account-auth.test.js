"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const source = fs.readFileSync(path.join(__dirname, "..", "static", "js", "account.js"), "utf8");

let resolveSettings;
const settingsResponse = new Promise((resolve) => {
  resolveSettings = resolve;
});

const element = (overrides = {}) => ({
  hidden: false,
  disabled: false,
  textContent: "",
  value: "",
  src: "",
  dataset: {},
  classList: { toggle() {} },
  addEventListener() {},
  querySelector() {
    return null;
  },
  querySelectorAll() {
    return [];
  },
  ...overrides,
});

const status = element({ hidden: true });
const signedOut = element();
const signedIn = element({ hidden: true });
const registerSubmit = element();
const registerForm = element({
  querySelector(selector) {
    return selector === 'button[type="submit"]' ? registerSubmit : null;
  },
});
const signInForm = element();
const profileName = element();
const profileEmail = element();
const profileRole = element();
const profileAvatar = element();
const discordState = element({ textContent: "Checking linked identities…" });
const discordLink = element({ disabled: true });
const discordSignIn = element({ disabled: true });
const signOut = element();
const displayName = element();
const profileForm = element({
  elements: { display_name: displayName },
});

const elements = new Map([
  ["[data-account-status]", status],
  ["[data-account-signed-out]", signedOut],
  ["[data-account-signed-in]", signedIn],
  ["[data-register-form]", registerForm],
  ["[data-signin-form]", signInForm],
  ["[data-profile-form]", profileForm],
  ["[data-profile-avatar]", profileAvatar],
  ["[data-profile-name]", profileName],
  ["[data-profile-email]", profileEmail],
  ["[data-profile-role]", profileRole],
  ["[data-discord-state]", discordState],
  ["[data-discord-link]", discordLink],
  ["[data-discord-sign-in]", discordSignIn],
  ["[data-sign-out]", signOut],
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

const client = {
  auth: {
    async getSession() {
      return {
        data: {
          session: {
            user: { id: "user-1", email: "member@example.test" },
          },
        },
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
            display_name: "Member",
            avatar_url: "/assets/profile-icons/armorer.png",
            role: "member",
          },
          error: null,
        };
      },
    };
  },
};

let settingsRequest;
const context = {
  URL,
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
  fetch(url, options) {
    settingsRequest = { url, options };
    return settingsResponse;
  },
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

vm.runInNewContext(source, context, { filename: "account.js" });

const flush = () => new Promise((resolve) => setImmediate(resolve));

const run = async () => {
  await flush();
  assert.equal(settingsRequest.url, "https://project.example/auth/v1/settings");
  assert.equal(discordState.textContent, "Checking linked identities…");

  resolveSettings({
    ok: true,
    async json() {
      return {
        disable_signup: false,
        external: { email: true, discord: true },
      };
    },
  });

  await flush();
  await flush();

  assert.equal(discordState.textContent, "Not linked");
  assert.equal(discordLink.disabled, false);
  assert.equal(discordLink.hidden, false);
  assert.equal(discordSignIn.disabled, false);
  assert.equal(registerSubmit.disabled, false);

  console.log("account auth initialization checks passed");
};

run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
