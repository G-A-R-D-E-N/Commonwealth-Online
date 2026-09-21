"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const source = fs.readFileSync(path.join(__dirname, "..", "static", "js", "account.js"), "utf8");

let signInSubmit;
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
  checkValidity() {
    return true;
  },
  reportValidity() {},
  ...overrides,
});

const status = element({ hidden: true });
const signInForm = element({
  addEventListener(type, handler) {
    if (type === "submit") {
      signInSubmit = handler;
    }
  },
});
const discordSignIn = element({ hidden: true, disabled: true });

const elements = new Map([
  ["[data-account-status]", status],
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

const client = {
  auth: {
    async getSession() {
      return { data: { session: null } };
    },
    async signInWithPassword() {
      return {
        data: {
          session: {
            user: {
              id: "user-1",
              email: "member@example.test",
            },
          },
        },
        error: null,
      };
    },
  },
};

let assignedUrl = "";
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
  await flush();

  assert.equal(typeof signInSubmit, "function");

  await signInSubmit({
    preventDefault() {},
  });

  assert.equal(assignedUrl, "https://commonwealth-online.com/profile/");
  assert.equal(status.textContent, "Signing in…");

  console.log("account sign-in redirect checks passed");
};

run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
