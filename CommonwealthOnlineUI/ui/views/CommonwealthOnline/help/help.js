(function () {
  "use strict";

  var VOICE_KEYS = ["voice.pushToTalk", "voice.pushToTalkKey", "voice.whisperKey", "voice.shoutKey"];
  var values = {};
  var root = null;

  function esc(text) {
    return String(text == null ? "" : text)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function defaultFor(key) {
    var found;
    (window.CO_SettingsData || []).forEach(function (section) {
      (section.items || []).forEach(function (item) {
        if (item.key === key) {
          found = item.value;
        }
      });
    });
    return found;
  }

  function valueOf(key) {
    return Object.prototype.hasOwnProperty.call(values, key) ? values[key] : defaultFor(key);
  }

  function keyCap(name) {
    return '<span class="info-key">' + esc(name) + "</span>";
  }

  function voiceLines() {
    var pttOn = valueOf("voice.pushToTalk") !== false;
    var ptt = String(valueOf("voice.pushToTalkKey") || "");
    var lines = [];
    if (!pttOn) {
      lines.push("Push-to-talk is off, so your microphone is open. Turn it on in Settings.");
    } else if (ptt) {
      lines.push("Hold " + keyCap(ptt) + " to talk to other players.");
    } else {
      lines.push("Push-to-talk has no key bound. Bind one in Settings.");
    }
    [["voice.whisperKey", "whisper to nearby players"], ["voice.shoutKey", "shout to distant players"]].forEach(function (entry) {
      var key = String(valueOf(entry[0]) || "");
      if (key) {
        lines.push("Hold " + keyCap(key) + " while talking to " + entry[1] + ".");
      }
    });
    return lines;
  }

  function sections() {
    return [
      ["CHAT", ["Press " + keyCap("Enter") + " to open chat, then " + keyCap("Esc") + " to close it."]],
      ["VOICE", voiceLines()],
      ["DOWNED", [
        "When your health runs out you are downed. A teammate can revive you by holding Activate on you, which can cost a Stimpak.",
        "If nobody reaches you, choose to respawn from the menu that appears."
      ]],
      ["PAUSE MENU", [
        "Social has friends and factions, Quests shows your shared progress, Server shows the current connection, and Settings holds voice, map and network options."
      ]]
    ];
  }

  var SECTION_TAGS = {
    "CHAT": "01 :: TEXT TRANSMISSION",
    "VOICE": "02 :: RADIO FREQUENCIES",
    "DOWNED": "03 :: CASUALTY TRIAGE",
    "PAUSE MENU": "04 :: TERMINAL SUBSYSTEMS"
  };

  function renderHelp() {
    var body = sections().map(function (section) {
      var tag = SECTION_TAGS[section[0]] || "OPERATIONAL DIRECTIVE";
      var slug = section[0].toLowerCase().replace(/[^a-z0-9]+/g, "-");
      return '<section class="info-section info-section--' + slug + '">' +
        '<div class="info-section-header">' +
          '<span class="info-section-tag">' + tag + '</span>' +
          '<h2 class="info-heading">' + esc(section[0]) + '</h2>' +
        '</div>' +
        '<div class="info-section-body">' +
          section[1].map(function (line) {
            return '<p class="info-line">' + line + '</p>';
          }).join('') +
        '</div>' +
      '</section>';
    }).join("");

    return '<section class="info-panel info-panel--help">' +
      '<header class="info-head">' +
        '<div class="info-head-title-group">' +
          '<span class="info-sys-tag">CO-DOC · FIELD MANUAL</span>' +
          '<h1 class="info-title">HELP</h1>' +
        '</div>' +
        '<div class="info-head-actions">' +
          '<span class="info-kbd-badge"><kbd class="info-kbd">ESC</kbd> / <kbd class="info-kbd">B</kbd></span>' +
          '<button type="button" class="info-btn" data-action="close" aria-label="Close help">Close</button>' +
        '</div>' +
      '</header>' +
      '<div class="info-grid info-grid--help">' + body + '</div>' +
      '<footer class="info-footer">' +
        '<span class="info-footer-status">COMMONWEALTH SURVIVOR FIELD MANUAL</span>' +
        '<span class="info-footer-version">DOC-ID: VAULT-77-HELP</span>' +
      '</footer>' +
    '</section>';
  }

  function paint() {
    if (root) {
      root.innerHTML = renderHelp();
      var close = root.querySelector('[data-action="close"]');
      if (close && typeof close.focus === "function") {
        close.focus();
      }
    }
  }

  function close() {
    if (typeof window.coInfoAction === "function") {
      window.coInfoAction(JSON.stringify({ action: "close" }));
    }
  }

  function onKeydown(event) {
    if (event.key === "Escape") {
      event.preventDefault();
      close();
    }
  }

  function handleControllerInput(button, phase) {
    if (phase === "down" && (button === "b" || button === "a")) {
      close();
    }
  }

  window.coVoiceEvent = function (event) {
    if (!event || event.kind !== "settings" || !event.values || typeof event.values !== "object") {
      return;
    }
    VOICE_KEYS.forEach(function (key) {
      if (Object.prototype.hasOwnProperty.call(event.values, key)) {
        values[key] = event.values[key];
      }
    });
    paint();
  };
  window.handleControllerInput = handleControllerInput;
  window.coHelpModel = { renderHelp: renderHelp };

  function init() {
    root = document.getElementById("coInfo");
    if (!root) {
      return;
    }
    root.addEventListener("click", function (event) {
      var target = event.target && event.target.closest ? event.target.closest("[data-action]") : null;
      if (target && target.getAttribute("data-action") === "close") {
        close();
      }
    });
    document.addEventListener("keydown", onKeydown);
    document.documentElement.style.setProperty("--co-ui-scale", String(Math.max(0.75, Math.min(1.35, window.innerWidth / 1920))));
    paint();
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
