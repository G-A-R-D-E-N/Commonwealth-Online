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
          description: "Who can see when you are online."
        },
        {
          key: "profile.allowFriendJoin",
          label: "Allow Friend Join",
          type: "select",
          value: "Ask First",
          options: ["Always", "Ask First", "Never"],
          description: "How friends may join your current session."
        },
        {
          key: "profile.showLocationToFriends",
          label: "Show Location To Friends",
          type: "toggle",
          value: true,
          description: "Share your current cell or region with friends."
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
          description: "Show other connected players on the world map."
        },
        {
          key: "map.showFriendMarkers",
          label: "Show Friend Markers",
          type: "toggle",
          value: true,
          description: "Highlight friends with distinct map markers."
        },
        {
          key: "map.showNameplates",
          label: "Show Nameplates",
          type: "toggle",
          value: true,
          description: "Show player nameplates above nearby remote players."
        },
        {
          key: "map.mapMarkerSize",
          label: "Map Marker Size",
          type: "select",
          value: "Medium",
          options: ["Small", "Medium", "Large"],
          description: "Size of player and friend markers on the map."
        },
        {
          key: "map.friendRequests",
          label: "Friend Requests",
          type: "select",
          value: "Everyone",
          options: ["Everyone", "Friends of Friends", "Nobody"],
          description: "Who can send you friend requests."
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
          description: "Preferred matchmaking region for public servers."
        },
        {
          key: "network.networkSmoothing",
          label: "Network Smoothing",
          type: "toggle",
          value: true,
          description: "Smooth remote transforms when packets arrive late."
        },
        {
          key: "network.interpolationQuality",
          label: "Interpolation Quality",
          type: "select",
          value: "High",
          options: ["Low", "Medium", "High"],
          description: "Smoothness of remote player movement interpolation."
        },
        {
          key: "network.showPing",
          label: "Show Ping",
          type: "toggle",
          value: true,
          description: "Display your current latency on the HUD."
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
