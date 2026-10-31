(function () {
  "use strict";

  var MAX_MESSAGES = 100;
  var MAX_CODE_POINTS = 256;

  var REASON_TEXT = {
    invalidText: "That message contains characters that cannot be sent.",
    tooLong: "That message is longer than 256 characters.",
    rateLimited: "You are sending messages too quickly. Wait a moment.",
    channelUnavailable: "Global chat is not available right now.",
    muted: "You cannot send chat messages right now.",
    duplicate: "You just sent that message. Change it before sending again.",
    notInFaction: "You are not in a faction."
  };

  var state = initialState();
  var elements = null;
  var selectedChannel = "global";
  var windowOpen = false;

  function initialState() {
    return { enabled: false, localPlayerId: "", messages: [], rejected: "" };
  }

  function hasOwn(object, key) {
    return Object.prototype.hasOwnProperty.call(object, key);
  }

  function countCodePoints(text) {
    return Array.from(String(text == null ? "" : text)).length;
  }

  function counterText(text) {
    return countCodePoints(text) + "/" + MAX_CODE_POINTS;
  }

  function reasonText(reason) {
    return typeof reason === "string" && hasOwn(REASON_TEXT, reason)
      ? REASON_TEXT[reason]
      : "Your message was not sent.";
  }

  function colorFor(playerId) {
    var text = String(playerId == null ? "" : playerId);
    var hash = 2166136261;
    for (var i = 0; i < text.length; i += 1) {
      hash ^= text.charCodeAt(i);
      hash = Math.imul(hash, 16777619) >>> 0;
    }
    return "hsl(" + (hash % 360) + ", 70%, 72%)";
  }

  function isMessage(value) {
    return !!value && typeof value === "object" && !Array.isArray(value);
  }

  function reduce(current, event) {
    if (!isMessage(event)) {
      return current;
    }
    var next = {
      enabled: current.enabled,
      localPlayerId: current.localPlayerId,
      messages: current.messages,
      rejected: current.rejected
    };
    if (event.kind === "state") {
      next.enabled = event.enabled === true;
      next.localPlayerId = String(event.localPlayerId == null ? "" : event.localPlayerId);
      if (!next.enabled) {
        next.messages = [];
      }
    } else if (event.kind === "message" && isMessage(event.message)) {
      next.messages = current.messages.concat([event.message]).slice(-MAX_MESSAGES);
      if (String(event.message.fromPlayerId) === current.localPlayerId) {
        next.rejected = "";
      }
    } else if (event.kind === "history") {
      next.messages = (Array.isArray(event.messages) ? event.messages : [])
        .filter(isMessage)
        .slice(-MAX_MESSAGES);
    } else if (event.kind === "rejected") {
      next.rejected = reasonText(event.reason);
    }
    return next;
  }

  function buildLine(message) {
    var line = document.createElement("li");
    line.className = "co-chat-line";
    var name = document.createElement("span");
    name.className = "co-chat-name";
    name.style.color = colorFor(message.fromPlayerId);
    name.textContent = message.fromName == null ? "" : String(message.fromName);
    var text = document.createElement("span");
    text.className = "co-chat-text";
    text.textContent = message.text == null ? "" : String(message.text);
    if (message.channel === "faction") {
      var label = document.createElement("span");
      label.className = "co-chat-channel-tag";
      label.textContent = "[Faction]";
      line.appendChild(label);
    }
    line.appendChild(name);
    line.appendChild(text);
    return line;
  }

  function channelFor(selected) {
    return selected === "faction" ? "faction" : "global";
  }

  function sendAction(text, channel) {
    var clean = String(text == null ? "" : text).trim();
    if (!clean || countCodePoints(clean) > MAX_CODE_POINTS) {
      return null;
    }
    return { op: "send", channel: channel === "faction" ? "faction" : "global", text: clean };
  }

  function historyAction() {
    return { op: "history" };
  }

  function focusAction(focused) {
    return { op: "focus", focused: focused === true };
  }

  function closeAction() {
    return { op: "close" };
  }

  function send(action) {
    if (typeof window.coChatAction === "function") {
      window.coChatAction(JSON.stringify(action));
    }
  }

  function closeWindow() {
    send(focusAction(false));
    send(closeAction());
  }

  function paintList() {
    if (!elements) {
      return;
    }
    elements.list.textContent = "";
    state.messages.forEach(function (message) {
      elements.list.appendChild(buildLine(message));
    });
    elements.list.scrollTop = elements.list.scrollHeight;
  }

  function paintComposer() {
    if (!elements) {
      return;
    }
    var value = elements.input.value;
    elements.input.disabled = !state.enabled;
    elements.status.textContent = state.enabled ? state.rejected : "Chat is not available on this server";
    elements.count.textContent = counterText(value);
    elements.count.classList.toggle("is-over", countCodePoints(value) > MAX_CODE_POINTS);
    paintChannel();
  }

  function paintChannel() {
    var faction = channelFor(selectedChannel) === "faction";
    elements.channelGlobal.setAttribute("aria-selected", String(!faction));
    elements.channelFaction.setAttribute("aria-selected", String(faction));
    elements.channelGlobal.disabled = !state.enabled;
    elements.channelFaction.disabled = !state.enabled;
    elements.input.placeholder = faction ? "Message faction chat" : "Message global chat";
  }

  function selectChannel(channel) {
    selectedChannel = channel === "faction" ? "faction" : "global";
    paintComposer();
    if (elements && state.enabled && typeof elements.input.focus === "function") {
      elements.input.focus();
    }
  }

  function keepComposerFocus(event) {
    if (event && typeof event.preventDefault === "function") {
      event.preventDefault();
    }
  }

  function isWithin(node, target) {
    if (!node || !target) {
      return false;
    }
    if (target === node) {
      return true;
    }
    return typeof node.contains === "function" && node.contains(target);
  }

  function isInsideWindow(target) {
    return !!elements && isWithin(elements.root, target);
  }

  function keepPointerInChat(event) {
    if (!windowOpen || !elements || !event) {
      return;
    }
    const target = event.target;
    if (isWithin(elements.input, target) || isWithin(elements.close, target)) {
      return;
    }
    if (typeof event.preventDefault === "function") {
      event.preventDefault();
    }
    if (state.enabled && typeof elements.input.focus === "function") {
      elements.input.focus();
    }
  }

  function swallowOutsideClick(event) {
    if (!windowOpen || !elements || !event || isInsideWindow(event.target)) {
      return;
    }
    if (typeof event.preventDefault === "function") {
      event.preventDefault();
    }
    if (typeof event.stopPropagation === "function") {
      event.stopPropagation();
    }
  }

  function toggleChannel() {
    selectChannel(channelFor(selectedChannel) === "faction" ? "global" : "faction");
  }

  function submit() {
    if (!state.enabled) {
      return;
    }
    if (countCodePoints(elements.input.value) > MAX_CODE_POINTS) {
      state.rejected = reasonText("tooLong");
      paintComposer();
      return;
    }
    var action = sendAction(elements.input.value, channelFor(selectedChannel));
    if (!action) {
      return;
    }
    send(action);
    elements.input.value = "";
    paintComposer();
    closeWindow();
  }

  function openWindow() {
    if (!elements) {
      return;
    }
    elements.root.classList.remove("is-closed");
    windowOpen = true;
    if (state.enabled && state.messages.length === 0) {
      send(historyAction());
    }
    paintList();
    paintComposer();
    if (!elements.input.disabled) {
      elements.input.focus();
    }
  }

  function closeWindowLocal() {
    if (!elements) {
      return;
    }
    elements.root.classList.add("is-closed");
    windowOpen = false;
    if (typeof elements.input.blur === "function") {
      elements.input.blur();
    }
  }

  function keydown(event) {
    if (event.key === "Escape") {
      event.preventDefault();
      closeWindow();
      return;
    }
    if (event.key === "Tab") {
      event.preventDefault();
      toggleChannel();
      return;
    }
    if (event.key === "Enter") {
      event.preventDefault();
      submit();
    }
  }

  function init() {
    elements = {
      root: document.getElementById("coChat"),
      list: document.getElementById("coChatList"),
      status: document.getElementById("coChatStatus"),
      input: document.getElementById("coChatInput"),
      count: document.getElementById("coChatCount"),
      close: document.getElementById("coChatClose"),
      channelGlobal: document.getElementById("coChannelGlobal"),
      channelFaction: document.getElementById("coChannelFaction")
    };
    var complete = Object.keys(elements).every(function (key) {
      return !!elements[key];
    });
    if (!complete) {
      elements = null;
      return;
    }
    elements.channelGlobal.addEventListener("mousedown", keepComposerFocus);
    elements.channelFaction.addEventListener("mousedown", keepComposerFocus);
    elements.channelGlobal.addEventListener("click", function () {
      selectChannel("global");
    });
    elements.channelFaction.addEventListener("click", function () {
      selectChannel("faction");
    });
    elements.input.addEventListener("input", paintComposer);
    elements.input.addEventListener("focus", function () {
      send(focusAction(true));
    });
    elements.input.addEventListener("blur", function () {
      send(focusAction(false));
    });
    window.addEventListener("pagehide", function () {
      send(focusAction(false));
    });
    document.addEventListener("keydown", keydown);
    document.addEventListener("mousedown", keepPointerInChat, true);
    document.addEventListener("click", swallowOutsideClick, true);
    elements.close.addEventListener("click", closeWindow);
    paintList();
    paintComposer();
  }

  window.coChatEvent = function (event) {
    state = reduce(state, event);
    paintList();
    paintComposer();
  };
  window.coChatOpen = openWindow;
  window.coChatClose = closeWindowLocal;
  window.coChatModel = {
    MAX_MESSAGES: MAX_MESSAGES,
    MAX_CODE_POINTS: MAX_CODE_POINTS,
    initialState: initialState,
    countCodePoints: countCodePoints,
    counterText: counterText,
    reasonText: reasonText,
    colorFor: colorFor,
    buildLine: buildLine,
    reduce: reduce,
    sendAction: sendAction,
    historyAction: historyAction,
    focusAction: focusAction,
    closeAction: closeAction,
    channelFor: channelFor
  };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
