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
        detail: "External server process with multiple clients, JSON configuration and local or remote hosting.",
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
        detail: "Direct connect, browsable entries, LAN discovery, favourites, ping display and controller support.",
      },
      {
        name: "Server-based characters",
        detail: "Per-server saves, in-game character creation and profile binding when reconnecting.",
      },
      {
        name: "Development & hosting tools",
        detail: "CLI and graphical development servers, fake clients, weather controls and deployment scripts.",
      },
    ],
    note:
      "This phase is the field kit for testing and hosting: the server browser and menu are usable surfaces, while the underlying server remains independently deployable.",
    doneWhen:
      "A host can find or directly join a server, create a character and operate the session from the in-game tools.",
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
    status: "next",
    statusLabel: "Next",
    title: "Player control & server administration",
    summary:
      "The next dispatch expands what players and server operators can control while they are connected to a session.",
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
        name: "Server administration",
        detail: "Remote administration, logs, moderation, permissions and password-protected private servers.",
      },
    ],
    note:
      "These are operator and session-management surfaces. Their order can change as the combat and capacity work exposes new requirements.",
    doneWhen:
      "Players can configure their session and operators can manage, moderate and protect a running server without leaving the game workflow.",
  },
  {
    id: "social",
    number: "06",
    status: "future",
    statusLabel: "Future",
    title: "Social layer",
    summary:
      "Once the session and combat foundations are stable, the roadmap turns toward communication, identity and coordinated travel through the Commonwealth.",
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
      "Social features depend on trustworthy player identity and server ownership rules. They belong after the core session can preserve state predictably.",
    doneWhen:
      "Players can identify, communicate with and coordinate with one another while reconnects and permissions remain predictable.",
  },
  {
    id: "living-commonwealth",
    number: "07",
    status: "future",
    statusLabel: "Future",
    title: "A living Commonwealth",
    summary:
      "The long-range work is shared world simulation: interactions, creatures, progression, settlements and the public infrastructure needed to support it.",
    systems: [
      {
        name: "Interaction sync",
        detail: "Doors, containers, terminals, workbenches, pickups and dialogue with shared versus local rules.",
      },
      {
        name: "Enemy & NPC sync",
        detail: "Shared positions, combat state, health, death, loot and basic AI coordination.",
      },
      {
        name: "Quest & world progression",
        detail: "Selected quest stages, discovery, cleared locations, workshops and world-state changes.",
      },
      {
        name: "Settlements, power armour & trade",
        detail: "Cooperative construction, power armour state, inventory transfers, trading and shared persistence.",
      },
      {
        name: "Mod compatibility",
        detail: "Load-order checks, version negotiation and warnings when peers use content a client does not have.",
      },
      {
        name: "Public server infrastructure",
        detail: "Master listings, filters, latency and region information with moderation support.",
      },
    ],
    note:
      "This is the long horizon, not a claim that every Fallout system will be synchronized. Each subsystem needs its own ownership, persistence and compatibility rules first.",
    doneWhen:
      "Shared interactions, NPCs, progression, settlements and trade remain consistent across clients, reconnects and supported content sets.",
  },
];

const MULTIPLAYER_GATES = [
  {
    id: "transport",
    status: "complete",
    statusLabel: "Foundation",
    title: "Message semantics",
    summary:
      "Reliable control traffic, snapshot baselines and deltas, per-peer publication and client interpolation give the session a usable replication spine.",
  },
  {
    id: "authority",
    status: "next",
    statusLabel: "Queued",
    title: "Authority & relevancy",
    summary:
      "The server already filters interest by cell and worldspace. Per-entity authority, priority, instancing and epoch handoff are the next trust and bandwidth boundary.",
  },
  {
    id: "identity",
    status: "active",
    statusLabel: "Hardening",
    title: "Identity, resume & recovery",
    summary:
      "Profile identity, compatibility metadata, resume credentials and durable state exist, while default-on identity enforcement and legacy activation retirement remain active work.",
  },
  {
    id: "validation",
    status: "active",
    statusLabel: "Hardening",
    title: "Validation & abuse resistance",
    summary:
      "Packet bounds, finite-value checks, rate limits, combat sequence checks and server-side trust boundaries protect the session; new gameplay systems must preserve those gates.",
  },
  {
    id: "acceptance",
    status: "next",
    statusLabel: "Proof gate",
    title: "Multi-client acceptance",
    summary:
      "A multiplayer feature is not ready from source tests alone. Capacity, loss and jitter, reconnects, persistence recovery, cross-version sessions and real two-client gameplay need repeatable release evidence.",
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
        "Commonwealth Online is a Fallout 4 multiplayer framework with dedicated servers, remote player sync, appearance and equipment replication, shared world state and custom PrismaUI menus. Supports both Fallout 4 Anniversary Edition and the original release. No DLC, mods or Creation Club content required unless other players have them.",
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
        "A detailed, Fallout-themed field briefing for Commonwealth Online multiplayer framework milestones, from the working network foundation through future shared-world systems.",
      bodyClass: "co-roadmap-page",
      activeKey: "roadmap",
      roadmap: ROADMAP_PHASES,
      multiplayerGates: MULTIPLAYER_GATES,
      roadmapArt: [
        {
          src: "/assets/images/playerimages/UserOptcron-1.webp",
          width: 1920,
          height: 1080,
          alt: "Fallout 4 gameplay capture showing two Commonwealth Online players on a ruined road.",
          kicker: "COMMUNITY CAPTURE // 01",
          title: "A shared world needs a shared signal",
          copy: "The foundation is not just moving actors. It is deciding who owns a state, who needs to receive it and what happens when the connection gets rough.",
        },
        {
          src: "/assets/images/playerimages/UserOptcron-3.webp",
          width: 1920,
          height: 1080,
          alt: "Fallout 4 gameplay capture showing a player moving through a Commonwealth neighborhood.",
          kicker: "COMMUNITY CAPTURE // 02",
          title: "The long game is a world that survives",
          copy: "Social systems, settlements, inventory and quests only become multiplayer features when ownership, persistence and recovery are explicit.",
        },
      ],
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
      scripts: ["/static/js/links.js", "/static/js/navbar.js", "/static/js/script.js", "/static/js/url-policy.js", "/static/js/servers.js"],
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
      description:
        "Register or sign in to a Commonwealth Online account, link Discord, and select a built-in Fallout 4 themed profile icon.",
      bodyClass: "co-account-page",
      activeKey: "account",
      styles: ["/static/css/account.css"],
      scripts: [
        "/static/js/links.js",
        "/static/js/navbar.js",
        "/static/js/script.js",
        "https://cdn.jsdelivr.net/npm/@supabase/supabase-js@2.105.0",
        "/static/js/account.js",
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
