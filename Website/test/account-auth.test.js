"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const source = fs.readFileSync(path.join(__dirname, "..", "static", "js", "account.js"), "utf8");

let registerHandler;
let discordHandler;
let signUpCalls = 0;
let oauthArgs;
let resolveSettings;
const settingsResponse = new Promise((resolve) => {
  resolveSettings = resolve;
});

const element = (overrides = {}) => ({
  hidden: false,
  disabled: false,
  textContent: "",
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
const registerSubmit = element();
const registerForm = element({
  querySelector(selector) {
    return selector === 'button[type="submit"]' ? registerSubmit : null;
  },
  addEventListener(type, handler) {
    if (type === "submit") {
      registerHandler = handler;
    }
  },
  checkValidity() {
    return true;
  },
  reportValidity() {},
  reset() {},
});
const signInForm = element();
const discordSignIn = element({
  hidden: false,
  disabled: true,
  addEventListener(type, handler) {
    if (type === "click") {
      discordHandler = handler;
    }
  },
});

const elements = new Map([
  ["[data-account-status]", status],
  ["[data-register-form]", registerForm],
  ["[data-signin-form]", signInForm],
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

let assignedUrl = "";
const client = {
  auth: {
    async getSession() {
      return { data: { session: null } };
    },
    async signUp() {
      signUpCalls += 1;
      return { data: { session: null }, error: null };
    },
    async signInWithOAuth(args) {
      oauthArgs = args;
      return {
        data: { url: "https://discord.com/oauth2/authorize?client_id=test" },
        error: null,
      };
    },
  },
};

const context = {
  URL,
  FormData: class {
    get(name) {
      if (name === "username") {
        return "   ";
      }
      if (name === "email") {
        return "member@example.test";
      }
      return "password123";
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
  fetch() {
    return settingsResponse;
  },
  window: {
    location: {
      origin: "https://commonwealth-online.com",
      assign(url) {
        assignedUrl = url;
      },
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

  assert.equal(registerSubmit.disabled, false);
  assert.equal(discordSignIn.disabled, true);
  assert.equal(assignedUrl, "");

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

  assert.equal(registerSubmit.disabled, false);
  assert.equal(discordSignIn.disabled, false);
  assert.equal(discordSignIn.hidden, false);
  assert.equal(assignedUrl, "");
  assert.equal(typeof registerHandler, "function");

  await registerHandler({ preventDefault() {} });

  assert.equal(signUpCalls, 0);
  assert.equal(status.textContent, "Enter a username.");
  assert.equal(status.hidden, false);
  assert.equal(typeof discordHandler, "function");

  await discordHandler();

  assert.equal(oauthArgs.provider, "discord");
  assert.equal(oauthArgs.options.redirectTo, "https://commonwealth-online.com/account/");
  assert.equal(oauthArgs.options.skipBrowserRedirect, true);
  assert.equal(assignedUrl, "https://discord.com/oauth2/authorize?client_id=test");

  console.log("account auth initialization checks passed");
};

run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
