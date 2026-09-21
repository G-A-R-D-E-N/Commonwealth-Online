"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const source = fs.readFileSync(path.join(__dirname, "..", "static", "js", "account.js"), "utf8");

let registerHandler;
let usernameValue = "   ";
let fetchCalls = [];
let now = 1_000_000;

const element = (overrides = {}) => ({
  hidden: false,
  disabled: false,
  textContent: "",
  classList: { toggle() {} },
  addEventListener() {},
  querySelector() { return null; },
  querySelectorAll() { return []; },
  ...overrides,
});

const status = element({ hidden: true });
const registerSubmit = element();
const registerForm = element({
  querySelector(selector) {
    return selector === 'button[type="submit"]' ? registerSubmit : null;
  },
  addEventListener(type, handler) {
    if (type === "submit") registerHandler = handler;
  },
  checkValidity() { return true; },
  reportValidity() {},
  reset() {},
});
const signInForm = element();
const root = element({
  dataset: {
    supabaseUrl: "https://project.example",
    supabaseKey: "sb_publishable_test",
    captchaProvider: "",
    captchaSiteKey: "",
  },
  querySelector(selector) {
    return new Map([
      ["[data-account-status]", status],
      ["[data-register-form]", registerForm],
      ["[data-signin-form]", signInForm],
    ]).get(selector) || null;
  },
});

const client = {
  auth: {
    async getSession() {
      return { data: { session: null } };
    },
    async setSession() {
      return { error: null };
    },
  },
};

const storage = new Map();
const context = {
  URL,
  Date: class extends Date {
    static now() { return now; }
  },
  FormData: class {
    get(name) {
      if (name === "username") return usernameValue;
      if (name === "email") return "member@example.test";
      if (name === "website") return "";
      return "password123";
    }
  },
  document: {
    querySelector(selector) {
      if (selector === "[data-account]") return root;
      if (selector === ".site-brand__logo") {
        return { src: "https://commonwealth-online.com/assets/branding/logo.svg" };
      }
      return null;
    },
  },
  fetch: async (url, options = {}) => {
    fetchCalls.push({ url, options });
    if (String(url).endsWith("/auth/v1/settings")) {
      return {
        ok: true,
        async json() {
          return { disable_signup: false, external: { email: true, discord: false } };
        },
      };
    }
    return {
      ok: true,
      status: 201,
      async json() {
        return { account: { requiresConfirmation: true }, session: null };
      },
    };
  },
  window: {
    location: {
      origin: "https://commonwealth-online.com",
      assign() {},
    },
    localStorage: {
      getItem(key) { return storage.get(key) || null; },
      setItem(key, value) { storage.set(key, value); },
    },
    supabase: {
      createClient() { return client; },
    },
  },
};

const flush = () => new Promise((resolve) => setImmediate(resolve));

const run = async () => {
  vm.runInNewContext(source, context, { filename: "account.js" });
  await flush();
  await flush();

  assert.equal(typeof registerHandler, "function");

  await registerHandler({ preventDefault() {} });
  assert.equal(status.textContent, "Enter a username.");
  assert.equal(fetchCalls.filter((call) => String(call.url).includes("/register-account")).length, 0);

  usernameValue = "Nomad";
  await registerHandler({ preventDefault() {} });

  const registrationCalls = fetchCalls.filter((call) => String(call.url).includes("/functions/v1/register-account"));
  assert.equal(registrationCalls.length, 1);
  const body = JSON.parse(registrationCalls[0].options.body);
  assert.equal(body.username, "Nomad");
  assert.equal(body.email, "member@example.test");
  assert.equal(body.redirectTo, "https://commonwealth-online.com/account/");
  assert.equal(registerSubmit.disabled, true);
  assert.equal(status.textContent, "Account created. Check your email to confirm your address, then sign in.");

  console.log("account auth initialization checks passed");
};

run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
