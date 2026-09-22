"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const source = fs.readFileSync(path.join(__dirname, "..", "static", "js", "profile.js"), "utf8");

let profileSubmit;
let passwordSubmit;
let discordClick;
let authStateHandler;
let linkArgs;
const authUpdates = [];
const profileUpdates = [];
const callOrder = [];

const element = (overrides = {}) => ({
  hidden: false,
  disabled: false,
  textContent: "",
  value: "",
  src: "",
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
  reset() {},
  ...overrides,
});

const status = element({ hidden: true });
const profileAvatar = element();
const profileName = element();
const profileEmail = element();
const discordState = element();
const discordLink = element({
  disabled: true,
  addEventListener(type, handler) {
    if (type === "click") {
      discordClick = handler;
    }
  },
});
const signOut = element();
const username = element();
const email = element();
const currentPassword = element();
const newPassword = element();
const confirmPassword = element();

const avatarRadios = [
  "/assets/profile-icons/armorer.png",
  "/assets/profile-icons/hacker.png",
  "/assets/profile-icons/rifleman.png",
  "/assets/profile-icons/medic.png",
  "/assets/profile-icons/scrapper.png",
  "/assets/profile-icons/cap_collector.png",
  "/assets/profile-images/Icon__Brotherhood.png",
  "/assets/profile-images/Icon__Institute.png",
  "/assets/profile-images/Icon__Minutemen.png",
  "/assets/profile-images/Icon__Railroad.png",
  "/assets/profile-images/CO.png",
  "/assets/profile-images/Cool.png",
  "/assets/profile-images/Love.png",
  "/assets/profile-images/Rage.png",
  "/assets/profile-images/Wink.png",
].map((value) => ({ value, checked: false }));
avatarRadios[0].checked = true;

const profileForm = element({
  elements: { username, email },
  addEventListener(type, handler) {
    if (type === "submit") {
      profileSubmit = handler;
    }
  },
  querySelector(selector) {
    if (selector === 'input[name="avatar_url"]:checked') {
      return avatarRadios.find((radio) => radio.checked) || null;
    }
    return null;
  },
  querySelectorAll(selector) {
    return selector === 'input[name="avatar_url"]' ? avatarRadios : [];
  },
});

let passwordReset = false;
const passwordForm = element({
  elements: {
    current_password: currentPassword,
    new_password: newPassword,
    confirm_password: confirmPassword,
  },
  addEventListener(type, handler) {
    if (type === "submit") {
      passwordSubmit = handler;
    }
  },
  reset() {
    passwordReset = true;
  },
});

const elements = new Map([
  ["[data-profile-status]", status],
  ["[data-profile-form]", profileForm],
  ["[data-password-form]", passwordForm],
  ["[data-profile-avatar]", profileAvatar],
  ["[data-profile-name]", profileName],
  ["[data-profile-email]", profileEmail],
  ["[data-discord-state]", discordState],
  ["[data-discord-link]", discordLink],
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

const user = {
  id: "user-1",
  email: "member@example.test",
  user_metadata: {
    display_name: "Resident",
    avatar_url: "/assets/profile-icons/armorer.png",
  },
};

const client = {
  auth: {
    async getSession() {
      return { data: { session: { user } } };
    },
    async getUserIdentities() {
      return { data: { identities: [] } };
    },
    async updateUser(payload, options) {
      authUpdates.push({ payload, options });
      callOrder.push("auth");
      return {
        data: {
          user: {
            ...user,
            email: payload.email || user.email,
            user_metadata: {
              ...user.user_metadata,
              ...(payload.data || {}),
            },
          },
        },
        error: null,
      };
    },
    async linkIdentity(args) {
      linkArgs = args;
      return {
        data: { url: "https://discord.com/oauth2/authorize?client_id=link-test" },
        error: null,
      };
    },
    async signOut() {
      return { error: null };
    },
    onAuthStateChange(handler) {
      authStateHandler = handler;
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
          },
          error: null,
        };
      },
      update(payload) {
        profileUpdates.push(payload);
        callOrder.push("profile");
        return {
          async eq() {
            return { error: null };
          },
        };
      },
    };
  },
};

let assignedUrl = "";
const context = {
  URL,
  document: {
    querySelector(selector) {
      if (selector === "[data-profile]") {
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
        external: { discord: true },
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
  vm.runInNewContext(source, context, { filename: "profile.js" });
  await flush();
  await flush();
  await flush();

  assert.equal(profileName.textContent, "Resident");
  assert.equal(profileEmail.textContent, "member@example.test");
  assert.equal(username.value, "Resident");
  assert.equal(email.value, "member@example.test");
  assert.equal(profileAvatar.src, "/assets/profile-icons/armorer.png");
  assert.equal(typeof authStateHandler, "function");

  username.value = "   ";
  await profileSubmit({ preventDefault() {} });
  assert.equal(authUpdates.length, 0);
  assert.equal(profileUpdates.length, 0);
  assert.equal(status.textContent, "Enter a username.");

  username.value = "Nomad";
  email.value = "nomad@example.test";
  avatarRadios.forEach((radio) => {
    radio.checked = radio.value === "/assets/profile-icons/rifleman.png";
  });

  await profileSubmit({ preventDefault() {} });

  assert.equal(callOrder[0], "profile");
  assert.equal(callOrder[1], "auth");
  assert.equal(authUpdates[0].payload.email, "nomad@example.test");
  assert.equal(authUpdates[0].payload.data.display_name, "Nomad");
  assert.equal(authUpdates[0].payload.data.avatar_url, "/assets/profile-icons/rifleman.png");
  assert.equal(authUpdates[0].options.emailRedirectTo, "https://commonwealth-online.com/account/");
  assert.equal(profileUpdates[0].display_name, "Nomad");
  assert.equal(profileUpdates[0].avatar_url, "/assets/profile-icons/rifleman.png");
  assert.equal(profileName.textContent, "Nomad");
  assert.match(status.textContent, /Check your new email address/);

  username.value = "Sentinel";
  email.value = "nomad@example.test";
  avatarRadios.forEach((radio) => {
    radio.checked = radio.value === "/assets/profile-images/Icon__Brotherhood.png";
  });

  await profileSubmit({ preventDefault() {} });

  assert.equal(profileUpdates[1].display_name, "Sentinel");
  assert.equal(profileUpdates[1].avatar_url, "/assets/profile-images/Icon__Brotherhood.png");
  assert.equal(authUpdates[1].payload.data.avatar_url, "/assets/profile-images/Icon__Brotherhood.png");
  assert.equal(authUpdates[1].options, undefined);
  assert.equal(profileAvatar.src, "/assets/profile-images/Icon__Brotherhood.png");
  assert.equal(profileName.textContent, "Sentinel");
  assert.equal(status.textContent, "Profile saved.");

  currentPassword.value = "old-password";
  newPassword.value = "new-password";
  confirmPassword.value = "new-password";

  await passwordSubmit({ preventDefault() {} });

  assert.equal(authUpdates[2].payload.password, "new-password");
  assert.equal(authUpdates[2].payload.currentPassword, "old-password");
  assert.equal(authUpdates[2].options, undefined);
  assert.equal(passwordReset, true);
  assert.equal(status.textContent, "Password updated.");

  console.log("profile settings checks passed");
};

run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
