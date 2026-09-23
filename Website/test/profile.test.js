"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const source = fs.readFileSync(path.join(__dirname, "..", "static", "js", "profile.js"), "utf8");

const makeElement = (overrides = {}) => {
  const listeners = new Map();
  const classes = new Set();
  const attrs = new Map();
  const element = {
    hidden: false,
    disabled: false,
    textContent: "",
    value: "",
    src: "",
    href: "",
    tabIndex: 0,
    title: "",
    dataset: {},
    elements: {},
    children: [],
    classList: {
      add(name) { classes.add(name); },
      remove(name) { classes.delete(name); },
      toggle(name, force) {
        const on = force === undefined ? !classes.has(name) : force;
        if (on) classes.add(name); else classes.delete(name);
        return on;
      },
      contains(name) { return classes.has(name); },
    },
    _classes: classes,
    _listeners: listeners,
    setAttribute(name, value) { attrs.set(name, String(value)); },
    getAttribute(name) { return attrs.has(name) ? attrs.get(name) : null; },
    removeAttribute(name) { attrs.delete(name); },
    addEventListener(type, handler) { listeners.set(type, handler); },
    removeEventListener() {},
    querySelector() { return null; },
    querySelectorAll() { return []; },
    checkValidity() { return true; },
    reportValidity() {},
    reset() {},
    focus() {},
    showModal() {},
    close() {},
    append(...items) { this.children.push(...items); },
    replaceChildren(...items) { this.children = [...items]; },
  };
  Object.assign(element, overrides);
  return element;
};

let profileSubmit;
let emailSubmit;
let passwordSubmit;
let signOutClick;
let openAvatar;
let confirmAvatar;
const authUpdates = [];
const profileUpdates = [];
const callOrder = [];

const status = makeElement({ hidden: true });
const profileAvatar = makeElement();
const avatarCurrent = makeElement();
const avatarCurrentName = makeElement();
const profileName = makeElement();
const profileEmail = makeElement();
const signOut = makeElement();
const viewPublicProfile = makeElement();
const username = makeElement();
const avatarHidden = makeElement({ value: "/assets/profile-icons/armorer.png" });
const emailField = makeElement();
const currentPassword = makeElement();
const newPassword = makeElement();
const confirmPassword = makeElement();

const profileForm = makeElement({
  elements: { username, avatar_url: avatarHidden },
  addEventListener(type, handler) {
    if (type === "submit") profileSubmit = handler;
  },
});

const emailForm = makeElement({
  elements: { email: emailField },
  addEventListener(type, handler) {
    if (type === "submit") emailSubmit = handler;
  },
});

const passwordForm = makeElement({
  elements: { current_password: currentPassword, new_password: newPassword, confirm_password: confirmPassword },
  addEventListener(type, handler) {
    if (type === "submit") passwordSubmit = handler;
  },
});

const avatarModal = makeElement({ _opened: false });
avatarModal.showModal = () => { avatarModal._opened = true; };
avatarModal.close = () => { avatarModal._opened = false; };

const armorerOption = makeElement({ dataset: { avatarValue: "/assets/profile-icons/armorer.png", avatarCategory: "perks" } });
const riflemanOption = makeElement({ dataset: { avatarValue: "/assets/profile-icons/rifleman.png", avatarCategory: "perks" } });

const allFilter = makeElement({ dataset: { avatarFilter: "all" } });
const openButton = makeElement({
  addEventListener(type, handler) {
    if (type === "click") openAvatar = handler;
  },
});
const confirmButton = makeElement({
  addEventListener(type, handler) {
    if (type === "click") confirmAvatar = handler;
  },
});

const tabs = ["profile", "privacy", "social", "multiplayer", "faction", "notifications", "security"].map((name) =>
  makeElement({ dataset: { profileTab: name } })
);
const panels = ["profile", "privacy", "social", "multiplayer", "faction", "notifications", "security"].map((name) =>
  makeElement({ dataset: { profilePanel: name } })
);

const nodes = new Map([
  ["[data-profile-status]", status],
  ["[data-profile-form]", profileForm],
  ["[data-email-form]", emailForm],
  ["[data-password-form]", passwordForm],
  ["[data-profile-avatar]", profileAvatar],
  ["[data-profile-avatar-current]", avatarCurrent],
  ["[data-profile-avatar-current-name]", avatarCurrentName],
  ["[data-profile-name]", profileName],
  ["[data-profile-email]", profileEmail],
  ["[data-sign-out]", signOut],
  ["[data-view-public-profile]", viewPublicProfile],
  ["[data-avatar-modal]", avatarModal],
  ["[data-avatar-picker-open]", openButton],
  ["[data-avatar-modal-confirm]", confirmButton],
]);

const root = makeElement({
  dataset: {
    supabaseUrl: "https://project.example",
    supabaseKey: "sb_publishable_test",
  },
  querySelector(selector) {
    return nodes.get(selector) || null;
  },
  querySelectorAll(selector) {
    if (selector === "[data-profile-tab]") return tabs;
    if (selector === "[data-profile-panel]") return panels;
    if (selector === "[data-avatar-value]") return [armorerOption, riflemanOption];
    if (selector === "[data-avatar-filter]") return [allFilter];
    return [];
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
    async signOut() {
      return { error: null };
    },
    onAuthStateChange(handler) {
      return { data: { subscription: { unsubscribe() {} } } };
    },
  },
  from(table) {
    assert.equal(table, "profiles");
    return {
      select() { return this; },
      eq() { return this; },
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
      return { external: { discord: true } };
    },
  }),
  window: {
    location: {
      origin: "https://commonwealth-online.com",
      hash: "",
      assign(url) {
        assignedUrl = url;
      },
    },
    history: {
      replaceState() {},
    },
    addEventListener() {},
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

  // Initial render populates identity and forms.
  assert.equal(profileName.textContent, "Resident");
  assert.equal(profileEmail.textContent, "member@example.test");
  assert.equal(username.value, "Resident");
  assert.equal(emailField.value, "member@example.test");
  assert.equal(avatarHidden.value, "/assets/profile-icons/armorer.png");
  assert.equal(profileAvatar.src, "/assets/profile-icons/armorer.png");
  assert.equal(avatarCurrent.src, "/assets/profile-icons/armorer.png");
  assert.equal(avatarCurrentName.textContent, "Armorer");
  assert.equal(viewPublicProfile.href, "/member/?username=Resident");

  // Saving the profile updates the profiles table and account metadata.
  username.value = "Nomad";
  avatarHidden.value = "/assets/profile-icons/rifleman.png";
  await profileSubmit({ preventDefault() {} });

  assert.equal(callOrder[0], "profile");
  assert.equal(callOrder[1], "auth");
  assert.equal(profileUpdates[0].display_name, "Nomad");
  assert.equal(profileUpdates[0].avatar_url, "/assets/profile-icons/rifleman.png");
  assert.equal(authUpdates[0].payload.data.display_name, "Nomad");
  assert.equal(authUpdates[0].payload.data.avatar_url, "/assets/profile-icons/rifleman.png");
  assert.equal(authUpdates[0].options, undefined);
  assert.equal(profileName.textContent, "Nomad");
  assert.equal(avatarCurrent.src, "/assets/profile-icons/rifleman.png");
  assert.equal(avatarCurrentName.textContent, "Rifleman");
  assert.equal(status.textContent, "Profile saved.");

  // Rejecting an unknown avatar.
  avatarHidden.value = "/assets/profile-icons/not-real.png";
  await profileSubmit({ preventDefault() {} });
  assert.equal(profileUpdates.length, 1, "invalid avatar must not update profiles");
  assert.equal(status.textContent, "Choose one of the available profile pictures.");

  // Changing the email flows through the email form with a redirect target.
  emailField.value = "nomad@example.test";
  await emailSubmit({ preventDefault() {} });
  assert.equal(authUpdates[1].payload.email, "nomad@example.test");
  assert.equal(authUpdates[1].options.emailRedirectTo, "https://commonwealth-online.com/account/");

  // Updating the password preserves the optional current password.
  currentPassword.value = "old-password";
  newPassword.value = "new-password";
  confirmPassword.value = "new-password";
  await passwordSubmit({ preventDefault() {} });
  assert.equal(authUpdates[2].payload.password, "new-password");
  assert.equal(authUpdates[2].payload.currentPassword, "old-password");
  assert.equal(authUpdates[2].options, undefined);

  // The avatar picker modal selects a picture and commits it.
  avatarHidden.value = "/assets/profile-icons/armorer.png";
  openAvatar();
  assert.equal(avatarModal._opened, true);
  riflemanOption._listeners.get("click")();
  confirmAvatar();
  assert.equal(avatarModal._opened, false);
  assert.equal(avatarHidden.value, "/assets/profile-icons/rifleman.png");
  assert.equal(avatarCurrent.src, "/assets/profile-icons/rifleman.png");

  console.log("profile settings checks passed");
};

run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
