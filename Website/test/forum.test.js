"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

class FakeElement {
  constructor(tag = "div") {
    this.tag = tag;
    this.children = [];
    this.dataset = {};
    this.hidden = false;
    this.textContent = "";
    this.className = "";
    this.href = "";
    this.attributes = {};
  }

  setAttribute(name, value) {
    this.attributes[name] = String(value);
  }

  addEventListener(type, handler) {
    this.listeners ||= {};
    this.listeners[type] = handler;
  }

  querySelector(selector) {
    return this.nodes?.[selector] || null;
  }

  replaceChildren(...items) {
    this.children = [...items];
  }

  append(...items) {
    this.children.push(...items);
  }
}

const flush = () => new Promise((resolve) => setImmediate(resolve));

const runIndex = async () => {
  const source = fs.readFileSync(path.join(__dirname, "..", "static", "js", "forum.js"), "utf8");
  const root = new FakeElement();
  root.dataset = { supabaseUrl: "https://project.example", supabaseKey: "sb_publishable_test" };
  const categories = new FakeElement();
  const threads = new FakeElement();
  const status = new FakeElement();
  const heading = new FakeElement();
  root.nodes = {
    "[data-forum-categories]": categories,
    "[data-forum-threads]": threads,
    "[data-forum-status]": status,
    "[data-forum-heading]": heading,
  };

  const document = {
    querySelector(selector) {
      if (selector === "[data-forum]") return root;
      if (selector === ".site-brand__logo") return { src: "https://commonwealth-online.com/assets/branding/logo.svg" };
      return null;
    },
    createElement(tag) {
      return new FakeElement(tag);
    },
  };

  const fetch = async (input) => {
    const url = String(input);
    if (new URL(url).pathname.endsWith("/rest/v1/forum_categories")) {
      return {
        ok: true,
        async json() {
          return [{ id: 1, slug: "general", name: "General", description: "General discussion", position: 1, is_locked: false }];
        },
      };
    }
    return {
      ok: true,
      async json() {
        return [{
          id: 42,
          title: "First discussion",
          is_pinned: false,
          is_locked: false,
          created_at: "2026-09-21T20:00:00Z",
          updated_at: "2026-09-21T20:00:00Z",
          category: { slug: "general", name: "General" },
          author: { display_name: "Nomad" },
        }];
      },
    };
  };

  vm.runInNewContext(source, {
    URL,
    URLSearchParams,
    Intl,
    Date,
    encodeURIComponent,
    document,
    fetch,
    window: { location: { search: "" } },
  });
  await flush();

  assert.equal(categories.children.length, 2);
  assert.equal(threads.children.length, 1);
  assert.equal(threads.children[0].href, "/forum/thread/?id=42");
  assert.equal(threads.children[0].children[1].children[0].textContent, "First discussion");
  assert.equal(status.hidden, true);
};

const runThread = async () => {
  const source = fs.readFileSync(path.join(__dirname, "..", "static", "js", "forum-thread.js"), "utf8");
  const root = new FakeElement();
  root.dataset = { supabaseUrl: "https://project.example", supabaseKey: "sb_publishable_test" };
  const status = new FakeElement();
  const posts = new FakeElement();
  const title = new FakeElement();
  const category = new FakeElement();
  const meta = new FakeElement();
  root.nodes = {
    "[data-thread-status]": status,
    "[data-thread-posts]": posts,
    "[data-thread-title]": title,
    "[data-thread-category]": category,
    "[data-thread-meta]": meta,
  };

  const document = {
    title: "",
    querySelector(selector) {
      return selector === "[data-forum-thread]" ? root : null;
    },
    createElement(tag) {
      return new FakeElement(tag);
    },
  };

  const fetch = async (input) => {
    const pathname = new URL(String(input)).pathname;
    if (pathname.endsWith("/rest/v1/forum_threads")) {
      return {
        ok: true,
        async json() {
          return [{
            id: 42,
            title: "First discussion",
            created_at: "2026-09-21T20:00:00Z",
            is_pinned: false,
            is_locked: false,
            category: { name: "General" },
            author: { id: "user-1", display_name: "Nomad" },
          }];
        },
      };
    }
    if (pathname.endsWith("/rest/v1/forum_posts")) {
      return {
        ok: true,
        async json() {
          return [{
            id: 1,
            body: "Hello Commonwealth",
            edited_at: null,
            created_at: "2026-09-21T20:01:00Z",
            author: {
              id: "user-1",
              display_name: "Nomad",
              avatar_url: "/assets/profile-icons/armorer.png",
            },
          }];
        },
      };
    }
    if (pathname.endsWith("/rest/v1/forum_reactions")) {
      return {
        ok: true,
        async json() {
          return [];
        },
      };
    }
    throw new Error("Unexpected forum request");
  };

  vm.runInNewContext(source, {
    URL,
    URLSearchParams,
    Intl,
    Date,
    encodeURIComponent,
    document,
    fetch,
    window: { location: { search: "?id=42" } },
  });
  await flush();

  assert.equal(title.textContent, "First discussion");
  assert.equal(category.textContent, "General");
  assert.equal(posts.children.length, 1);
  assert.equal(posts.children[0].children[1].children[1].textContent, "Hello Commonwealth");
  assert.equal(status.hidden, true);
};

Promise.resolve()
  .then(runIndex)
  .then(runThread)
  .then(() => console.log("forum browsing checks passed"))
  .catch((error) => {
    console.error(error);
    process.exitCode = 1;
  });
