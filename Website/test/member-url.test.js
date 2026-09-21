"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const source = fs.readFileSync(path.join(__dirname, "..", "static", "js", "member.js"), "utf8");

const element = () => ({
  hidden: false,
  textContent: "",
  src: "",
  disabled: false,
  children: [],
  classList: { toggle() {} },
  addEventListener() {},
  append(...items) { this.children.push(...items); },
  replaceChildren(...items) { this.children = [...items]; },
});

const nodes = new Map();
for (const selector of [
  "[data-member-status]","[data-member-profile]","[data-member-avatar]","[data-member-name]",
  "[data-member-presence]","[data-member-bio]","[data-member-faction]","[data-member-playstyle]",
  "[data-member-joined]","[data-member-joined-row]","[data-member-badges-card]",
  "[data-member-badges-list]","[data-member-username-history-card]",
  "[data-member-username-history-list]","[data-member-friends-card]",
  "[data-member-friends-list]","[data-member-actions-card]","[data-friend-action]",
  "[data-block-action]","[data-member-action-status]"
]) nodes.set(selector, element());

const root = element();
root.dataset = { supabaseUrl: "https://project.example", supabaseKey: "sb_publishable_test" };
root.querySelector = (selector) => nodes.get(selector) || null;

let resolvedUsername = "";
let canonicalUrl = "";
let restProfileLookup = "";
let restRpcCall = "";

const emptyQuery = {
  select() { return this; },
  eq(column, value) {
    if (column === "display_name") resolvedUsername = value;
    return this;
  },
  order() { return this; },
  limit: async () => ({ data: [], error: null }),
  maybeSingle: async () => ({ data: null, error: null }),
};

const client = {
  auth: {
    async getSession() { return { data: { session: null } }; },
  },
  from(table) {
    if (table === "profiles") {
      return {
        select() { return this; },
        eq(column, value) {
          if (column === "display_name") resolvedUsername = value;
          return this;
        },
        async maybeSingle() {
          return { data: { id: "de9d5d6f-52f5-4f93-a03f-1894abba8839", display_name: "Nomad" }, error: null };
        },
      };
    }
    return Object.create(emptyQuery);
  },
  async rpc(name) {
    if (name === "get_public_member_profile") {
      return {
        data: {
          display_name: "Nomad",
          avatar_url: "/assets/profile-icons/armorer.png",
          bio: "",
          faction_id: null,
          faction_name: null,
          faction_tag: null,
          playstyle: "",
          joined_at: null,
          presence_status: null,
          current_server: null,
          show_friends: false,
        },
        error: null,
      };
    }
    return { data: [], error: null };
  },
};

const document = {
  querySelector(selector) {
    if (selector === "[data-member]") return root;
    if (selector === ".site-brand__logo") {
      return { src: "https://commonwealth-online.com/assets/branding/logo.svg" };
    }
    return null;
  },
  createElement() { return element(); },
};

vm.runInNewContext(source, {
  URL,
  URLSearchParams,
  encodeURIComponent,
  document,
  fetch: async (input, options = {}) => {
    const requestUrl = String(input);
    if (requestUrl.includes("/rest/v1/profiles")) {
      restProfileLookup = requestUrl;
      return {
        ok: true,
        async json() {
          return [{ id: "de9d5d6f-52f5-4f93-a03f-1894abba8839", display_name: "Nomad" }];
        },
      };
    }
    if (requestUrl.includes("/rest/v1/rpc/get_public_member_profile")) {
      restRpcCall = requestUrl;
      return {
        ok: true,
        async json() {
          return [{
            display_name: "Nomad",
            avatar_url: "/assets/profile-icons/armorer.png",
            bio: "",
            faction_id: null,
            faction_name: null,
            faction_tag: null,
            playstyle: "",
            joined_at: null,
            presence_status: null,
            current_server: null,
            show_friends: false,
          }];
        },
      };
    }
    throw new Error("Unexpected fetch");
  },
  window: {
    location: { search: "?username=Nomad" },
    history: {
      replaceState(_state, _title, url) { canonicalUrl = url; },
    },
  },
});

setImmediate(() => {
  try {
    assert.match(restProfileLookup, /display_name=eq\.Nomad/);
    assert.match(restRpcCall, /get_public_member_profile/);
    assert.equal(canonicalUrl, "/member/?username=Nomad");
    assert.doesNotMatch(canonicalUrl, /\?id=/);
    assert.equal(nodes.get("[data-member-name]").textContent, "Nomad");
    console.log("member public URL checks passed");
  } catch (error) {
    console.error(error);
    process.exitCode = 1;
  }
});
