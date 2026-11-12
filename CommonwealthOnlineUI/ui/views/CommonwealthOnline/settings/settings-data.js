(function () {
  "use strict";

  /**
   * Default multiplayer settings schema for Commonwealth Online.
   * Host views can pass a custom copy via CO_Settings.create({ settingsData }).
   */
  window.CO_SettingsData = [
    {
      section: "PROFILE",
      items: [
        {
          key: "profile.displayName",
          label: "Display Name",
          type: "text",
          value: "",
          description: "The name other players see in Commonwealth Online. Leave blank to use your character name. Changes save when you leave the field."
        },
        {
          key: "profile.onlineStatus",
          label: "Online Status",
          type: "select",
          value: "Friends",
          options: ["Everyone", "Friends", "Nobody"],
          description: "Nobody hides you: other players see you as offline. Everyone and Friends currently behave the same."
        },
        {
          key: "profile.allowFriendJoin",
          label: "Allow Friend Join",
          type: "select",
          value: "Always",
          options: ["Always", "Ask First", "Never"],
          description: "Who may join your session. Always: no prompt. Ask First: you accept or decline each join, and no answer within 60 seconds declines it. Never: every join is refused."
        },
        {
          key: "profile.showLocationToFriends",
          label: "Show Location To Friends",
          type: "toggle",
          value: true,
          description: "Friends can see your location on the world map. When off, only you, your party and your faction see it."
        }
      ]
    },
    {
      section: "VOICE CHAT",
      items: [
        {
          key: "voice.voiceChat",
          label: "Voice Chat",
          type: "toggle",
          value: true,
          description: "Enable voice chat with other players. Voice is relayed through the server or host."
        },
        {
          key: "voice.pushToTalk",
          label: "Push To Talk",
          type: "toggle",
          value: true,
          description: "Require a key hold to transmit voice chat."
        },
        {
          key: "voice.pushToTalkKey",
          label: "Push To Talk Key",
          type: "keybind",
          value: "B",
          description: "Key used for push-to-talk voice chat."
        },
        {
          key: "voice.whisperKey",
          label: "Whisper Key",
          type: "keybind",
          value: "",
          description: "Hold while talking to whisper to nearby players."
        },
        {
          key: "voice.shoutKey",
          label: "Shout Key",
          type: "keybind",
          value: "",
          description: "Hold while talking to shout to distant players."
        },
        {
          key: "voice.openMicSensitivity",
          label: "Open Mic Sensitivity",
          type: "slider",
          value: -45,
          min: -80,
          max: 0,
          step: 5,
          description: "Open mic threshold in dBFS. The bar shows your live microphone level."
        },
        {
          key: "voice.voiceChatVolume",
          label: "Voice Chat Volume",
          type: "slider",
          value: 85,
          min: 0,
          max: 100,
          step: 5,
          description: "Adjusts incoming voice chat volume."
        },
        {
          key: "voice.mutePlayersByDefault",
          label: "Mute Players By Default",
          type: "toggle",
          value: false,
          description: "Start new sessions with other players muted."
        },
        {
          key: "voice.deafened",
          label: "Deafen",
          type: "toggle",
          value: false,
          description: "Stop hearing all voice chat."
        },
        {
          key: "voice.micMuted",
          label: "Mute Microphone",
          type: "toggle",
          value: false,
          description: "Never transmit your microphone."
        },
        {
          key: "voice.status",
          label: "Voice Status",
          type: "text",
          editable: false,
          value: "Not connected",
          description: "Current voice chat state."
        }
      ]
    },
    {
      section: "MAP & SOCIAL",
      items: [
        {
          key: "map.showPlayerMarkers",
          label: "Show Player Markers",
          type: "toggle",
          value: true,
          description: "Show other connected players on the world map. Off hides every other player's marker, including friends, party and faction."
        },
        {
          key: "map.showFriendMarkers",
          label: "Show Friend Markers",
          type: "toggle",
          value: true,
          description: "Show friends on the world map. Off hides friend-only markers; a friend who is also in your party or faction still shows."
        },
        {
          key: "map.showNameplates",
          label: "Show Nameplates",
          type: "toggle",
          value: true,
          description: "Show remote players' names in the game. Off shows them as Player."
        },
        {
          key: "map.mapMarkerSize",
          label: "Map Marker Size",
          type: "select",
          value: "Medium",
          options: ["Small", "Medium", "Large"],
          description: "Size of other player and friend markers on the world map: Small 75%, Medium 100%, Large 150%."
        },
        {
          key: "map.friendRequests",
          label: "Friend Requests",
          type: "select",
          value: "Everyone",
          options: ["Everyone", "Friends of Friends", "Nobody"],
          description: "Who can send you friend requests. Friends of Friends needs a shared friend. Nobody declines new requests."
        }
      ]
    },
    {
      section: "NETWORK",
      items: [
        {
          key: "network.regionPreference",
          label: "Region Preference",
          type: "select",
          value: "Auto",
          options: ["Auto", "North America", "Europe", "Asia", "Oceania"],
          description: "Preferred region for the server browser. It only changes the order of servers and has no effect on Steam P2P sessions."
        },
        {
          key: "network.networkSmoothing",
          label: "Network Smoothing",
          type: "toggle",
          value: true,
          description: "Smooth remote player movement when packets arrive late. Off shows each remote player where its packets put them, without smoothing."
        },
        {
          key: "network.interpolationQuality",
          label: "Interpolation Quality",
          type: "select",
          value: "High",
          options: ["Low", "Medium", "High"],
          description: "Smoothness of remote player movement. High is smoothest but shows remote players further behind; Low is closest to real time."
        },
        {
          key: "network.showPing",
          label: "Show Ping",
          type: "toggle",
          value: true,
          description: "Display your current latency on the network HUD. Steam P2P guests see no ping, shown as a dash."
        },
        {
          key: "network.networkStatsOverlay",
          label: "Network Stats Overlay",
          type: "toggle",
          value: false,
          description: "Show packet and bandwidth stats for debugging."
        }
      ]
    }
  ];
})();
