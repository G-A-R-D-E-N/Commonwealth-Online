"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const source = fs.readFileSync(path.join(__dirname, "..", "static", "js", "faction-manage.js"), "utf8");
const factionId = "11111111-2222-3333-4444-555555555555";
const userId = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";

const element = () => ({
  hidden: false,
  textContent: "",
  href: "",
  src: "",
  disabled: false,
  children: [],
  classList: { toggle() {} },
  addEventListener() {},
  append(...items) { this.children.push(...items); },
  replaceChildren(...items) { this.children = [...items]; },
  querySelector() { return null; },
});

const runCase = async (search) => {
  const nodes = new Map();
  for (const selector of [
    "[data-faction-manage-status]",
    "[data-faction-manage-content]",
    "[data-faction-request-list]",
    "[data-faction-manage-members]",
    "[data-faction-invite-search]",
    "[data-faction-invite-results]",
    "[data-faction-manage-name]",
    "[data-faction-manage-back]",
  ]) nodes.set(selector, element());

  const inviteForm = nodes.get("[data-faction-invite-search]");
  inviteForm.checkValidity = () => true;
  inviteForm.elements = { query: { value: "" } };

  const root = element();
  root.dataset = { supabaseUrl: "https://project.example", supabaseKey: "sb_publishable_test" };
  root.querySelector = (selector) => nodes.get(selector) || null;

  let lookup = null;
  let canonicalUrl = "";

  const client = {
    auth: {
      async getSession() {
        return { data: { session: { user: { id: userId } } } };
      },
    },
    from(table) {
      if (table === "factions") {
        return {
          select() { return this; },
          eq(column, value) {
            lookup = { column, value };
            return this;
          },
          async single() {
            return {
              data: {
                id: factionId,
                slug: "commonwealth-rangers",
                name: "Commonwealth Rangers",
                tag: "CR",
              },
              error: null,
            };
          },
        };
      }

      if (table === "faction_members") {
        return {
          select() { return this; },
          eq() { return this; },
          order: async () => ({ data: [], error: null }),
          maybeSingle: async () => ({ data: { role_id: "leader", status: "active" }, error: null }),
        };
      }

      if (table === "faction_roles") {
        return {
          select() { return this; },
          eq() { return this; },
          maybeSingle: async () => ({ data: { can_manage_members: true }, error: null }),
        };
      }

      if (table === "profiles") {
        return {
          select() { return this; },
          eq() { return this; },
          ilike() { return this; },
          limit: async () => ({ data: [], error: null }),
        };
      }

      throw new Error("Unexpected table: " + table);
    },
  };

  const document = {
    querySelector(selector) {
      if (selector === "[data-faction-manage]") return root;
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
    Map,
    Promise,
    encodeURIComponent,
    document,
    window: {
      location: { search },
      history: {
        replaceState(_state, _title, url) { canonicalUrl = url; },
      },
      supabase: {
        createClient() { return client; },
      },
    },
  });

  await new Promise((resolve) => setImmediate(resolve));

  return {
    lookup,
    canonicalUrl,
    backHref: nodes.get("[data-faction-manage-back]").href,
    title: nodes.get("[data-faction-manage-name]").textContent,
  };
};

(async () => {
  const slug = await runCase("?slug=commonwealth-rangers");
  assert.deepEqual(slug.lookup, { column: "slug", value: "commonwealth-rangers" });
  assert.equal(slug.canonicalUrl, "/faction/manage/?slug=commonwealth-rangers");
  assert.equal(slug.backHref, "/faction/?slug=commonwealth-rangers");
  assert.equal(slug.title, "Commonwealth Rangers [CR]");

  const legacy = await runCase("?id=" + factionId);
  assert.deepEqual(legacy.lookup, { column: "id", value: factionId });
  assert.equal(legacy.canonicalUrl, "/faction/manage/?slug=commonwealth-rangers");
  assert.equal(legacy.backHref, "/faction/?slug=commonwealth-rangers");

  console.log("faction management URL checks passed");
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
