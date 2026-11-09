const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const OVERLAY = fs.readFileSync(path.join(__dirname, "overlay.js"), "utf8");
const STYLES = fs.readFileSync(path.join(__dirname, "styles.css"), "utf8");
const MARKUP = fs.readFileSync(path.join(__dirname, "index.html"), "utf8");

const ALL_STATES = [
  "Connecting",
  "Authenticating",
  "LoadingWorld",
  "FlyOver",
  "Spawning",
  "Ready",
  "Failed",
  "Cancelled"
];

const VISIBLE_STATES = ["Connecting", "Authenticating", "LoadingWorld", "FlyOver", "Spawning"];
const HIDDEN_STATES = ["Ready", "Failed", "Cancelled"];

function makeRoot() {
  return {
    innerHTML: "",
    hidden: true,
    querySelector() {
      return null;
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

function createHarness() {
  const root = makeRoot();
  const document = {
    readyState: "complete",
    getElementById(id) {
      return id === "coJoinOverlay" ? root : null;
    },
    addEventListener() {}
  };
  const window = {};
  vm.runInNewContext(OVERLAY, {
    window,
    document,
    JSON,
    String,
    Number,
    Object,
    Array,
    Math,
    isFinite,
    console
  });
  return { window, root, document, model: window.coJoinOverlayModel };
}

function TestStatusTableCoversEveryState() {
  const { model } = createHarness();

  assert.deepEqual(Object.keys(model.STATUS).sort(), ALL_STATES.slice().sort());
  assert.equal(model.STATUS.Connecting, "Connecting to server");
  assert.equal(model.STATUS.Authenticating, "Authenticating");
  assert.equal(model.STATUS.LoadingWorld, "Loading into world");
  assert.equal(model.STATUS.FlyOver, "Welcome to Commonwealth Online");
  assert.equal(model.STATUS.Spawning, "Entering the Commonwealth");
  assert.equal(model.STATUS.Ready, "");
  assert.equal(model.STATUS.Failed, "Join failed");
  assert.equal(model.STATUS.Cancelled, "Join cancelled");

  for (const name of ALL_STATES) {
    assert.equal(typeof model.STATUS[name], "string", `${name} needs a status line`);
  }
}

function TestVisibleStatesShow() {
  for (const name of VISIBLE_STATES) {
    const harness = createHarness();
    const accepted = harness.window.coJoinOverlaySetState({
      state: name,
      status: "",
      progress: 0.5,
      visible: true,
      dismissible: false
    });
    assert.equal(accepted, true, `${name} must be accepted`);
    assert.equal(harness.root.hidden, false, `${name} must show the overlay`);
    assert.ok(harness.root.innerHTML.includes(harness.model.LOGO_SRC), `${name} must show the CO logo`);
    assert.ok(
      harness.root.innerHTML.includes(harness.model.STATUS[name]),
      `${name} must show its status line`
    );
  }
}

function TestTerminalStatesHide() {
  for (const name of HIDDEN_STATES) {
    const harness = createHarness();
    const model = harness.model;

    assert.equal(model.isVisible(model.normalize({ state: name, visible: true })), false, `${name} hides`);
    assert.equal(model.render(model.normalize({ state: name, visible: true })), "", `${name} renders nothing`);

    assert.equal(harness.window.coJoinOverlaySetState({ state: name, visible: true }), true);
    assert.equal(harness.root.hidden, true, `${name} must hide the overlay`);
    assert.equal(harness.root.innerHTML, "", `${name} must clear the overlay`);
  }
}

function TestVisibleFalseHides() {
  const harness = createHarness();

  assert.equal(harness.window.coJoinOverlaySetState({ state: "LoadingWorld", visible: true }), true);
  assert.equal(harness.root.hidden, false);

  assert.equal(harness.window.coJoinOverlaySetState({ state: "LoadingWorld", visible: false }), true);
  assert.equal(harness.root.hidden, true);
  assert.equal(harness.root.innerHTML, "");
}

function TestHideEntryPoint() {
  const harness = createHarness();

  assert.equal(harness.window.coJoinOverlaySetState({ state: "Spawning" }), true);
  assert.equal(harness.root.hidden, false);

  assert.equal(harness.window.coJoinOverlayHide(), true);
  assert.equal(harness.root.hidden, true);
  assert.equal(harness.root.innerHTML, "");
}

function TestUnknownStatesIgnored() {
  const harness = createHarness();

  assert.equal(harness.window.coJoinOverlaySetState({ state: "Connecting" }), true);
  const painted = harness.root.innerHTML;

  for (const state of ["Warping", "connecting", "", 7, null, undefined, {}, []]) {
    assert.equal(harness.window.coJoinOverlaySetState({ state }), false, `unknown state ${String(state)} is ignored`);
    assert.equal(harness.root.innerHTML, painted, "an ignored push must not repaint");
    assert.equal(harness.root.hidden, false);
  }

  assert.equal(harness.model.normalize({ state: "Downed" }), null);
  assert.equal(harness.model.normalize({ state: "Ready " }), null);
}

function TestMalformedPayloadDoesNotThrow() {
  const harness = createHarness();

  const payloads = [
    "{",
    "not json",
    "",
    "null",
    "[]",
    "42",
    null,
    undefined,
    7,
    [],
    {},
    { state: 42 },
    { state: {} },
    '{"state":"LoadingWorld"'
  ];

  for (const payload of payloads) {
    assert.doesNotThrow(() => harness.window.coJoinOverlaySetState(payload));
    assert.equal(harness.window.coJoinOverlaySetState(payload), false, `${String(payload)} is rejected`);
  }

  assert.equal(harness.window.coJoinOverlaySetState('{"state":"LoadingWorld","progress":0.35}'), true);
  assert.equal(harness.root.hidden, false);
}

function TestProgressAndStatus() {
  const { model } = createHarness();

  assert.equal(model.normalize({ state: "LoadingWorld", progress: 0.35 }).progress, 0.35);
  assert.equal(model.normalize({ state: "LoadingWorld", progress: 3 }).progress, 1);
  assert.equal(model.normalize({ state: "LoadingWorld", progress: -3 }).progress, 0);
  assert.equal(model.normalize({ state: "LoadingWorld", progress: "half" }).progress, 0);
  assert.equal(model.normalize({ state: "LoadingWorld", progress: "0.5" }).progress, 0.5);

  assert.equal(model.statusFor("LoadingWorld", ""), "Loading into world");
  assert.equal(model.statusFor("LoadingWorld", "Streaming terrain"), "Streaming terrain");
  assert.equal(model.statusFor("LoadingWorld", "   "), "Loading into world");
  assert.equal(model.statusFor("LoadingWorld", 7), "Loading into world");

  const rendered = model.render(model.normalize({ state: "LoadingWorld", progress: 0.35 }));
  assert.ok(rendered.includes("width:35%"));
  assert.ok(rendered.includes("Loading into world"));

  const escaped = model.render(model.normalize({ state: "LoadingWorld", status: "<b>x</b>" }));
  assert.ok(escaped.includes("&lt;b&gt;"), "the status line must be escaped");
  assert.ok(!escaped.includes("<b>"));

  const clamped = model.render(model.normalize({ state: "LoadingWorld", progress: 9 }));
  assert.ok(clamped.includes("width:100%"));
}

function TestNoInputCapture() {
  assert.doesNotMatch(OVERLAY, /addEventListener\(\s*["']key/);
  assert.doesNotMatch(OVERLAY, /["']key(down|up|press)["']/);
  assert.doesNotMatch(OVERLAY, /\.focus\(/);
  assert.doesNotMatch(OVERLAY, /tabindex|tabIndex/i);
  assert.doesNotMatch(OVERLAY, /preventDefault/);

  assert.doesNotMatch(MARKUP, /<button|<input|<select|<textarea|tabindex/i);
  assert.doesNotMatch(MARKUP, /on(click|key|focus)=/i);

  assert.match(STYLES, /pointer-events:\s*none/);
  assert.match(STYLES, /#coJoinOverlay\s*\{[\s\S]*?pointer-events:\s*none/);
  assert.doesNotMatch(STYLES, /overflow(-y)?\s*:\s*(auto|scroll)\b/);
  assert.doesNotMatch(STYLES, /outline:\s*/);
}

function TestScrimOnly() {
  assert.match(STYLES, /linear-gradient\(/);
  assert.match(STYLES, /html,\s*body\s*\{[^}]*background:\s*transparent/);
  assert.doesNotMatch(STYLES, /background:\s*#[0-9a-fA-F]{3,8}\b/);
  assert.match(STYLES, /rgba\(4,\s*8,\s*6,\s*0\)/);
}

function TestLoggerAndAssets() {
  const { model } = createHarness();

  assert.equal(model.LOGO_SRC, "../shared/logo.svg");
  assert.equal(model.ROOT_ID, "coJoinOverlay");
  assert.ok(fs.existsSync(path.join(__dirname, "..", "shared", "logo.svg")), "the shared CO logo must exist");
}

function TestDocumentGuard() {
  const window = {};
  vm.runInNewContext(OVERLAY, {
    window,
    JSON,
    String,
    Number,
    Object,
    Array,
    Math,
    isFinite,
    console
  });

  assert.equal(typeof window.coJoinOverlaySetState, "function");
  assert.equal(typeof window.coJoinOverlayHide, "function");
  assert.doesNotThrow(() => window.coJoinOverlaySetState({ state: "LoadingWorld" }));
  assert.equal(window.coJoinOverlaySetState({ state: "LoadingWorld" }), true);
  assert.equal(window.coJoinOverlayHide(), true);
}

function main() {
  TestStatusTableCoversEveryState();
  TestVisibleStatesShow();
  TestTerminalStatesHide();
  TestVisibleFalseHides();
  TestHideEntryPoint();
  TestUnknownStatesIgnored();
  TestMalformedPayloadDoesNotThrow();
  TestProgressAndStatus();
  TestNoInputCapture();
  TestScrimOnly();
  TestLoggerAndAssets();
  TestDocumentGuard();
  console.log("Join overlay regression passed");
}

main();
