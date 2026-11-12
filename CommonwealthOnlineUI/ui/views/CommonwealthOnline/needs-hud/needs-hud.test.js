const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const source = fs.readFileSync(__dirname + "/needs-hud.js", "utf8");
const css = fs.readFileSync(__dirname + "/styles.css", "utf8");

function fakeElement() {
  return {
    hidden: false,
    attributes: {},
    style: {
      setProperty(name, value) {
        this[name] = value;
      }
    },
    setAttribute(name, value) {
      this.attributes[name] = String(value);
    }
  };
}

function load(withDom) {
  const clock = { now: 0, timers: [] };
  const elements = {
    coNeedsHud: fakeElement(),
    coNeedsHunger: fakeElement(),
    coNeedsThirst: fakeElement(),
    coNeedsSleep: fakeElement()
  };
  const window = {};
  const document = {
    getElementById(id) {
      return withDom ? elements[id] || null : null;
    }
  };
  const sandbox = {
    window,
    document,
    Date: { now: () => clock.now },
    setTimeout(callback, delay) {
      clock.timers.push({ callback, due: clock.now + delay });
      return clock.timers.length;
    },
    clearTimeout(handle) {
      const timer = clock.timers[handle - 1];
      if (timer) {
        timer.cleared = true;
      }
    }
  };
  vm.runInNewContext(source, sandbox);
  return { window, elements, clock };
}

function advance(clock, ms) {
  clock.now += ms;
  let due;
  while ((due = clock.timers.find((timer) => !timer.done && !timer.cleared && timer.due <= clock.now))) {
    due.done = true;
    due.callback();
  }
}

const plain = (value) => JSON.parse(JSON.stringify(value));
const closeTo = (left, right) => Math.abs(left - right) < 1e-9;
const { window: bare } = load(false);
const model = bare.coNeedsHudModel;

{
  assert.equal(model.satisfactionOf(0), 1, "no need is a full bar");
  assert.equal(model.satisfactionOf(1), 0, "a maxed need is an empty bar");
  assert.equal(model.satisfactionOf(0.25), 0.75, "satisfaction is one minus the need");
}

{
  const table = [
    [0, "ok"],
    [0.49, "ok"],
    [0.5, "dim"],
    [0.74, "dim"],
    [0.75, "warn"],
    [0.89, "warn"],
    [0.9, "crit"],
    [1, "crit"]
  ];
  for (const [need, tone] of table) {
    assert.equal(model.colorStateOf(need), tone, `need ${need} gives tone ${tone}`);
  }
}

{
  assert.equal(model.uiScaleFor(1920, 1080), 1, "1080p keeps the baseline widget scale");
  assert.equal(model.uiScaleFor(1280, 720), 1, "smaller viewports never shrink below the baseline");
  assert.ok(closeTo(model.uiScaleFor(2560, 1440), 4 / 3), "1440p grows the widget with the vanilla HUD");
  assert.ok(closeTo(model.uiScaleFor(2741, 1227), Math.min(2741 / 1920, 1227 / 1080)), "the owner's viewport scales by its smaller axis");
  assert.equal(model.uiScaleFor(2560, 1080), 1, "an ultrawide viewport is limited by its height");
  assert.equal(model.uiScaleFor(3840, 2160), 2, "4K caps at twice the baseline");
  assert.equal(model.uiScaleFor(0, 0), 1, "missing dimensions fall back to the baseline");
}

{
  const good = model.parseState('{"enabled":true,"hunger":0.2,"thirst":0,"sleep":1}');
  assert.deepEqual(plain(good), { enabled: true, needs: { hunger: 0.2, thirst: 0, sleep: 1 } });
  assert.equal(model.parseState('{"enabled":false,"hunger":0,"thirst":0,"sleep":0}').enabled, false, "a disabled state parses so the HUD can hide");
  const malformed = [
    undefined,
    null,
    "",
    "{",
    "null",
    "5",
    "[]",
    '"enabled"',
    '{"hunger":0.1,"thirst":0.1,"sleep":0.1}',
    '{"enabled":"yes","hunger":0,"thirst":0,"sleep":0}',
    '{"enabled":true,"hunger":1.5,"thirst":0,"sleep":0}',
    '{"enabled":true,"hunger":-0.1,"thirst":0,"sleep":0}',
    '{"enabled":true,"hunger":"0.5","thirst":0,"sleep":0}',
    '{"enabled":true,"hunger":0.1,"thirst":0.1}'
  ];
  for (const json of malformed) {
    assert.equal(model.parseState(json), null, `malformed ${JSON.stringify(json)} is ignored`);
  }
}

{
  const calm = { enabled: true, needs: { hunger: 0.2, thirst: 0.2, sleep: 0.2 } };
  const first = model.nextFade(null, calm, 1000);
  assert.equal(first.opacity, 1, "the first state shows at full opacity");
  assert.equal(first.fullUntil, 1000 + model.FULL_MS, "the first state holds full for FULL_MS");
  assert.equal(model.FULL_MS, 4000);
  const settled = model.nextFade(first, calm, 1000 + model.FULL_MS);
  assert.equal(settled.opacity, 0.8, "all needs under 0.5 settle to the idle opacity");
  assert.equal(model.IDLE_OPACITY, 0.8);
  assert.ok(model.IDLE_OPACITY >= 0.75, "the idle block stays legible instead of reading as transparent");
  assert.equal(settled.full, false);

  const bumped = { enabled: true, needs: { hunger: 0.22, thirst: 0.2, sleep: 0.2 } };
  const woken = model.nextFade(settled, bumped, 9000);
  assert.equal(woken.opacity, 1, "a change of 0.02 returns the block to full opacity");
  assert.equal(woken.fullUntil, 9000 + 4000, "the wake lasts FULL_MS from the change");

  const small = { enabled: true, needs: { hunger: 0.21, thirst: 0.2, sleep: 0.2 } };
  assert.equal(model.nextFade(settled, small, 9000).opacity, 0.8, "a change under 0.02 that crosses no threshold does not wake the block");
  const near = { enabled: true, needs: { hunger: 0.219999, thirst: 0.2, sleep: 0.2 } };
  assert.equal(model.nextFade(settled, near, 9000).opacity, 0.8, "a change just below 0.02 remains idle");

  const under = { enabled: true, needs: { hunger: 0.49, thirst: 0.2, sleep: 0.2 } };
  const idle = model.nextFade(model.nextFade(null, under, 0), under, 10000);
  assert.equal(idle.opacity, 0.8, "0.49 is still calm");
  const crossed = model.nextFade(idle, { enabled: true, needs: { hunger: 0.5, thirst: 0.2, sleep: 0.2 } }, 10000);
  assert.equal(crossed.fullUntil, 14000, "crossing 0.5 counts as a change even with a 0.01 move");

  const high = model.nextFade(idle, { enabled: true, needs: { hunger: 0.6, thirst: 0.2, sleep: 0.2 } }, 50000);
  assert.equal(high.opacity, 1, "any need at 0.5 or more keeps the block at full opacity");
  assert.equal(model.nextFade(high, { enabled: true, needs: { hunger: 0.6, thirst: 0.2, sleep: 0.2 } }, 900000).opacity, 1, "a high need stays full with no change");
}

{
  const { window: live, elements } = load(true);
  live.coNeedsSetState('{"enabled":true,"hunger":0.8,"thirst":0.1,"sleep":0.6}');
  assert.equal(elements.coNeedsHud.hidden, false, "an enabled state shows the block");
  assert.equal(elements.coNeedsHunger.attributes["data-tone"], "warn", "hunger at 0.8 is amber");
  assert.equal(elements.coNeedsThirst.attributes["data-tone"], "ok", "thirst at 0.1 is green");
  assert.equal(elements.coNeedsSleep.attributes["data-tone"], "dim", "sleep at 0.6 is the subtle dim");
  assert.ok(Math.abs(Number(elements.coNeedsHunger.style["--co-needs-satisfaction"]) - 0.2) < 1e-9, "hunger at 0.8 leaves a 0.2 bar");
  assert.equal(elements.coNeedsHud.style.opacity, "1", "a high need keeps the block at full opacity");
  live.coNeedsSetState("not json");
  assert.equal(elements.coNeedsHud.hidden, true, "malformed JSON hides the block");
  live.coNeedsSetState('{"enabled":true,"hunger":2,"thirst":0,"sleep":0}');
  assert.equal(elements.coNeedsHud.hidden, true, "an out-of-range need hides the block");
  live.coNeedsSetState('{"enabled":false,"hunger":0,"thirst":0,"sleep":0}');
  assert.equal(elements.coNeedsHud.hidden, true, "a disabled state hides the block");
}

{
  const { window: live, elements, clock } = load(true);
  clock.now = 1000;
  live.coNeedsSetState('{"enabled":true,"hunger":0.2,"thirst":0.2,"sleep":0.2}');
  assert.equal(elements.coNeedsHud.style.opacity, "1", "a first state shows at full opacity");
  advance(clock, 4000);
  assert.equal(elements.coNeedsHud.style.opacity, "0.8", "after FULL_MS a calm block drops to the idle opacity");
  live.coNeedsSetState('{"enabled":true,"hunger":0.2,"thirst":0.2,"sleep":0.2}');
  assert.equal(elements.coNeedsHud.style.opacity, "0.8", "a repeated identical state does not wake the block");
  live.coNeedsSetState('{"enabled":true,"hunger":0.22,"thirst":0.2,"sleep":0.2}');
  assert.equal(elements.coNeedsHud.style.opacity, "1", "a change of 0.02 wakes the block");
}

{
  const markup = fs.readFileSync(__dirname + "/index.html", "utf8");
  const preview = fs.readFileSync(__dirname + "/preview.html", "utf8");
  const ids = ["coNeedsHud", "coNeedsHunger", "coNeedsThirst", "coNeedsSleep"];
  for (const id of ids) {
    assert.ok(markup.includes(`id="${id}"`), `index.html has #${id}, which needs-hud.js paints`);
    assert.ok(source.includes(`"${id}"`), `needs-hud.js looks up #${id}`);
  }
  assert.ok(markup.includes('id="coNeedsHud" hidden'), "the block starts hidden");
  for (const label of ["Hunger", "Thirst", "Sleep"]) {
    assert.ok(markup.includes(`aria-label="${label}"`), `the ${label} row is labelled`);
  }
  assert.ok(markup.includes('<script src="needs-hud.js"></script>'));
  assert.ok(preview.includes('<script src="needs-hud.js"></script>'));
  assert.ok(preview.includes("window.coNeedsSetState("), "preview drives the HUD through the entry point");
}

{
  const root = /:root\s*\{[^}]*\}/.exec(css);
  assert.ok(root, "styles.css has a :root token block");
  for (const token of ["--co-green:", "--co-amber:", "--co-danger:", "--co-green-dim:"]) {
    assert.ok(root[0].includes(token), `the token block defines ${token}`);
  }
  assert.equal(/#[0-9a-fA-F]{3,8}\b/.test(css.replace(root[0], "")), false, "colours outside :root come from tokens only");
  assert.match(css, /@media \(prefers-reduced-motion: reduce\)\s*\{[\s\S]*?transition:\s*none;/, "reduced motion turns transitions off");
  assert.ok(css.includes("var(--co-needs-satisfaction"), "the bar width comes from the satisfaction variable");
  assert.ok(css.includes("var(--co-needs-left)") && css.includes("var(--co-needs-bottom)"), "the block position comes from variables");
  for (const token of ["--co-needs-track-width:", "--co-needs-track-height:", "--co-needs-icon-size:", "--co-needs-row-gap:", "--co-needs-icon-gap:"]) {
    assert.ok(root[0].includes(token), `the token block defines ${token}`);
    assert.ok(css.includes(`var(${token.slice(0, -1)})`), `${token.slice(0, -1)} drives the widget size`);
  }
}

console.log("Needs HUD regression passed");
