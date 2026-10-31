(function () {
  "use strict";

  var HEADERS = [
    "Differences from this server:",
    "Client files were not compared on this machine.",
    "Extra F4SE plugins (allowed, not required):"
  ];
  var WARNING_HEADER_INDEX = 2;
  var DEFAULT_HEADLINE = "Server modpack does not match.";

  var LABELS = {
    "Missing": "co-modpack-detail-entry--missing",
    "Unexpected": "co-modpack-detail-entry--unexpected",
    "Wrong load order": "co-modpack-detail-entry--order",
    "Different content": "co-modpack-detail-entry--content",
    "Different native plugin": "co-modpack-detail-entry--content",
    "Different config": "co-modpack-detail-entry--content",
    "Different archive": "co-modpack-detail-entry--content"
  };

  function headerIndex(line) {
    for (var i = 0; i < HEADERS.length; i++) {
      if (line === HEADERS[i]) {
        return i;
      }
    }
    return -1;
  }

  function split(text) {
    var lines = String(text == null ? "" : text).split("\n");
    var firstHeader = -1;
    for (var i = 0; i < lines.length; i++) {
      if (headerIndex(lines[i].trim()) >= 0) {
        firstHeader = i;
        break;
      }
    }
    if (firstHeader < 0) {
      return { headline: lines.join("\n").trim(), lines: [], warnings: [] };
    }
    var headline = lines.slice(0, firstHeader).join(" ").trim();
    var entries = [];
    var warnings = [];
    var current = entries;
    for (var j = firstHeader; j < lines.length; j++) {
      var trimmed = lines[j].trim();
      var header = headerIndex(trimmed);
      if (header >= 0) {
        current = header === WARNING_HEADER_INDEX ? warnings : entries;
        continue;
      }
      if (trimmed) {
        current.push(trimmed);
      }
    }
    return { headline: headline || DEFAULT_HEADLINE, lines: entries, warnings: warnings };
  }

  function splitLabel(line) {
    var index = line.indexOf(": ");
    if (index <= 0) {
      return null;
    }
    var label = line.slice(0, index);
    if (!LABELS[label]) {
      return null;
    }
    return { label: label, name: line.slice(index + 2), className: LABELS[label] };
  }

  function render(container, lines) {
    if (!container) {
      return;
    }
    var items = lines || [];
    var doc = container.ownerDocument || (typeof document !== "undefined" ? document : null);
    container.innerHTML = "";
    if (doc) {
      for (var i = 0; i < items.length; i++) {
        var item = doc.createElement("li");
        var parsed = splitLabel(items[i]);
        if (parsed) {
          item.className = "co-modpack-detail-entry " + parsed.className;
        }
        item.textContent = items[i];
        container.appendChild(item);
      }
    }
    container.classList.toggle("hidden", items.length === 0);
  }

  function show(scope, modpack) {
    if (!scope || !scope.querySelector) {
      return;
    }
    var warnings = modpack ? modpack.warnings : null;
    var warningsTitle = scope.querySelector("#coModpackWarningsTitle");
    render(scope.querySelector("#coModpackDetail"), modpack ? modpack.lines : null);
    render(scope.querySelector("#coModpackWarnings"), warnings);
    if (warningsTitle) {
      warningsTitle.classList.toggle("hidden", !(warnings && warnings.length));
    }
  }

  window.CO_ModpackDetail = {
    headers: HEADERS,
    labels: LABELS,
    headline: DEFAULT_HEADLINE,
    split: split,
    splitLabel: splitLabel,
    show: show
  };
})();
