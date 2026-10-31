const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const source = fs.readFileSync(__dirname + "/chat.js", "utf8");
const markup = fs.readFileSync(__dirname + "/index.html", "utf8");

function fakeElement(tagName) {
  return {
    tagName,
    className: "",
    textContent: "",
    style: {},
    children: [],
    appendChild(child) {
      this.children.push(child);
    }
  };
}

function load() {
  const window = {};
  const document = {
    readyState: "complete",
    activeElement: null,
    createElement: fakeElement,
    getElementById() {
      return null;
    },
    addEventListener() {}
  };
  vm.runInNewContext(source, { document, window, Array, Math, String, Object, JSON });
  return window;
}

const plain = (value) => JSON.parse(JSON.stringify(value));
const sample = (overrides) => Object.assign({
  channel: "global",
  messageId: "m1",
  fromPlayerId: "p1",
  fromName: "Alice",
  text: "hi",
  serverTime: 1
}, overrides);

const window = load();
const model = window.coChatModel;

{
  const line = model.buildLine(sample({
    fromName: "<script>alert(1)</script>",
    text: "<img src=x onerror=alert(1)>"
  }));
  const [name, text] = line.children;
  assert.equal(name.textContent, "<script>alert(1)</script>", "a name with <script> is set as text");
  assert.equal(text.textContent, "<img src=x onerror=alert(1)>", "message text with markup is set as text");
  assert.equal(line.innerHTML, undefined, "the list is built without innerHTML");
  assert.equal(name.innerHTML, undefined, "the name is built without innerHTML");
}

{
  const line = model.buildLine(sample({ fromPlayerId: "p2" }));
  assert.equal(line.children[0].style.color, model.colorFor("p2"), "the name uses the colour for its playerId");
  assert.equal(model.colorFor("76561198000000001"), model.colorFor("76561198000000001"), "colour is stable per playerId");
  assert.match(model.colorFor("p1"), /^hsl\(\d+, 70%, 72%\)$/);
  const colours = new Set(["p1", "p2", "p3", "p4", "p5", "p6", "p7", "p8"].map((id) => model.colorFor(id)));
  assert.ok(colours.size > 1, "different players get different colours");
}

{
  assert.equal(model.countCodePoints("😀"), 1, "an emoji counts as one code point");
  assert.equal(model.countCodePoints("a😀b"), 3);
  assert.equal(model.counterText(""), "0/256");
  assert.equal(model.counterText("😀".repeat(256)), "256/256");
  assert.equal(model.counterText("😀".repeat(257)), "257/256");
}

{
  assert.deepEqual(plain(model.sendAction("  hello  ")), { op: "send", channel: "global", text: "hello" });
  assert.deepEqual(plain(model.sendAction("hi", "faction")), { op: "send", channel: "faction", text: "hi" },
    "the window sends on the selected channel");
  assert.deepEqual(plain(model.sendAction("😀".repeat(256))), { op: "send", channel: "global", text: "😀".repeat(256) });
  assert.equal(model.sendAction("   "), null, "blank text is not sent");
  assert.equal(model.sendAction("😀".repeat(257)), null, "text over 256 code points is not sent");
  assert.deepEqual(plain(model.historyAction()), { op: "history" });
  assert.deepEqual(plain(model.focusAction(true)), { op: "focus", focused: true }, "focus drives the game text entry mode");
  assert.deepEqual(plain(model.focusAction()), { op: "focus", focused: false });
  assert.deepEqual(plain(model.closeAction()), { op: "close" }, "closing asks the UI DLL to hide the window");
  assert.equal(model.channelFor("faction"), "faction");
  assert.equal(model.channelFor("global"), "global");
  assert.equal(model.channelFor(undefined), "global", "an unknown channel falls back to global");
}

{
  let state = model.reduce(model.initialState(), {
    kind: "history",
    messages: [sample({ text: "a" }), sample({ text: "b" })]
  });
  state = model.reduce(state, { kind: "history", messages: [sample({ text: "c" })] });
  assert.deepEqual(plain(state.messages.map((message) => message.text)), ["c"], "history replaces the list");

  state = model.reduce(state, { kind: "message", message: sample({ text: "d" }) });
  assert.equal(state.messages.length, 2, "a live message appends");

  const many = Array.from({ length: 105 }, (_, index) => sample({ messageId: "m" + index, text: "t" + index }));
  state = model.reduce(state, { kind: "history", messages: many });
  assert.equal(state.messages.length, 100, "the list keeps the last 100 messages");
  assert.equal(state.messages[0].text, "t5");
}

{
  const codes = ["invalidText", "tooLong", "rateLimited", "channelUnavailable", "muted", "duplicate", "notInFaction"];
  const texts = new Set();
  for (const reason of codes) {
    const state = model.reduce(model.initialState(), { kind: "rejected", reason });
    assert.notEqual(state.rejected, "Your message was not sent.", `${reason} needs its own text`);
    assert.equal(state.rejected, model.reasonText(reason));
    texts.add(state.rejected);
  }
  assert.equal(texts.size, codes.length, "each rejection reason has distinct text");
  assert.equal(model.reasonText("constructor"), "Your message was not sent.", "prototype keys fall back");
  assert.equal(model.reasonText("notInFaction"), "You are not in a faction.",
    "the Faction channel is offered outside a faction and the server explains the refusal");
}

{
  let state = model.reduce(model.initialState(), { kind: "state", enabled: true, localPlayerId: "p1" });
  assert.equal(state.enabled, true);
  assert.equal(state.localPlayerId, "p1");
  state = model.reduce(state, { kind: "rejected", reason: "muted" });
  state = model.reduce(state, { kind: "message", message: sample({ fromPlayerId: "p1" }) });
  assert.equal(state.rejected, "", "the local player's next message clears the rejection");
  assert.equal(model.reduce(state, { kind: "state", enabled: "yes" }).enabled, false, "enabled must be exactly true");
  state = model.reduce(state, { kind: "state", enabled: false, localPlayerId: "p1" });
  assert.equal(state.messages.length, 0, "disabling chat clears the message list");
}

{
  const line = model.buildLine(sample({ channel: "faction", fromName: "Bob", text: "team" }));
  assert.equal(line.children.length, 3, "a faction line has a label before the name and text");
  assert.equal(line.children[0].className, "co-chat-channel-tag");
  assert.equal(line.children[0].textContent, "[Faction]");
  assert.equal(line.children[1].textContent, "Bob");
  assert.equal(line.children[2].textContent, "team");
  assert.equal(model.buildLine(sample()).children.length, 2, "a global line has no faction label");
}

{
  const sent = [];
  const focused = [];
  const blurred = [];
  const domElements = {};
  const domElement = () => ({
    attrs: {},
    listeners: {},
    value: "",
    textContent: "",
    disabled: false,
    placeholder: "",
    scrollTop: 0,
    scrollHeight: 0,
    classList: {
      removed: [],
      added: [],
      toggle() {},
      remove(name) {
        this.removed.push(name);
      },
      add(name) {
        this.added.push(name);
      }
    },
    setAttribute(name, value) {
      this.attrs[name] = value;
    },
    addEventListener(type, fn) {
      this.listeners[type] = fn;
    },
    contains(node) {
      return node === this;
    },
    appendChild() {},
    focus() {
      focused.push(true);
    },
    blur() {
      blurred.push(true);
    }
  });
  const documentListeners = {};
  const domWindow = {
    addEventListener() {},
    coChatAction: (payload) => sent.push(JSON.parse(payload))
  };
  const domDocument = {
    readyState: "complete",
    activeElement: null,
    createElement: fakeElement,
    getElementById(id) {
      if (!domElements[id]) {
        domElements[id] = domElement();
      }
      return domElements[id];
    },
    addEventListener(type, fn) {
      documentListeners[type] = fn;
    }
  };
  vm.runInNewContext(source, { document: domDocument, window: domWindow, Array, Math, String, Object, JSON });

  domWindow.coChatEvent({ kind: "state", enabled: true, localPlayerId: "p1" });
  domWindow.coChatOpen();
  assert.deepEqual(plain(domElements.coChat.classList.removed), ["is-closed"], "opening reveals the window");
  assert.equal(focused.length, 1, "opening focuses the composer so typing lands in the chat");
  assert.deepEqual(sent[0], { op: "history" }, "the first open asks for history");
  domWindow.coChatOpen();

  domElements.coChatInput.value = "  hello there  ";
  domElements.coChatInput.listeners.input();
  assert.equal(domElements.coChatCount.textContent, "15/256", "the counter follows the raw typed text");
  documentListeners.keydown({ key: "Enter", preventDefault() {} });
  assert.deepEqual(sent[sent.length - 3], { op: "send", channel: "global", text: "hello there" },
    "Enter sends on the global channel with the text trimmed");
  assert.equal(domElements.coChatInput.value, "", "the composer clears after sending");
  assert.deepEqual(sent.slice(-3), [
    { op: "send", channel: "global", text: "hello there" },
    { op: "focus", focused: false },
    { op: "close" }
  ], "a send releases text entry and asks the UI DLL to close the window");
  const closesAfterFirstSend = sent.filter((action) => action.op === "close").length;
  domElements.coChatInput.value = "   ";
  documentListeners.keydown({ key: "Enter", preventDefault() {} });
  assert.equal(sent.filter((action) => action.op === "close").length, closesAfterFirstSend,
    "an empty Enter sends nothing and does not close");
  domElements.coChatInput.value = "x".repeat(257);
  documentListeners.keydown({ key: "Enter", preventDefault() {} });
  assert.equal(sent.filter((action) => action.op === "close").length, closesAfterFirstSend,
    "an over-long draft is refused and the window stays open");
  domElements.coChatInput.value = "";

  documentListeners.keydown({ key: "Tab", preventDefault() {} });
  assert.equal(domElements.coChannelFaction.attrs["aria-selected"], "true", "Tab switches to the faction channel");
  assert.equal(domElements.coChatInput.placeholder, "Message faction chat");
  documentListeners.keydown({ key: "Tab", preventDefault() {} });
  assert.equal(domElements.coChannelGlobal.attrs["aria-selected"], "true", "Tab switches back to global");

  domElements.coChannelFaction.listeners.click();
  assert.equal(domElements.coChannelFaction.attrs["aria-selected"], "true", "the faction channel is clickable");
  domElements.coChatInput.value = "team up";
  documentListeners.keydown({ key: "Enter", preventDefault() {} });
  assert.deepEqual(sent[sent.length - 3], { op: "send", channel: "faction", text: "team up" },
    "the selected channel is used for the next send");

  assert.equal(typeof documentListeners.mousedown, "function",
    "the page has to listen for a mousedown anywhere on its surface");
  const focusBeforeOutsideClick = focused.length;
  let outsidePrevented = 0;
  documentListeners.mousedown({ target: {}, preventDefault() { outsidePrevented += 1; } });
  assert.equal(outsidePrevented, 1, "a mousedown outside the window is cancelled before the browser blurs the composer");
  assert.equal(focused.length, focusBeforeOutsideClick + 1, "a mousedown outside puts focus back on the composer");
  assert.equal(sent.filter((action) => action.op === "close").length, closesAfterFirstSend + 1,
    "a mousedown outside never closes the chat window; only the sends did");

  let outsideClickPrevented = 0;
  let outsideClickStopped = 0;
  documentListeners.click({
    target: {},
    preventDefault() { outsideClickPrevented += 1; },
    stopPropagation() { outsideClickStopped += 1; }
  });
  assert.equal(outsideClickPrevented, 1, "a click outside the window is swallowed");
  assert.equal(outsideClickStopped, 1, "a click outside the window never reaches anything below it");

  const focusBeforeListClick = focused.length;
  let listPrevented = 0;
  documentListeners.mousedown({ target: domElements.coChatList, preventDefault() { listPrevented += 1; } });
  assert.equal(listPrevented, 1, "a mousedown on the message list cannot blur the composer either");
  assert.equal(focused.length, focusBeforeListClick + 1, "a mousedown on the message list returns focus to the composer");

  let inputPrevented = 0;
  const focusBeforeInputClick = focused.length;
  documentListeners.mousedown({ target: domElements.coChatInput, preventDefault() { inputPrevented += 1; } });
  assert.equal(inputPrevented, 0, "a mousedown in the composer is left to the browser so the caret can be placed");
  assert.equal(focused.length, focusBeforeInputClick, "a mousedown in the composer does not re-focus it");

  let closePrevented = 0;
  documentListeners.mousedown({ target: domElements.coChatClose, preventDefault() { closePrevented += 1; } });
  assert.equal(closePrevented, 0, "the close button still takes focus, because its hide hides the pointer");

  let insidePrevented = 0;
  documentListeners.click({ target: domElements.coChat, preventDefault() { insidePrevented += 1; } });
  assert.equal(insidePrevented, 0, "a click inside the window is left alone");

  documentListeners.keydown({ key: "Escape", preventDefault() {} });
  assert.deepEqual(sent.slice(-2), [{ op: "focus", focused: false }, { op: "close" }],
    "Escape releases the game text entry mode and asks the UI DLL to close the window");

  const focusBeforeMouseSwitch = focused.length;
  let mousedownCancelled = 0;
  domElements.coChannelGlobal.listeners.mousedown({ preventDefault() { mousedownCancelled += 1; } });
  assert.equal(mousedownCancelled, 1, "a channel mousedown is cancelled so the click never moves focus off the composer");
  domElements.coChannelGlobal.listeners.click();
  assert.equal(domElements.coChannelGlobal.attrs["aria-selected"], "true", "a mouse channel switch selects the channel");
  assert.equal(focused.length, focusBeforeMouseSwitch + 1, "a mouse channel switch hands focus back to the composer");
  assert.equal(mousedownCancelled, 1, "selecting a channel does not re-enter the mousedown handler");

  domWindow.coChatEvent({ kind: "state", enabled: false, localPlayerId: "p1" });
  const focusWhileDisabled = focused.length;
  domElements.coChannelFaction.listeners.click();
  assert.equal(focused.length, focusWhileDisabled, "a disabled channel button never pulls focus back to the composer");

  domWindow.coChatClose();
  assert.deepEqual(plain(domElements.coChat.classList.added), ["is-closed"],
    "the UI DLL's close command hides the window");
  assert.equal(blurred.length, 1, "the composer is blurred when the window closes");

  let preventedWhileClosed = 0;
  documentListeners.mousedown({ target: {}, preventDefault() { preventedWhileClosed += 1; } });
  assert.equal(preventedWhileClosed, 0, "a mousedown outside a closed window is not swallowed");
}

{
  assert.match(markup, /id="coChat"/, "the window is its own page root");
  assert.match(markup, /id="coChatInput"/, "the window owns its own composer");
  assert.doesNotMatch(source, /coFactionAction/, "the chat window never drives the Social window");
  assert.match(source, /addEventListener\("mousedown", keepComposerFocus\)/,
    "the channel buttons cancel mousedown so the pointer does not release the game text entry mode");
  assert.doesNotMatch(source, /faction-tabs/, "chat no longer lives in a tab bar");
  assert.doesNotMatch(markup, /faction-tabs/, "the chat page has no Social tab bar");
}

assert.doesNotThrow(() => window.coChatOpen(), "opening without a DOM is ignored");
assert.doesNotThrow(() => window.coChatEvent({ kind: "state", enabled: true, localPlayerId: "p1" }),
  "events without a DOM are ignored");

console.log("Chat window regression passed");
