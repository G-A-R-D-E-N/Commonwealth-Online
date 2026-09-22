"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const source = fs.readFileSync(path.join(__dirname, "..", "static", "js", "faction.js"), "utf8");
const factionId = "11111111-2222-3333-4444-555555555555";

const element = () => ({
  hidden: false,
  textContent: "",
  href: "",
  src: "",
  disabled: false,
  onclick: null,
  children: [],
  classList: { toggle() {} },
  append(...items) { this.children.push(...items); },
  replaceChildren(...items) { this.children = [...items]; },
});

const runCase = async (search) => {
  const nodes = new Map();
  for (const selector of [
    "[data-faction-status]",
    "[data-faction-members]",
    "[data-faction-membership-card]",
    "[data-faction-membership-copy]",
    "[data-faction-membership-action]",
    "[data-faction-membership-secondary]",
    "[data-faction-manage-link]",
    "[data-faction-name]",
    "[data-faction-tag]",
    "[data-faction-summary]",
    "[data-faction-lore]",
    "[data-faction-focus]",
    "[data-faction-recruitment]",
  ]) nodes.set(selector, element());

  const root = element();
  root.dataset = { supabaseUrl: "https://project.example", supabaseKey: "sb_publishable_test" };
  root.querySelector = (selector) => nodes.get(selector) || null;

  let lookup = null;
  let canonicalUrl = "";

  const client = {
    auth: {
      async getSession() { return { data: { session: null } }; },
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
                summary: "Summary",
                lore: "Lore",
                focus: "mixed",
                recruitment: "application",
                status: "active",
              },
              error: null,
            };
          },
        };
      }

      if (table === "faction_roles") {
        return {
          select() { return this; },
          async eq() {
            return { data: [{ id: "leader", name: "Leader", priority: 0 }], error: null };
          },
        };
      }

      if (table === "faction_members") {
        return {
          select() { return this; },
          eq() { return this; },
          async order() {
            return {
              data: [{
                user_id: "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
                role_id: "leader",
                joined_at: "2026-09-21T00:00:00Z",
                user: {
                  id: "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
                  display_name: "Nomad",
                  avatar_url: "/assets/profile-icons/armorer.png",
                },
              }],
              error: null,
            };
          },
        };
      }

      throw new Error("Unexpected table: " + table);
    },
  };

  const document = {
    title: "",
    querySelector(selector) {
      if (selector === "[data-faction]") return root;
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
      location: { search, assign() {} },
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
    rosterHref: nodes.get("[data-faction-members]").children[0]?.href || "",
  };
};

(async () => {
  const slug = await runCase("?slug=commonwealth-rangers");
  assert.deepEqual(slug.lookup, { column: "slug", value: "commonwealth-rangers" });
  assert.equal(slug.canonicalUrl, "/faction/?slug=commonwealth-rangers");
  assert.equal(slug.rosterHref, "/member/?username=Nomad");
  assert.doesNotMatch(slug.rosterHref, /\?id=/);

  const legacy = await runCase("?id=" + factionId);
  assert.deepEqual(legacy.lookup, { column: "id", value: factionId });
  assert.equal(legacy.canonicalUrl, "/faction/?slug=commonwealth-rangers");

  console.log("faction public URL checks passed");
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
