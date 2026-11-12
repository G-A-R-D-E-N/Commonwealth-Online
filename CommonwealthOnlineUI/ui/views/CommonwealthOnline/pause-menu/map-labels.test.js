const test = require("node:test");
const assert = require("node:assert/strict");
const labels = require("./map-labels.js");

test("labels that do not overlap are all shown", () => {
  const shown = labels.layoutLabels([
    { id: "a", text: "Sanctuary", x: 100, y: 200, priority: 1 },
    { id: "b", text: "Red Rocket", x: 400, y: 200, priority: 1 }
  ]);
  assert.equal(shown.a, true);
  assert.equal(shown.b, true);
});

test("the higher-priority label wins an overlap and the other is culled", () => {
  const shown = labels.layoutLabels([
    { id: "low", text: "Concord", x: 100, y: 200, priority: 1 },
    { id: "high", text: "Starlight Drive-In", x: 105, y: 200, priority: 3 }
  ]);
  assert.equal(shown.high, true);
  assert.equal(shown.low, undefined);
});

test("equal priority ties resolve by id so the layout is deterministic", () => {
  const items = [
    { id: "zeta", text: "Abernathy", x: 50, y: 50, priority: 2 },
    { id: "alpha", text: "Abernathy", x: 52, y: 50, priority: 2 }
  ];
  const first = labels.layoutLabels(items);
  const second = labels.layoutLabels(items.slice().reverse());
  assert.deepEqual(Object.keys(first), Object.keys(second));
  assert.equal(first.alpha, true);
  assert.equal(first.zeta, undefined);
});

test("malformed items and empty text are skipped", () => {
  const shown = labels.layoutLabels([
    null,
    { id: "", text: "x", x: 1, y: 1, priority: 1 },
    { id: "nolabel", text: "", x: 1, y: 1, priority: 1 },
    { id: "nan", text: "Bad", x: NaN, y: 1, priority: 1 },
    { id: "ok", text: "Good", x: 10, y: 10, priority: 0 }
  ]);
  assert.deepEqual(Object.keys(shown), ["ok"]);
});

test("label boxes grow with text length and sit above the marker", () => {
  const short = labels.labelBox({ text: "AB", x: 100, y: 100 }, 13, 1);
  const long = labels.labelBox({ text: "ABCDEFGHIJ", x: 100, y: 100 }, 13, 1);
  assert.ok(long.right - long.left > short.right - short.left);
  assert.ok(short.bottom < 100);
});

test("frame coalescer runs the culling pass once per scheduled frame", () => {
  const queued = [];
  let runs = 0;
  const request = labels.createFrameCoalescer((callback) => queued.push(callback), () => { runs++; });
  for (let i = 0; i < 25; i++) {
    request();
  }
  assert.equal(queued.length, 1, "many pan and zoom events share one scheduled frame");
  assert.equal(runs, 0, "nothing runs until the frame fires");
  queued.shift()();
  assert.equal(runs, 1);
  request();
  assert.equal(queued.length, 1, "a new event after the frame schedules a new frame");
  queued.shift()();
  assert.equal(runs, 2);
});

test("location labels appear only once the map is zoomed in enough", () => {
  assert.equal(labels.labelsVisibleAtZoom(0), false);
  assert.equal(labels.labelsVisibleAtZoom(labels.ZOOM_SHOW_T), true);
  assert.equal(labels.labelsVisibleAtZoom(NaN), false);
});
