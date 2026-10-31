(function () {
  "use strict";

  function isObject(value) {
    return !!value && typeof value === "object" && !Array.isArray(value);
  }

  function isPlayerId(value) {
    return Number.isInteger(value) && value >= 0;
  }

  function nameFor(playerId, players) {
    var match = players.find(function (player) {
      return player.playerId === playerId;
    });
    return match ? match.name : "Player " + playerId;
  }

  function createHud() {
    var status = { active: false, micHot: false, pending: false };
    var players = [];
    var speakerIds = [];
    return {
      apply: function (event) {
        if (!isObject(event)) {
          return;
        }
        if (event.kind === "status" && typeof event.active === "boolean") {
          status = { active: event.active, micHot: event.micHot === true, pending: event.pending === true };
          if (!status.active) {
            players = [];
            speakerIds = [];
          }
        } else if (event.kind === "players" && Array.isArray(event.players)) {
          players = event.players.filter(function (player) {
            return isObject(player) && isPlayerId(player.playerId) && typeof player.name === "string";
          });
        } else if (event.kind === "speakers" && Array.isArray(event.playerIds)) {
          speakerIds = status.active ? event.playerIds.filter(isPlayerId) : [];
        }
      },
      view: function () {
        return {
          micVisible: status.active && status.micHot,
          speakers: speakerIds.map(function (playerId) {
            return nameFor(playerId, players);
          }),
          pending: status.active && status.pending
        };
      }
    };
  }

  var hud = createHud();

  function paint() {
    var mic = document.getElementById("coVoiceMic");
    var speakers = document.getElementById("coVoiceSpeakers");
    var pending = document.getElementById("coVoicePending");
    if (!mic || !speakers || !pending) {
      return;
    }
    var view = hud.view();
    mic.hidden = !view.micVisible;
    pending.hidden = !view.pending;
    speakers.textContent = "";
    view.speakers.forEach(function (name) {
      var item = document.createElement("li");
      item.textContent = name;
      speakers.appendChild(item);
    });
  }

  window.coVoiceEvent = function (event) {
    hud.apply(event);
    paint();
  };
  window.coVoiceHudModel = {
    createHud: createHud
  };
})();
