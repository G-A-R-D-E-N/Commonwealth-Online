"use strict";

const SERVER_FOOTER_COPY =
  "Independent fan project for Fallout 4 multiplayer framework research. Not affiliated with Bethesda or Microsoft. Supports both Fallout 4 Anniversary Edition and the original release. DLC, mods and Creation Club content are not required unless other players have them. Public server entries are curated in servers/server.json.";

const UPDATES_FOOTER_COPY =
  "Independent fan project for Fallout 4 multiplayer framework research. Not affiliated with Bethesda or Microsoft. Supports both Fallout 4 Anniversary Edition and the original release. DLC, mods and Creation Club content are not required unless other players have them. Updates are versioned in the repository and linked to GitHub and Nexus Mods.";

const ROADMAP_PHASES = [
  {
    id: "foundation",
    number: "01",
    status: "complete",
    statusLabel: "Complete",
    title: "Foundation & world link",
    summary:
      "The core link between Fallout 4 clients and a dedicated server is operational. Players can enter the same Commonwealth and see the session move through the game world.",
    systems: [
      {
        name: "Dedicated server runtime",
        detail: "External server process with multiple clients, JSON configuration, a graphical host, LAN discovery and Steam P2P hosting.",
      },
      {
        name: "Remote player proxies",
        detail: "Remote players are represented in-game with synchronized position, rotation, cell and worldspace transitions.",
      },
      {
        name: "Movement state",
        detail: "Idle, movement, sprinting, sneaking, jumping, weapon-drawn state and teleports are carried across the session.",
      },
      {
        name: "Version coverage",
        detail: "The framework supports Fallout 4 Anniversary Edition and the original release in the same project.",
      },
    ],
    note:
      "This is the multiplayer spine: connect clients, establish a server-owned session and keep remote actors present as the local game changes cells.",
    doneWhen:
      "A new client can join a dedicated server, enter the same world and observe remote movement through cell and worldspace changes.",
  },
  {
    id: "replication",
    number: "02",
    status: "complete",
    statusLabel: "Complete",
    title: "Identity, loadout & session sync",
    summary:
      "The systems that make another player feel like a player are in place, from appearance and apparel through the current shared time and weather path.",
    systems: [
      {
        name: "Character appearance",
        detail: "Body morphs, tints, hair, head parts, facial sliders and versioned optional data are replicated.",
      },
      {
        name: "Clothing & equipment",
        detail: "Tracked apparel slots stay mirrored, including equipment changes made while a player is standing still.",
      },
      {
        name: "Ranged weapon state",
        detail: "Right-hand weapons resolve locally, equip on proxies and preserve drawn or holstered state.",
      },
      {
        name: "Shared time & weather",
        detail: "Time, days passed and exterior weather synchronize, with corrections after menus and loading.",
      },
    ],
    note:
      "The current runtime synchronizes player-facing time and weather; the component and per-scope ownership migration remains separate engineering work. Optional content is resolved where available; DLC, mods and Creation Club content are not required unless another player is using assets your client needs to display.",
    doneWhen:
      "A reconnecting player sees the intended character, equipment and shared session conditions without manual repair.",
  },
  {
    id: "operations",
    number: "03",
    status: "complete",
    statusLabel: "Complete",
    title: "Terminal UI & hosting operations",
    summary:
      "The project has the practical tools needed to discover a server, create a character and operate a development session without hiding the multiplayer layer behind a prototype screen.",
    systems: [
      {
        name: "Custom main menu",
        detail: "PrismaUI-backed HTML menu with multiplayer access, embedded browsing, settings and input capture.",
      },
      {
        name: "Server browser",
        detail: "Public Browse, LAN discovery, favourites, recent servers, Direct Connect, Steam friends hosting, password prompts and controller support.",
      },
      {
        name: "Server-based characters",
        detail: "Per-server saves, in-game character creation and profile binding when reconnecting.",
      },
      {
        name: "Internet hosting & host tools",
        detail: "Graphical hosting, Steam friend codes, optional public listing, passwords, connected-player status, kick/ban controls, CLI tools and deployment scripts.",
      },
    ],
    note:
      "Version 1.1.0 completes the practical hosting path: home hosts can use Iroh without port forwarding, keep a world private by sharing an invite code, or opt into authenticated public discovery. The server browser and controller path are usable surfaces while the relay remains independently deployable.",
    doneWhen:
      "A host can start and moderate a Steam or dedicated-server session, and players can browse, reconnect or directly join from the in-game tools.",
  },
  {
    id: "combat-capacity",
    number: "04",
    status: "active",
    statusLabel: "Active",
    title: "Combat & capacity",
    summary:
      "Current work is focused on making shared players behave correctly under combat pressure while increasing the number of players the architecture can carry.",
    systems: [
      {
        name: "Animation refinement",
        detail: "Armed locomotion, draw and holster transitions, firing, melee, strafing and jump recovery.",
      },
      {
        name: "Weapon & combat actions",
        detail: "Firing, reloads, melee, grenades, aiming, hit reactions and death or revival events.",
      },
      {
        name: "Expanded player capacity",
        detail: "Proxy allocation and runtime performance work toward a long-term architecture target of approximately 16 players.",
      },
    ],
    note:
      "The active gate is believable multiplayer combat: movement, animation and action state must agree across clients before the world can safely scale up.",
    doneWhen:
      "Two or more clients can move, fight and recover together while action state remains believable under the target session load.",
  },
  {
    id: "player-control",
    number: "05",
    status: "complete",
    statusLabel: "Complete",
    title: "Player control & server administration",
    summary:
      "Shipped in 2.1.0: the in-game admin menu, live server settings, bans, kicks and strikes, combat options, raid settings, multiplayer settings and the pause-menu map.",
    systems: [
      {
        name: "Multiplayer settings",
        detail: "Profile, voice, map and social, network and keybind options with persistent configuration.",
      },
      {
        name: "Fallout 4 settings bridge",
        detail: "Open the original Scaleform settings over the Commonwealth Online UI when the game needs its native controls.",
      },
      {
        name: "Pause menu & map",
        detail: "Zoom, pan, player markers and settings panels connected to live multiplayer data.",
      },
      {
        name: "Expanded server administration",
        detail: "Extend the shipped password, kick, ban, unban and session-status controls with richer permissions, remote administration and logs.",
      },
    ],
    note:
      "Shipped in 2.1.0.",
    doneWhen:
      "Players can configure their session and operators can manage, moderate and protect a running server without leaving the game workflow.",
  },
  {
    id: "social",
    number: "06",
    status: "complete",
    statusLabel: "Complete",
    title: "Social layer",
    summary:
      "Shipped in 2.1.0: voice chat, chat channels, a saved friends list, factions with ranks and their own chat, and a live map with fog of war.",
    systems: [
      {
        name: "Map & markers",
        detail: "Local and remote players, friends, locations, quests and custom markers.",
      },
      {
        name: "Profiles & gamertags",
        detail: "Display names, icons, per-server characters, friends and optional account links.",
      },
      {
        name: "Voice & text chat",
        detail: "Proximity voice, party channels, server text chat, direct messages and muting.",
      },
      {
        name: "Teams & social features",
        detail: "Parties, shared markers, invitations and group voice or text channels.",
      },
    ],
    note:
      "Built on the identity and ownership rules that shipped in 2.1.0.",
    doneWhen:
      "Players can identify, communicate with and coordinate with one another while reconnects and permissions remain predictable.",
  },
  {
    id: "living-commonwealth",
    number: "07",
    status: "next",
    statusLabel: "Next",
    title: "A living Commonwealth",
    summary:
      "The next dispatch brings the shared world to life: enemies and NPCs every player sees, shared loot, and selected quest progress that stays in step across clients.",
    systems: [
      {
        name: "Enemy & NPC sync",
        detail: "Shared positions, combat state, health, death, loot and basic AI coordination.",
      },
      {
        name: "Interaction sync",
        detail: "Doors, containers, terminals, workbenches, pickups and dialogue with shared versus local rules, beyond the settlement doors shipped in 2.1.0.",
      },
      {
        name: "Quest & world progression",
        detail: "Selected quest stages, discovery, cleared locations and world-state changes beyond the fireteam quests shipped in 2.1.0.",
      },
    ],
    note:
      "Each subsystem needs its own ownership, persistence and compatibility rules before it ships.",
    doneWhen:
      "Enemies, NPCs, loot and selected quest stages match across clients and survive reconnects.",
  },
  {
    id: "long-horizon",
    number: "08",
    status: "future",
    statusLabel: "Future",
    title: "Power armour, trade & public servers",
    summary:
      "Later work: power armour state, a deeper trade and settlement economy, and public server infrastructure with moderation support.",
    systems: [
      {
        name: "Power armour sync",
        detail: "Interactive power armour state: frame entry and exit, equipped plates, fusion core drain and field repair shared between players.",
      },
      {
        name: "Trade & settlement economy",
        detail: "Inventory transfers and shared persistence beyond the 2.1.0 trade window.",
      },
      {
        name: "Public server infrastructure",
        detail: "Master listings, filters, latency and region information with moderation support.",
      },
    ],
    note:
      "This is the long horizon. Scope is set after the next dispatch ships.",
    doneWhen:
      "Power armour, trade and public listings stay consistent across clients, reconnects and supported content sets.",
  },
];

const PUBLIC_PAGES = [
  {
    route: "/",
    template: "pages/home",
    output: "index.html",
    page: {
      title: "Commonwealth Online",
      description:
        "Commonwealth Online 1.1.0 is a Fallout 4 multiplayer framework with dedicated servers, Steam P2P hosting, Steam friend codes, authenticated public discovery, remote player sync, appearance and equipment replication, shared world state and custom PrismaUI menus. Supports both Fallout 4 Anniversary Edition and the original release.",
      bodyClass: "co-home-page",
      activeKey: "home",
      roadmap: ROADMAP_PHASES,
      scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js"],
    },
  },
  {
    route: "/media",
    template: "pages/media",
    output: "media/index.html",
    page: {
      title: "Media - Commonwealth Online",
      description:
        "In-game screenshots and multiplayer demonstrations from Commonwealth Online on Fallout 4 Anniversary Edition and the original release. Remote players, equipment sync, community characters and custom menus.",
      bodyClass: "co-media-page",
      activeKey: "media",
      scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js"],
    },
  },
  {
    route: "/roadmap",
    template: "pages/roadmap",
    output: "roadmap/index.html",
    page: {
      title: "Roadmap - Commonwealth Online",
      description:
        "Explore the Commonwealth Online roadmap, from playing together to the future of a shared world.",
      bodyClass: "co-roadmap-page",
      activeKey: "roadmap",
      styles: ["/static/css/roadmap.css?v=20261102-3"],
      scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js"],
    },
  },
  {
    route: "/hosting",
    template: "pages/hosting",
    output: "hosting/index.html",
    page: {
      title: "Hosting Guide - Commonwealth Online",
      description:
        "Host a Commonwealth Online world through Steam with a Friend Code, or run a dedicated server and join it through Direct Connect.",
      bodyClass: "co-hosting-page",
      activeKey: "hosting",
      styles: ["/static/css/hosting.css"],
      scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js"],
    },
  },
  {
    route: "/servers",
    template: "pages/servers",
    output: "servers/index.html",
    page: {
      title: "Servers - Commonwealth Online",
      description:
        "Browse public Commonwealth Online servers for Fallout 4 Anniversary Edition and the original release. DLC, mods and Creation Club content are not required unless other players have them.",
      bodyClass: "co-servers-page",
      activeKey: "servers",
      scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js", "/static/js/url-policy.js", "/static/js/servers.js", "/static/js/servers-favorites.js"],
      footerCopy: SERVER_FOOTER_COPY,
    },
  },
  {
    route: "/updates",
    template: "pages/updates",
    output: "updates/index.html",
    page: {
      title: "Updates - Commonwealth Online",
      description:
        "Project updates and release notes for Commonwealth Online (Fallout 4 multiplayer, Anniversary Edition and the original release) maintained as Markdown changelogs in the public repository.",
      bodyClass: "co-updates-page",
      activeKey: "updates",
      scripts: [
        "/static/js/vendor/marked.min.js",
        "/static/js/links.js",
        "/static/js/navbar.js",
        "/static/js/script.js",
        "/static/js/url-policy.js",
        "/static/js/updates.js",
      ],
      footerCopy: UPDATES_FOOTER_COPY,
    },
  },
  {
    route: "/account",
    template: "pages/account",
    output: "account/index.html",
    page: {
      title: "Account - Commonwealth Online",
      description: "Create a Commonwealth Online account or sign in to your existing profile.",
      bodyClass: "co-account-page",
      activeKey: "account",
      styles: ["/static/css/account.css"],
      scripts: [
        "/static/js/links.js",
        "/static/js/navbar.js",
        "/static/js/script.js",
        "/static/js/account.js",
      ],
    },
  },
  {
    route: "/profile",
    template: "pages/profile",
    output: "profile/index.html",
    page: {
      title: "Profile - Commonwealth Online",
      description: "Manage your Commonwealth Online profile, security, privacy, friends, and notifications.",
      bodyClass: "co-profile-page",
      activeKey: "account",
      styles: ["/static/css/profile.css?v=20260929-1", "/static/css/social.css?v=20260929-1"],
      scripts: [
        "/static/js/links.js",
        "/static/js/navbar.js",
        "/static/js/script.js",
        "/static/js/profile.js",
        "/static/js/profile-social.js",
        "/static/js/profile-factions.js",
        "/static/js/profile-notifications.js",
        "/static/js/profile-badges.js",
        "/static/js/profile-servers.js",
        "/static/js/profile-characters.js",
      ],
    },
  },
  {
    route: "/members",
    template: "pages/members",
    output: "members/index.html",
    page: {
      title: "Members - Commonwealth Online",
      description: "Browse public Commonwealth Online member profiles.",
      bodyClass: "co-members-page",
      activeKey: "members",
      styles: ["/static/css/social.css?v=20260929-1"],
      scripts: [
        "/static/js/links.js",
        "/static/js/navbar.js",
        "/static/js/script.js",
        "/static/js/members.js",
      ],
    },
  },
  {
    route: "/member",
    template: "pages/member",
    output: "member/index.html",
    page: {
      title: "Member - Commonwealth Online",
      description: "View a public Commonwealth Online member profile.",
      bodyClass: "co-member-page",
      activeKey: "members",
      styles: ["/static/css/social.css?v=20260929-1"],
      scripts: [
        "/static/js/links.js",
        "/static/js/navbar.js",
        "/static/js/script.js",
        "/static/js/member.js",
        "/static/js/member-servers.js",
        "/static/js/member-characters.js",
      ],
    },
  },
  {
    route: "/factions",
    template: "pages/factions",
    output: "factions/index.html",
    page: {
      title: "Factions - Commonwealth Online",
      description: "Browse approved Commonwealth Online player factions, lore, focus, and recruitment status.",
      bodyClass: "co-factions-page",
      activeKey: "factions",
      styles: ["/static/css/social.css?v=20260929-1", "/static/css/factions.css?v=20260921-2"],
      scripts: [
        "/static/js/links.js",
        "/static/js/navbar.js",
        "/static/js/script.js",
        "/static/js/factions.js",
      ],
    },
  },
  {
    route: "/faction",
    template: "pages/faction",
    output: "faction/index.html",
    page: {
      title: "Faction - Commonwealth Online",
      description: "View a Commonwealth Online faction profile, lore, recruitment status, and roster.",
      bodyClass: "co-faction-page",
      activeKey: "factions",
      styles: ["/static/css/social.css?v=20260929-1", "/static/css/factions.css"],
      scripts: [
        "/static/js/links.js",
        "/static/js/navbar.js",
        "/static/js/script.js",
        "/static/js/faction.js",
      ],
    },
  },
  {
    route: "/factions/apply",
    template: "pages/faction-apply",
    output: "factions/apply/index.html",
    page: {
      title: "Faction Application - Commonwealth Online",
      description: "Apply to establish a persistent player faction in Commonwealth Online.",
      bodyClass: "co-faction-apply-page",
      activeKey: "factions",
      styles: ["/static/css/profile.css?v=20260929-1", "/static/css/factions.css?v=20260921-3"],
      scripts: [
        "/static/js/links.js",
        "/static/js/navbar.js",
        "/static/js/script.js",
        "/static/js/faction-apply.js",
      ],
    },
  },
  {
    route: "/faction/manage",
    template: "pages/faction-manage",
    output: "faction/manage/index.html",
    page: {
      title: "Manage Faction - Commonwealth Online",
      description: "Manage Commonwealth Online faction membership requests, invitations, and roster.",
      bodyClass: "co-faction-manage-page",
      activeKey: "factions",
      styles: ["/static/css/profile.css?v=20260929-1", "/static/css/social.css?v=20260929-1", "/static/css/factions.css"],
      scripts: [
        "/static/js/links.js",
        "/static/js/navbar.js",
        "/static/js/script.js",
        "/static/js/faction-manage.js",
      ],
    },
  },
  {
    route: "/factions/review",
    template: "pages/faction-review",
    output: "factions/review/index.html",
    page: {
      title: "Review Factions - Commonwealth Online",
      description: "Review pending Commonwealth Online faction applications.",
      bodyClass: "co-faction-review-page",
      activeKey: "factions",
      styles: ["/static/css/profile.css?v=20260929-1", "/static/css/social.css?v=20260929-1", "/static/css/factions.css?v=20260921-4"],
      scripts: [
        "/static/js/links.js",
        "/static/js/navbar.js",
        "/static/js/script.js",
        "/static/js/faction-review.js",
      ],
    },
  },
];

const LEGACY_REDIRECTS = {
  "/index.html": "/",
  "/media/index.html": "/media",
  "/servers/index.html": "/servers",
  "/updates/index.html": "/updates",
};

const APPLICATIONS_PAGE = {
  route: "/apply",
  template: "pages/applications",
  output: "apply/index.html",
  page: {
    title: "Apply - Commonwealth Online",
    description: "Apply to join the Commonwealth Online team or become a beta tester.",
    bodyClass: "co-apply-page",
    activeKey: "applications",
    scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js"],
  },
};

const FORUM_PAGE = {
  route: "/forum",
  template: "pages/forum",
  output: "forum/index.html",
  page: {
    title: "Forum - Commonwealth Online",
    description: "Community discussion for the Commonwealth Online multiplayer framework.",
    bodyClass: "co-forum-page",
    activeKey: "forum",
    scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js", "/static/js/forum.js"],
  },
};

const STATIC_SHELL_PAGES = [APPLICATIONS_PAGE, FORUM_PAGE];

module.exports = {
  APPLICATIONS_PAGE,
  FORUM_PAGE,
  LEGACY_REDIRECTS,
  PUBLIC_PAGES,
  STATIC_SHELL_PAGES,
};
