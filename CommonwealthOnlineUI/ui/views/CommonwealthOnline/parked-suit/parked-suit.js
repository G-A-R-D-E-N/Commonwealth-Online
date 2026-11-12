(function () {
  "use strict";

  var CODES = ["1C", "55", "BD", "E9", "7A", "FF", "04", "C3"];
  var GRID = 6;
  var DEFAULT_TARGET = 3;
  var MIN_TARGET = 2;
  var MAX_TARGET = 4;
  var BREACH_MS = 1100;
  var AUTHORISE_MS = 6000;
  var LINK_MS = 6000;
  var OPEN_MS = 600;
  var FLIP_MS = 500;
  var HOLD_MS = 1600;
  var DENIED_MS = 1400;
  var TIMEOUT_HOLD_MS = 1800;
  var PHASE_TEXT = {
    breaching: "BREACHING",
    authorising: "AUTHORISING...",
    opening: "UNLOCKED",
    flipping: "UNLOCKED",
    transferred: "UNLOCKED",
    owned: "ALREADY YOURS",
    timeout: "NO RESPONSE FROM SUIT, TRY AGAIN",
    denied: "ACCESS DENIED"
  };

  var suits = [];
  var selected = 0;
  var open = false;
  var mode = "list";
  var status = "";
  var breach = null;
  var pending = null;
  var linkSuit = null;
  var timer = 0;
  var wipeTimer = 0;
  var lastSeen = {};
  var rowFx = {};
  var seed = 0x20112026;
  var reduceMotion = !!(window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches);

  var root = document.getElementById("coParkedSuit");
  var eyebrow = document.getElementById("coParkedSuitEyebrow");
  var list = document.getElementById("coParkedSuitList");
  var breachView = document.getElementById("coParkedSuitBreachView");
  var gridEl = document.getElementById("coParkedSuitGrid");
  var targetEl = document.getElementById("coParkedSuitTarget");
  var bufferEl = document.getElementById("coParkedSuitBuffer");
  var hack = document.getElementById("coParkedSuitHack");
  var close = document.getElementById("coParkedSuitClose");
  var removeSuit = document.getElementById("coParkedSuitRemove");
  var statusLine = document.getElementById("coParkedSuitStatus");
  var hint = document.getElementById("coParkedSuitHint");
  var hintBreach = document.getElementById("coParkedSuitHintBreach");
  var hintTransfer = document.getElementById("coParkedSuitHintTransfer");
  var transfer = document.getElementById("coParkedSuitTransfer");
  var transferLock = document.getElementById("coParkedSuitTransferLock");
  var transferState = document.getElementById("coParkedSuitTransferState");
  var transferOwner = document.getElementById("coParkedSuitTransferOwner");
  var transferLine = document.getElementById("coParkedSuitTransferLine");

  function rand(bound) {
    seed = (Math.imul(seed, 1103515245) + 12345) >>> 0;
    return (seed >>> 16) % bound;
  }

  function clampTarget(value) {
    var length = Number.isInteger(value) ? value : DEFAULT_TARGET;
    return Math.max(MIN_TARGET, Math.min(MAX_TARGET, length));
  }

  function bufferSize() {
    return breach ? breach.length + 1 : MAX_TARGET + 1;
  }

  function createBreach(suitId, challengeSeed, targetLength) {
    seed = challengeSeed >>> 0;
    var length = clampTarget(targetLength);
    var cells = [];
    for (var r = 0; r < GRID; r += 1) {
      var row = [];
      for (var c = 0; c < GRID; c += 1) row.push(CODES[rand(CODES.length)]);
      cells.push(row);
    }
    var path = [[0, rand(GRID)]];
    while (path.length < length) {
      var last = path[path.length - 1];
      if (path.length % 2 === 1) {
        var stepRow = rand(GRID - 1);
        if (stepRow >= last[0]) stepRow += 1;
        path.push([stepRow, last[1]]);
      } else {
        var stepCol = rand(GRID - 1);
        if (stepCol >= last[1]) stepCol += 1;
        path.push([last[0], stepCol]);
      }
    }
    var target = path.map(function (point) { return cells[point[0]][point[1]]; });
    return {
      suitId: suitId,
      length: length,
      cells: cells,
      target: target,
      used: [],
      picks: [],
      phase: "row",
      line: 0,
      cursor: 0
    };
  }

  function isUsed(r, c) {
    return breach.used.indexOf(r + ":" + c) >= 0;
  }

  function candidates() {
    var out = [];
    if (!breach) return out;
    if (breach.phase === "row") {
      for (var c = 0; c < GRID; c += 1) if (!isUsed(breach.line, c)) out.push([breach.line, c]);
    } else {
      for (var r = 0; r < GRID; r += 1) if (!isUsed(r, breach.line)) out.push([r, breach.line]);
    }
    return out;
  }

  function matchedCount() {
    var matched = 0;
    for (var i = 0; i < breach.picks.length && matched < breach.target.length; i += 1) {
      if (breach.picks[i].code === breach.target[matched]) matched += 1;
    }
    return matched;
  }

  function padlock(className) {
    var wrap = document.createElement("span");
    wrap.innerHTML = '<svg class="pl ' + className + '" viewBox="0 0 24 24" aria-hidden="true">' +
      '<path class="pl-shackle" d="M7 10V7a5 5 0 0 1 10 0v3"></path>' +
      '<rect class="pl-body" x="4" y="10" width="16" height="12" rx="2"></rect>' +
      '<circle class="pl-hole" cx="12" cy="15.5" r="1.7"></circle>' +
      '<rect class="pl-hole" x="11.2" y="15.5" width="1.6" height="3.6"></rect>' +
      "</svg>";
    return wrap.firstChild;
  }

  function ownerLabel(suit) {
    return suit.mine ? "You" : suit.owner || "Owner unknown";
  }

  function findSuit(suitId) {
    for (var i = 0; i < suits.length; i += 1) {
      if (suits[i].suitId === suitId) return suits[i];
    }
    return null;
  }

  function clearTimers() {
    clearTimeout(timer);
    clearTimeout(wipeTimer);
  }

  var seenTarget = null;

  function reset() {
    clearTimers();
    pending = null;
    linkSuit = null;
    mode = "list";
    breach = null;
    status = "";
  }

  function send(op, suitId, picks) {
    if (!open) return;
    if (op === "close") {
      reset();
      window.coParkedSuitAction(JSON.stringify({ op: "close" }));
      return;
    }
    var payload = { op: op, suitId: suitId };
    if (picks) payload.picks = picks;
    window.coParkedSuitAction(JSON.stringify(payload));
  }

  function back() {
    reset();
    window.coParkedSuitAction(JSON.stringify({ op: "clearTarget" }));
    render();
  }

  function beginLink(suitId, message) {
    mode = "link";
    linkSuit = suitId;
    status = message;
    send("breachStart", suitId);
    clearTimeout(timer);
    timer = setTimeout(linkTimeout, LINK_MS);
    render();
  }

  function linkTimeout() {
    linkSuit = null;
    mode = "list";
    status = "NO RESPONSE FROM SUIT";
    render();
  }

  function removeSelected() {
    var suit = suits[selected];
    if (!suit || !suit.mine) return;
    send("remove", suit.suitId);
  }

  function startBreach() {
    var suit = suits[selected];
    if (!suit || mode === "link") return;
    beginLink(suit.suitId, "ESTABLISHING LINK");
  }

  function showPhase(phase) {
    pending.phase = phase;
    transfer.className = "parked-suit-transfer is-" + phase;
    transferState.textContent = PHASE_TEXT[phase];
    transferLine.textContent = "";
  }

  function wipeOwner(from, to, done) {
    if (reduceMotion) {
      transferOwner.textContent = to;
      done();
      return;
    }
    var length = Math.max(from.length, to.length);
    var step = 0;
    function tick() {
      step += 1;
      transferOwner.textContent = to.slice(0, step) + (step < from.length ? from.slice(step) : "");
      if (step < length) {
        wipeTimer = setTimeout(tick, FLIP_MS / length);
      } else {
        done();
      }
    }
    wipeTimer = setTimeout(tick, FLIP_MS / length);
  }

  function startAuthorise() {
    var suit = findSuit(breach.suitId);
    var picks = breach.picks.map(function (entry) { return entry.at; });
    pending = {
      suitId: breach.suitId,
      owner: suit ? ownerLabel(suit) : "Owner unknown",
      before: { mine: !!(suit && suit.mine), outcome: "" },
      outcome: "",
      phase: ""
    };
    breach = null;
    mode = "transfer";
    status = "";
    send("hack", pending.suitId, picks);
    transferOwner.textContent = pending.owner;
    render();
    showPhase("breaching");
    timer = setTimeout(afterBreach, BREACH_MS);
  }

  function afterBreach() {
    if (pending.outcome) {
      reveal();
      return;
    }
    showPhase("authorising");
    timer = setTimeout(noResponse, AUTHORISE_MS);
  }

  function noResponse() {
    showPhase("timeout");
    transferOwner.textContent = pending.owner;
    timer = setTimeout(back, TIMEOUT_HOLD_MS);
  }

  function reveal() {
    if (pending.outcome === "owned") {
      showPhase("owned");
      transferOwner.textContent = "You";
      timer = setTimeout(back, HOLD_MS);
      return;
    }
    showPhase("opening");
    timer = setTimeout(flip, OPEN_MS);
  }

  function flip() {
    showPhase("flipping");
    wipeOwner(pending.owner, "You", function () {
      showPhase("transferred");
      transferOwner.textContent = "You";
      transferLine.textContent = "OWNERSHIP TRANSFERRED";
      timer = setTimeout(back, HOLD_MS);
    });
  }

  function startDenied() {
    var failedSuit = breach.suitId;
    pending = { suitId: failedSuit, owner: "", before: null, outcome: "", phase: "" };
    breach = null;
    mode = "transfer";
    status = "";
    send("breachFailed", failedSuit);
    transferOwner.textContent = "";
    render();
    showPhase("denied");
    timer = setTimeout(resetGrid, DENIED_MS);
  }

  function resetGrid() {
    var suitId = pending.suitId;
    pending = null;
    beginLink(suitId, "Link failed, establishing a new link");
  }

  function pick(r, c) {
    if (!breach || isUsed(r, c)) return;
    breach.used.push(r + ":" + c);
    breach.picks.push({ code: breach.cells[r][c], at: [r, c] });
    if (matchedCount() >= breach.target.length) {
      startAuthorise();
      return;
    }
    if (breach.picks.length >= bufferSize()) {
      startDenied();
      return;
    }
    if (breach.phase === "row") {
      breach.phase = "col";
      breach.line = c;
    } else {
      breach.phase = "row";
      breach.line = r;
    }
    breach.cursor = 0;
    status = "";
    render();
  }

  function moveCursor(step) {
    var live = candidates();
    if (!live.length) return;
    breach.cursor = (breach.cursor + step + live.length) % live.length;
    render();
  }

  function pickCursor() {
    var live = candidates();
    var point = live[Math.min(breach.cursor, live.length - 1)];
    if (point) pick(point[0], point[1]);
  }

  function trackSuit(suit) {
    var prev = lastSeen[suit.suitId];
    var outcome = typeof suit.outcome === "string" ? suit.outcome : "";
    if (pending && pending.suitId === suit.suitId && pending.outcome === "" && (pending.phase === "breaching" || pending.phase === "authorising")) {
      if ((outcome && outcome !== pending.before.outcome) || (suit.mine && !pending.before.mine)) {
        pending.outcome = outcome === "owned" ? "owned" : "broken";
        if (pending.phase === "authorising") {
          clearTimeout(timer);
          reveal();
        }
      }
    }
    var panelOwns = pending && pending.suitId === suit.suitId && pending.phase !== "timeout" && pending.phase !== "denied";
    if (prev && open && !panelOwns) {
      if (!prev.mine && suit.mine) {
        rowFx[suit.suitId] = "unlocking";
      } else if (!prev.locked && suit.locked) {
        rowFx[suit.suitId] = "locking";
      }
    }
    lastSeen[suit.suitId] = { mine: !!suit.mine, locked: !!suit.locked };
  }

  function renderList() {
    list.textContent = "";
    if (!suits.length) {
      var empty = document.createElement("li");
      empty.className = "parked-suit-empty";
      empty.textContent = "No parked power armor in reach.";
      list.appendChild(empty);
      return;
    }
    suits.forEach(function (suit, index) {
      var item = document.createElement("li");
      item.className = "parked-suit-row";
      if (rowFx[suit.suitId]) {
        item.classList.add("is-" + rowFx[suit.suitId]);
        delete rowFx[suit.suitId];
      }
      item.setAttribute("role", "option");
      item.setAttribute("aria-selected", index === selected ? "true" : "false");
      item.tabIndex = -1;
      var lockState = "";
      if (suit.mine) {
        lockState = "is-mine is-open";
      } else if (!suit.locked) {
        lockState = "is-free is-open";
      }
      item.appendChild(padlock("parked-suit-row__lock " + lockState));
      var id = document.createElement("span");
      id.className = "parked-suit-row__id";
      id.textContent = suit.suitId;
      var meta = document.createElement("span");
      meta.className = "parked-suit-row__meta";
      var owner = document.createElement("span");
      owner.className = suit.result ? "parked-suit-row__owner is-result" : "parked-suit-row__owner";
      owner.textContent = suit.result || ownerLabel(suit);
      var where = document.createElement("span");
      where.className = "parked-suit-row__where";
      where.textContent = suit.location || "";
      meta.appendChild(owner);
      meta.appendChild(where);
      item.appendChild(id);
      item.appendChild(meta);
      item.addEventListener("click", function () { selected = index; render(); });
      item.addEventListener("dblclick", function () { selected = index; startBreach(); });
      list.appendChild(item);
    });
  }

  function renderBreach() {
    var live = candidates();
    breach.cursor = live.length ? Math.min(breach.cursor, live.length - 1) : 0;
    var cursorPoint = live[breach.cursor];

    targetEl.textContent = "";
    var matched = matchedCount();
    breach.target.forEach(function (code, index) {
      var item = document.createElement("li");
      item.textContent = code;
      if (index < matched) item.className = "is-matched";
      targetEl.appendChild(item);
    });

    bufferEl.textContent = "";
    for (var slot = 0; slot < bufferSize(); slot += 1) {
      var cell = document.createElement("span");
      var pickEntry = breach.picks[slot];
      cell.textContent = pickEntry ? pickEntry.code : "--";
      if (!pickEntry) cell.className = "is-empty";
      bufferEl.appendChild(cell);
    }

    gridEl.textContent = "";
    for (var r = 0; r < GRID; r += 1) {
      for (var c = 0; c < GRID; c += 1) {
        var button = document.createElement("button");
        button.type = "button";
        button.className = "parked-suit-code";
        button.textContent = breach.cells[r][c];
        button.setAttribute("role", "gridcell");
        if (isUsed(r, c)) {
          button.className += " is-used";
        } else if (live.some(function (point) { return point[0] === r && point[1] === c; })) {
          button.className += " is-live";
          if (cursorPoint && cursorPoint[0] === r && cursorPoint[1] === c) button.className += " is-cursor";
          button.addEventListener("click", (function (row, col) {
            return function () { pick(row, col); };
          }(r, c)));
        } else {
          button.disabled = true;
        }
        gridEl.appendChild(button);
      }
    }
  }

  function render() {
    root.hidden = !open;
    if (!open) return;
    var inBreach = mode === "breach" && !!breach;
    var inTransfer = mode === "transfer" && !!pending;
    eyebrow.textContent = inBreach || inTransfer ? "BREACH PROTOCOL" : "PLAYER INTERACTION";
    list.hidden = inBreach || inTransfer;
    breachView.hidden = !inBreach;
    transfer.hidden = !inTransfer;
    hack.hidden = inBreach || inTransfer;
    hack.disabled = !suits.length;
    close.hidden = inBreach || inTransfer;
    removeSuit.hidden = inBreach || inTransfer || !(suits[selected] && suits[selected].mine);
    hint.hidden = inBreach || inTransfer;
    hintBreach.hidden = !inBreach;
    hintTransfer.hidden = !inTransfer;
    statusLine.textContent = status;
    if (inBreach) {
      renderBreach();
      return;
    }
    if (!inTransfer) renderList();
  }

  window.coParkedSuitSetState = function (state) {
    if (!state || typeof state !== "object") return;
    var wasOpen = open;
    open = state.open !== false;
    var incoming = Array.isArray(state.suits) ? state.suits : [];
    suits = incoming.filter(function (suit) {
      return suit && typeof suit.suitId === "string" && suit.suitId.length > 0;
    });
    if (!open || !wasOpen) reset();
    suits.forEach(trackSuit);
    if (mode === "link") {
      var linked = findSuit(linkSuit);
      if (linked && linked.challenge && linked.challenge.suitId === linkSuit) {
        clearTimeout(timer);
        breach = createBreach(linkSuit, linked.challenge.seed, linked.challenge.length);
        linkSuit = null;
        mode = "breach";
        status = "";
      }
    }
    if (Number.isInteger(state.selection)) selected = state.selection;
    selected = suits.length ? Math.max(0, Math.min(suits.length - 1, selected)) : 0;
    if (open && typeof state.target === "string" && state.targetSequence !== seenTarget) {
      seenTarget = state.targetSequence;
      if (mode === "list" && findSuit(state.target)) {
        beginLink(state.target, "ESTABLISHING LINK");
        return;
      }
    }
    if (typeof state.status === "string" && state.status) status = state.status;
    render();
    if (open && mode === "list") (suits.length ? list : hack).focus();
  };

  window.coParkedSuitController = function (button, buttonPhase) {
    if (buttonPhase !== "down" || !open) return;
    if (mode === "transfer") {
      if (button === "b") back();
      return;
    }
    if (mode === "breach") {
      if (button === "b") { back(); return; }
      if (button === "a") { pickCursor(); return; }
      if (button === "dpad_up" || button === "dpad_left") { moveCursor(-1); return; }
      if (button === "dpad_down" || button === "dpad_right") { moveCursor(1); }
      return;
    }
    if (button === "b") { send("close"); return; }
    if (button === "a") { startBreach(); return; }
    if (button === "dpad_up" && suits.length) { selected = (selected + suits.length - 1) % suits.length; render(); return; }
    if (button === "dpad_down" && suits.length) { selected = (selected + 1) % suits.length; render(); }
  };

  hack.addEventListener("click", function () { startBreach(); });
  close.addEventListener("click", function () { send("close"); });
  removeSuit.addEventListener("click", function () { removeSelected(); });
  transferLock.appendChild(padlock(""));
  window.addEventListener("keydown", function (event) {
    if (!open) return;
    if (mode === "transfer") {
      if (event.key === "Escape" || event.key === "Backspace") { event.preventDefault(); back(); }
      return;
    }
    if (mode === "breach") {
      if (event.key === "Escape" || event.key === "Backspace") { event.preventDefault(); back(); return; }
      if (event.key === "Enter") { event.preventDefault(); pickCursor(); return; }
      if (event.key === "ArrowUp" || event.key === "ArrowLeft") { event.preventDefault(); moveCursor(-1); return; }
      if (event.key === "ArrowDown" || event.key === "ArrowRight") { event.preventDefault(); moveCursor(1); }
      return;
    }
    if (event.key === "Escape") { event.preventDefault(); send("close"); return; }
    if (event.key === "Enter") { event.preventDefault(); startBreach(); return; }
    if (event.key === "ArrowUp" && suits.length) { event.preventDefault(); selected = (selected + suits.length - 1) % suits.length; render(); return; }
    if (event.key === "ArrowDown" && suits.length) { event.preventDefault(); selected = (selected + 1) % suits.length; render(); }
  });
  render();
}());
