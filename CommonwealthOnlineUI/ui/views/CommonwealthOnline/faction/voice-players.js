(function () {
  "use strict";

  var STEP = 10;

  function isObject(value) {
    return !!value && typeof value === "object" && !Array.isArray(value);
  }

  function isPlayerId(value) {
    return Number.isInteger(value) && value >= 0;
  }

  function isVolume(value) {
    return Number.isInteger(value) && value >= 0 && value <= 100;
  }

  function isPlayer(value) {
    return isObject(value) && isPlayerId(value.playerId) && isVolume(value.volume);
  }

  function createPlayers() {
    var active = false;
    var players = [];
    var speakerIds = [];
    return {
      apply: function (event) {
        if (!isObject(event)) {
          return;
        }
        if (event.kind === "status" && typeof event.active === "boolean") {
          active = event.active;
          if (!active) {
            players = [];
            speakerIds = [];
          }
        } else if (event.kind === "players" && Array.isArray(event.players)) {
          players = event.players.filter(isPlayer);
        } else if (event.kind === "speakers" && Array.isArray(event.playerIds)) {
          speakerIds = active ? event.playerIds : [];
        }
      },
      view: function () {
        if (!active) {
          return { active: false, rows: [] };
        }
        return {
          active: true,
          rows: players.map(function (player) {
            return {
              playerId: player.playerId,
              name: typeof player.name === "string" ? player.name : "Player " + player.playerId,
              muted: player.muted === true,
              volume: player.volume,
              speaking: speakerIds.indexOf(player.playerId) >= 0
            };
          })
        };
      }
    };
  }

  function muteAction(row) {
    return { op: "mutePlayer", playerId: row.playerId, muted: !row.muted };
  }

  function volumeAction(row, step) {
    var volume = Math.min(100, Math.max(0, row.volume + step));
    return volume === row.volume ? null : { op: "setPlayerVolume", playerId: row.playerId, volume: volume };
  }

  var players = createPlayers();
  var shownActive = null;

  function send(action) {
    if (typeof window.coVoiceAction === "function") {
      window.coVoiceAction(JSON.stringify(action));
    } else {
      console.warn("coVoiceAction is not available; the voice action was dropped");
    }
  }

  function element(tagName, className, text) {
    var node = document.createElement(tagName);
    node.className = className;
    node.textContent = text;
    return node;
  }

  function button(label, key, onClick) {
    var node = element("button", "faction-btn", label);
    node.dataset.voiceKey = key;
    node.addEventListener("click", onClick);
    return node;
  }

  function sendVolume(row, step) {
    var action = volumeAction(row, step);
    if (action) {
      send(action);
    }
  }

  function speakingText(row) {
    return row.speaking ? "Speaking" : "";
  }

  function buildRow(row) {
    var item = element("li", "faction-row", "");
    item.appendChild(element("span", "faction-row__name", row.name));
    var marker = element("span", "voice-speaking", speakingText(row));
    marker.dataset.voiceKey = "speak-" + row.playerId;
    item.appendChild(marker);
    item.appendChild(button(row.muted ? "Unmute" : "Mute", "mute-" + row.playerId, function () {
      send(muteAction(row));
    }));
    item.appendChild(button("-", "down-" + row.playerId, function () {
      sendVolume(row, -STEP);
    }));
    item.appendChild(element("span", "voice-volume", String(row.volume)));
    item.appendChild(button("+", "up-" + row.playerId, function () {
      sendVolume(row, STEP);
    }));
    return item;
  }

  function focusedKey() {
    var active = document.activeElement;
    return active && active.dataset && active.dataset.voiceKey ? active.dataset.voiceKey : "";
  }

  function restoreFocus(list, key) {
    if (!key) {
      return;
    }
    var target = list.querySelector("[data-voice-key=\"" + key + "\"]");
    if (target) {
      target.focus();
    }
  }

  function paint() {
    var status = document.getElementById("coVoiceStatus");
    var list = document.getElementById("coVoiceList");
    if (!status || !list) {
      return;
    }
    var key = focusedKey();
    var view = players.view();
    shownActive = view.active;
    list.textContent = "";
    if (!view.active) {
      status.textContent = "Voice chat is not active.";
      return;
    }
    status.textContent = view.rows.length ? "" : "No other players in the session.";
    view.rows.forEach(function (row) {
      list.appendChild(buildRow(row));
    });
    restoreFocus(list, key);
  }

  function markSpeakers() {
    var list = document.getElementById("coVoiceList");
    if (!list) {
      return;
    }
    players.view().rows.forEach(function (row) {
      var marker = list.querySelector("[data-voice-key=\"speak-" + row.playerId + "\"]");
      if (marker) {
        marker.textContent = speakingText(row);
      }
    });
  }

  function showVoiceTab(voice) {
    var faction = document.getElementById("coFaction");
    var panel = document.getElementById("coVoice");
    var factionTab = document.getElementById("coTabFaction");
    var voiceTab = document.getElementById("coTabVoice");
    if (!faction || !panel || !factionTab || !voiceTab) {
      return;
    }
    faction.hidden = voice;
    panel.hidden = !voice;
    factionTab.setAttribute("aria-selected", String(!voice));
    voiceTab.setAttribute("aria-selected", String(voice));
  }
  window.coVoiceToggleTab = function () {
    var panel = document.getElementById("coVoice");
    showVoiceTab(!!panel && panel.hidden);
  };
  var factionTab = document.getElementById("coTabFaction");
  var voiceTab = document.getElementById("coTabVoice");
  if (factionTab && voiceTab) {
    factionTab.addEventListener("click", function () {
      showVoiceTab(false);
    });
    voiceTab.addEventListener("click", function () {
      showVoiceTab(true);
    });
  }
  window.coVoiceEvent = function (event) {
    players.apply(event);
    if (!isObject(event)) {
      return;
    }
    if (event.kind === "speakers") {
      markSpeakers();
    } else if (event.kind === "players" || players.view().active !== shownActive) {
      paint();
    }
  };
  window.coVoicePlayers = {
    createPlayers: createPlayers,
    muteAction: muteAction,
    volumeAction: volumeAction
  };
})();
