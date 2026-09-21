"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

class FakeElement {
  constructor() {
    this.children = [];
    this.dataset = {};
    this.hidden = false;
    this.textContent = "";
    this.value = "";
    this.className = "";
  }

  querySelector(selector) {
    return this.nodes?.[selector] || null;
  }

  addEventListener(type, handler) {
    this.listeners ||= {};
    this.listeners[type] = handler;
  }

  replaceChildren() {
    this.children = [];
  }

  append(...children) {
    this.children.push(...children);
  }
}

const source = fs.readFileSync(path.join(__dirname, "..", "static", "js", "members.js"), "utf8");

const run = async () => {
  const root = new FakeElement();
  root.dataset.supabaseUrl = "https://example.supabase.co";
  root.dataset.supabaseKey = "sb_publishable_test";

  const status = new FakeElement();
  const list = new FakeElement();
  const search = new FakeElement();
  const count = new FakeElement();
  list.hidden = true;
  root.nodes = {
    "[data-members-status]": status,
    "[data-members-list]": list,
    "[data-members-search]": search,
    "[data-members-count]": count,
  };

  let request;
  const document = {
    querySelector(selector) {
      if (selector === "[data-members]") return root;
      if (selector === ".site-brand__logo") {
        return { src: "https://commonwealth-online.com/assets/branding/CommonwealthOnlineLogo.svg" };
      }
      return null;
    },
    createElement() {
      return new FakeElement();
    },
  };

  const fetch = async (input, options) => {
    request = { input: String(input), options };
    return {
      ok: true,
      async json() {
        return [
          {
            id: "member-1",
            display_name: "Nomad",
            avatar_url: "/assets/profile-icons/profile-01.svg",
            user_profile_details: { faction: "Minutemen", is_public: true },
            user_presence: { status: "in_game" },
          },
        ];
      },
    };
  };

  vm.runInNewContext(source, {
    URL,
    encodeURIComponent,
    document,
    fetch,
    window: {},
  });

  await new Promise((resolve) => setImmediate(resolve));

  assert.match(request.input, /\/rest\/v1\/profiles/);
  assert.equal(request.options.headers.apikey, "sb_publishable_test");
  assert.equal(list.hidden, false);
  assert.equal(list.children.length, 1);
  assert.equal(count.textContent, "1");
  assert.equal(status.hidden, true);
  assert.equal(list.children[0].children[1].children[0].textContent, "Nomad");

  root.dataset.supabaseUrl = "";
  status.hidden = false;
  count.textContent = "";
  vm.runInNewContext(source, {
    URL,
    encodeURIComponent,
    document,
    fetch,
    window: {},
  });
  assert.equal(status.textContent, "Member directory is not configured.");
  assert.equal(status.dataset.tone, "error");
};

run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
