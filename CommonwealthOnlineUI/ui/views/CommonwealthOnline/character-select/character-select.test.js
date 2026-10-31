const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const SOURCE = fs.readFileSync(path.join(__dirname, "character-select.js"), "utf8");
const STYLES = fs.readFileSync(path.join(__dirname, "styles.css"), "utf8");
const MARKUP = fs.readFileSync(path.join(__dirname, "index.html"), "utf8");

function makeRoot() {
  return {
    innerHTML: "",
    hidden: true,
    listeners: {},
    addEventListener(type, fn) {
      this.listeners[type] = fn;
    },
    setAttribute(name) {
      if (name === "hidden") {
        this.hidden = true;
      }
    },
    removeAttribute(name) {
      if (name === "hidden") {
        this.hidden = false;
      }
    }
  };
}

function createHarness(dom) {
  const root = dom ? dom.root : makeRoot();
  const inputs = {};
  const sent = [];
  const timers = [];
  const document = dom ? dom.document : {
    readyState: "complete",
    getElementById(id) {
      return id === "coCharacterSelect" ? root : null;
    },
    querySelector(selector) {
      const match = /^#coSlotName-(\d+)$/.exec(selector);
      if (match) {
        return { value: inputs[match[1]] == null ? "" : inputs[match[1]] };
      }
      return null;
    },
    addEventListener() {}
  };
  const window = {
    coCharacterSelectAction(payload) {
      sent.push(JSON.parse(payload));
    }
  };
  const sandbox = { window, document, console, setTimeout: (fn) => timers.push(fn), clearTimeout() {} };
  sandbox.globalThis = sandbox;
  vm.createContext(sandbox);
  vm.runInContext(SOURCE, sandbox);
  const model = sandbox.window.coCharacterSelectModel;
  model.resetForTests();
  return { root, inputs, sent, timers, document, window, model };
}

function createLiveDom() {
  const root = makeRoot();
  const document = {
    readyState: "complete",
    activeElement: null,
    getElementById(id) {
      return id === "coCharacterSelect" ? root : null;
    },
    querySelector(selector) {
      return elements[selector.slice(1)] || null;
    },
    addEventListener() {}
  };
  let elements = {};
  let html = "";
  Object.defineProperty(root, "innerHTML", {
    get() {
      return html;
    },
    set(next) {
      html = next;
      elements = {};
      document.activeElement = null;
      for (const match of next.matchAll(/<input id="(coSlotName-\d+)"[^>]*>/g)) {
        const value = /value="([^"]*)"/.exec(match[0]);
        elements[match[1]] = {
          id: match[1],
          value: value ? value[1] : "",
          selectionStart: 0,
          selectionEnd: 0,
          focus() {
            document.activeElement = this;
          },
          setSelectionRange(start, end) {
            this.selectionStart = start;
            this.selectionEnd = end;
          }
        };
      }
    }
  });
  return { root, document };
}

function slotsState(overrides) {
  const base = {
    characterSlots: {
      supported: true,
      needsSelection: true,
      slots: [
        { slot: 0, name: "Hero", createdAt: 100, lastPlayedAt: 200 },
        { slot: 1, empty: true }
      ]
    }
  };
  return Object.assign(base, overrides || {});
}

{
  const h = createHarness();
  assert.equal(h.model.normalize("nope"), null, "garbage string must not parse");
  assert.equal(h.model.normalize({}), null, "missing block must not parse");
  assert.equal(JSON.stringify(h.model.normalize({ characterSlots: { supported: false } })), JSON.stringify({ enabled: false }));
  const parsed = h.model.normalize(slotsState());
  assert.equal(parsed.enabled, true);
  assert.equal(parsed.needsSelection, true);
  assert.equal(parsed.slots.length, 2);
  assert.equal(parsed.slots[0].name, "Hero");
  assert.equal(parsed.slots[1].empty, true);
  assert.equal(h.model.normalize({ characterSlots: { supported: true, slots: [{ slot: 99 }] } }).slots.length, 0,
    "slots without a valid shape must be dropped");
  assert.equal(h.model.normalize({ characterSlots: { supported: true, slots: [{ slot: 0, name: "x".repeat(129) }] } }).slots.length, 0,
    "overlong names must be dropped");
}

{
  const h = createHarness();
  h.model.setState(slotsState());
  assert.match(h.root.innerHTML, /Choose your character/);
  assert.match(h.root.innerHTML, /Hero/);
  assert.match(h.root.innerHTML, /data-op="slotSelect" data-slot="0"/);
  assert.match(h.root.innerHTML, /data-op="slotDelete" data-slot="0"/);
  assert.match(h.root.innerHTML, /Empty slot 2/);
  assert.match(h.root.innerHTML, /maxlength="48"/);
  assert.match(h.root.innerHTML, /data-op="slotCreate" data-slot="1"/);
  assert.match(h.root.innerHTML, /data-op="disconnect"/);
  assert.equal(h.root.hidden, false);
}

{
  const h = createHarness();
  h.model.setState({ characterSlots: { supported: true, needsSelection: false, slots: [] } });
  assert.match(h.root.innerHTML, /Loading into the Commonwealth/);
  assert.doesNotMatch(h.root.innerHTML, /data-op="slotSelect"/);
}

{
  const h = createHarness();
  assert.equal(h.model.normalize({ characterSlots: { loading: true } }).loading, true, "a reconnect block must normalize while loading");
  h.model.setState({ characterSlots: { supported: false, loading: true } });
  assert.match(h.root.innerHTML, /Loading character\.\.\./, "a reconnect must show the loading text");
  assert.doesNotMatch(h.root.innerHTML, /data-op="slotSelect"/, "no picker while the reconnect runs");
  assert.equal(h.root.hidden, false, "the panel must stay open across the reconnect");
}

{
  const h = createHarness();
  h.model.setState({ characterSlots: { supported: false } });
  assert.equal(h.root.innerHTML, "");
  assert.equal(h.root.hidden, true);
}

{
  const h = createHarness();
  h.model.setState(slotsState({
    characterSlots: {
      supported: true,
      needsSelection: true,
      legacy: true,
      notice: "Update <b>now</b>",
      slots: [{ slot: 0, name: "<script>alert(1)</script>" }]
    }
  }));
  assert.match(h.root.innerHTML, /older mod build/);
  assert.match(h.root.innerHTML, /Update &lt;b&gt;now&lt;\/b&gt;/, "server notice must be escaped");
  assert.doesNotMatch(h.root.innerHTML, /<script>alert/, "slot names must be escaped");
}

for (const [reason, text] of [
  ["invalidRequest", "not valid"],
  ["badSlot", "does not exist"],
  ["slotEmpty", "empty"],
  ["slotOccupied", "already has"],
  ["slotDeleted", "Restore it"],
  ["slotsFull", "full"],
  ["nameTaken", "already taken"],
  ["notDeleted", "not deleted"],
  ["alreadyActive", "Another session"],
  ["slotActive", "currently in use"],
  ["badToken", "did not match"],
  ["tokenExpired", "expired"],
  ["rateLimited", "Too many requests"],
  ["persistenceUnavailable", "unavailable right now"],
  ["slotsUnavailable", "unavailable on this server"]
]) {
  const h = createHarness();
  h.model.setState(slotsState({
    characterSlots: {
      supported: true,
      needsSelection: true,
      slots: [{ slot: 0, empty: true }],
      lastResult: { op: "create", ok: false, reason, slot: 0 }
    }
  }));
  assert.match(h.root.innerHTML, new RegExp(text), `reason ${reason} must map to clear text`);
  assert.doesNotMatch(h.root.innerHTML, new RegExp(reason), `raw reason code ${reason} must not leak`);
}

{
  const h = createHarness();
  h.model.setState(slotsState({
    characterSlots: {
      supported: true,
      needsSelection: true,
      slots: [{ slot: 0, empty: true }],
      lastResult: { op: "mystery", ok: false, reason: "weird", slot: 0 }
    }
  }));
  assert.match(h.root.innerHTML, /refused that request/, "unknown reasons fall back to generic text");
}

{
  const h = createHarness();
  h.model.setState(slotsState({
    characterSlots: {
      supported: true,
      needsSelection: true,
      slots: [{ slot: 0, empty: true, deleted: true }],
      lastResult: { op: "delete", ok: true, slot: 0 }
    }
  }));
  assert.match(h.root.innerHTML, /Undo delete/, "delete-ok must offer restore on the emptied slot");
  assert.deepEqual(JSON.parse(JSON.stringify(h.sent)), [{ op: "slotList" }], "mutating ok must refresh the list");
}

{
  const h = createHarness();
  const withResult = (slots) => slotsState({
    characterSlots: {
      supported: true,
      needsSelection: true,
      slots,
      lastResult: { op: "delete", ok: true, slot: 0 }
    }
  });
  h.model.setState(withResult([{ slot: 0, name: "Hero", createdAt: 100, lastPlayedAt: 200 }]));
  assert.equal(h.sent.length, 1, "the first list refresh must be sent");
  h.timers[0]();
  assert.equal(h.sent.length, 2, "a refresh with no list update must be sent again");
  h.model.setState(withResult([{ slot: 0, empty: true, deleted: true }]));
  h.timers[1]();
  assert.equal(h.sent.length, 2, "an answered refresh must not be sent again");
}

{
  const h = createHarness();
  h.model.setState(slotsState({
    characterSlots: {
      supported: true,
      needsSelection: true,
      slots: [{ slot: 2, name: "Doomed" }],
      pendingDelete: { slot: 2, token: "tok123", expiresInSeconds: 120 }
    }
  }));
  assert.match(h.root.innerHTML, /Doomed/);
  assert.match(h.root.innerHTML, /within 120 seconds/);
  assert.match(h.root.innerHTML, /data-op="slotDeleteConfirm" data-slot="2"/);
  h.model.handleClick({ op: "slotDeleteConfirm", slot: 2 });
  assert.deepEqual(JSON.parse(JSON.stringify(h.sent)), [{ op: "slotDelete", slot: 2, token: "tok123" }], "confirm must echo the token");
}

{
  const h = createHarness();
  h.model.setState(slotsState({
    characterSlots: {
      supported: true,
      needsSelection: true,
      slots: [{ slot: 2, name: "Doomed" }],
      pendingDelete: { slot: 2, token: "tok123", expiresInSeconds: 120 }
    }
  }));
  h.model.handleClick({ op: "slotDeleteCancel" });
  assert.doesNotMatch(h.root.innerHTML, /Yes, delete/, "keep must drop the confirm");
  assert.deepEqual(JSON.parse(JSON.stringify(h.sent)), [{ op: "slotDeleteCancel" }], "keep must clear the plugin pending token");
}

{
  const h = createHarness();
  h.inputs["1"] = "  padded";
  h.model.setState(slotsState());
  h.model.handleClick({ op: "slotCreate", slot: 1 });
  assert.equal(h.sent.length, 0, "bad names must not send");
  assert.match(h.root.innerHTML, /cannot start or end with a space/);
  h.inputs["1"] = "Va#ult";
  h.model.handleClick({ op: "slotCreate", slot: 1 });
  assert.equal(h.sent.length, 0, "charset violations must not send");
  h.inputs["1"] = "Nora";
  h.model.handleClick({ op: "slotCreate", slot: 1 });
  assert.deepEqual(JSON.parse(JSON.stringify(h.sent)), [{ op: "slotCreate", slot: 1, name: "Nora" }]);
  h.model.handleClick({ op: "slotSelect", slot: 0 });
  assert.deepEqual(JSON.parse(JSON.stringify(h.sent[1])), { op: "slotSelect", slot: 0 });
  h.model.handleClick({ op: "slotDelete", slot: 0 });
  assert.deepEqual(JSON.parse(JSON.stringify(h.sent[2])), { op: "slotDelete", slot: 0 });
  h.model.handleClick({ op: "refresh", slot: null });
  assert.deepEqual(JSON.parse(JSON.stringify(h.sent[3])), { op: "slotList" }, "refresh must re-list");
}

{
  const h = createHarness();
  assert.equal(h.model.validateName(""), "Use 1 to 48 characters.");
  assert.equal(h.model.validateName("x".repeat(49)), "Use 1 to 48 characters.");
  assert.equal(h.model.validateName(" bad"), "The name cannot start or end with a space.");
  assert.equal(h.model.validateName("a#b"), "The name cannot contain # or control characters.");
  assert.equal(h.model.validateName("Nora"), "");
}

{
  assert.match(STYLES, /\.co-select/, "styles must cover the panel");
  assert.match(MARKUP, /id="coCharacterSelect"/, "markup must host the panel root");
  assert.match(MARKUP, /character-select\.js/, "markup must load the panel script");
}

{
  const h = createHarness();
  h.model.setState(slotsState({
    characterSlots: {
      supported: true,
      needsSelection: true,
      slots: [{ slot: 0, empty: true, deleted: true, restorableUntil: 1800000000 }]
    }
  }));
  assert.match(h.root.innerHTML, /Restore before /, "a soft-deleted slot must show its restore deadline");
  assert.match(h.root.innerHTML, /Undo delete/);
}

{
  const h = createHarness();
  h.model.setState(slotsState({
    characterSlots: {
      supported: true,
      needsSelection: true,
      slots: [{ slot: 0, empty: true, deleted: true, restorableUntil: 1e17 }]
    }
  }));
  assert.doesNotMatch(h.root.innerHTML, /Restore before /, "an out-of-range restore deadline must not render a date");
  assert.match(h.root.innerHTML, /Undo delete/);
}

{
  const h = createHarness();
  h.model.setState(slotsState({
    characterSlots: {
      supported: true,
      needsSelection: true,
      slots: [{ slot: 0, name: "Hero", createdAt: 100, lastPlayedAt: 200 }],
      lastResult: { op: "delete", ok: true, slot: 0 }
    }
  }));
  h.timers[0]();
  h.timers[1]();
  assert.equal(h.sent.length, 3, "a refresh must be sent at most three times");
  h.timers[2]();
  assert.equal(h.sent.length, 3, "the refresh retry must stop at the cap");
}

{
  const dom = createLiveDom();
  const h = createHarness(dom);
  h.model.setState(slotsState());
  const field = dom.document.querySelector("#coSlotName-1");
  field.focus();
  let typed = "";
  for (const letter of "Nora") {
    typed += letter;
    field.value = typed;
    h.model.setState(slotsState());
    assert.equal(dom.document.querySelector("#coSlotName-1"), field, "a state tick after a keystroke must not rebuild the field");
    assert.equal(dom.document.activeElement, field, "a state tick after a keystroke must keep focus in the field");
  }
  assert.equal(field.value, "Nora");

  field.selectionStart = 2;
  field.selectionEnd = 3;
  h.model.setState(slotsState({
    characterSlots: Object.assign({}, slotsState().characterSlots, { notice: "Server restarting soon" })
  }));
  const rebuilt = dom.document.querySelector("#coSlotName-1");
  assert.notEqual(rebuilt, field, "a real state change still rebuilds the markup");
  assert.equal(dom.document.activeElement, rebuilt, "a rebuild must return focus to the field being typed in");
  assert.equal(rebuilt.value, "Nora", "a rebuild must keep the typed name");
  assert.equal(rebuilt.selectionStart, 2, "a rebuild must keep the caret");
  assert.equal(rebuilt.selectionEnd, 3, "a rebuild must keep the selection");

  dom.document.activeElement = null;
  h.model.setState(slotsState());
  assert.equal(dom.document.activeElement, null, "a rebuild must not pull focus into a field nobody was typing in");
  assert.equal(dom.document.querySelector("#coSlotName-1").value, "Nora", "a rebuild without focus still keeps the typed name");
  assert.doesNotMatch(h.root.innerHTML, /Nora/, "typed text must not enter the markup the rebuild check compares");
}

console.log("character-select: all checks passed.");
