(function () {
  var CHAR_WIDTH_EM = 0.62;
  var LINE_HEIGHT_EM = 1.2;
  var PAD_X_PX = 10;
  var PAD_Y_PX = 4;
  var GAP_PX = 6;
  var ZOOM_SHOW_T = 0.5;

  function isFiniteNumber(value) {
    return typeof value === "number" && isFinite(value);
  }

  function labelBox(item, fontPx, uiScale) {
    var size = isFiniteNumber(fontPx) && fontPx > 0 ? fontPx : 13;
    var scale = isFiniteNumber(uiScale) && uiScale > 0 ? uiScale : 1;
    var text = typeof item.text === "string" ? item.text : "";
    var width = text.length * size * CHAR_WIDTH_EM + PAD_X_PX * 2 * scale;
    var height = size * LINE_HEIGHT_EM + PAD_Y_PX * 2 * scale;
    var bottom = item.y - GAP_PX * scale;
    return { left: item.x - width / 2, right: item.x + width / 2, top: bottom - height, bottom: bottom };
  }

  function overlaps(a, b, gap) {
    return a.left < b.right + gap && b.left < a.right + gap && a.top < b.bottom + gap && b.top < a.bottom + gap;
  }

  function comparePriority(a, b) {
    if (a.priority !== b.priority) {
      return b.priority - a.priority;
    }
    return a.id < b.id ? -1 : a.id > b.id ? 1 : 0;
  }

  function layoutLabels(items, options) {
    var opts = options || {};
    var gap = isFiniteNumber(opts.gap) ? opts.gap : 4;
    var fontPx = isFiniteNumber(opts.fontPx) ? opts.fontPx : 13;
    var uiScale = isFiniteNumber(opts.uiScale) ? opts.uiScale : 1;
    var list = Array.isArray(items) ? items.filter(function (item) {
      return item && typeof item.id === "string" && item.id !== "" && typeof item.text === "string" && item.text !== "" &&
        isFiniteNumber(item.x) && isFiniteNumber(item.y);
    }) : [];
    list.sort(comparePriority);
    var accepted = [];
    var shown = Object.create(null);
    for (var i = 0; i < list.length; i++) {
      var box = labelBox(list[i], fontPx, uiScale);
      var clash = false;
      for (var j = 0; j < accepted.length; j++) {
        if (overlaps(box, accepted[j], gap)) {
          clash = true;
          break;
        }
      }
      if (!clash) {
        accepted.push(box);
        shown[list[i].id] = true;
      }
    }
    return shown;
  }

  function labelsVisibleAtZoom(zoomT) {
    return isFiniteNumber(zoomT) && zoomT >= ZOOM_SHOW_T;
  }

  // Constraint: pan and zoom events coalesce into one culling pass per scheduled frame.
  function createFrameCoalescer(schedule, run) {
    var pending = false;
    return function request() {
      if (pending) {
        return;
      }
      pending = true;
      schedule(function () {
        pending = false;
        run();
      });
    };
  }

  var api = {
    ZOOM_SHOW_T: ZOOM_SHOW_T,
    labelBox: labelBox,
    overlaps: overlaps,
    layoutLabels: layoutLabels,
    labelsVisibleAtZoom: labelsVisibleAtZoom,
    createFrameCoalescer: createFrameCoalescer
  };

  if (typeof window !== "undefined") {
    window.COMapLabels = api;
  }
  if (typeof module !== "undefined" && module.exports) {
    module.exports = api;
  }
})();
